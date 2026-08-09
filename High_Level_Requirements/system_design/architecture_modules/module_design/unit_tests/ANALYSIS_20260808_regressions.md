# Unit-test regression analysis — 2026-08-08 run_20260808_0206

## Verdict

| Layer | Role in failures |
|-------|------------------|
| **Working device path (S04/S05/S06)** | ✅ PASS on FW 4.79 — **do not change** rate/pre-roll/drain design |
| **Unit harness + scenarios** | ❌ Primary cause — still assumed circular **rm=500**, short `ring_full` waits, force-`l` + end det |
| **Docs** | ❌ Stale (`json_protocol` rm=500, F02 “500 samples”, echo Pa vs hPa) |
| **Firmware** | ⚠️ Two real test-path bugs + one destructive self-test address; production path OK |

## Evidence from logs

1. **S04/S05/S06 all passed** → Opt-A + linear pre-roll is correct.
2. Status consistently shows `"rm": 3000`, `"r": 1200+` while unit tests expect `r=500, rm=500`.
3. `ring_full` never within 12 s — full 3000 @ ~100 Hz needs ~30 s and races `ARM_TIMEOUT`.
4. U15: ARM → stream (`?` spam) → timeout to IDLE because **Q did not set `g_manual_frame`** (only B did).
5. U17 echo: `B 101325` → echo `p: 399.56` = `(101325*50) % 65536 * 2 / 100` → **B treated as hPa** while protocol/tests send **Pa**.
6. U03/U07: force-`l` → end det **skipped** by design (S04) → POST_RUN never comes.
7. U12 `f`: TEST_ADDR=0 **erases pre-roll**; response may also exceed 500 ms wait.

## What was corrected

### Firmware 4.80 (minimal, preserves working code)

- `B` / echo: **Pascals** end-to-end (`set_pressure_pa`, `test_get_pressure` returns Pa).
- `Q` and `L` (tm=1) set `g_manual_frame` like `B` → unit ARM does not open stream.
- Default test frame: identity quat, zero LA, sea-level Pa.
- `flash_test()` uses reserved **0x1FE000**, then re-erases.

### Harness 2.20 + unit scenarios

- `ARM_FILL_CAP=3000`, `PREROLL_KEEP=1000`, `UNIT_RING_READY=200`.
- `wait_for_ring_count()` preferred over short `ring_full`.
- Start/end/state tests enter LOGGING via **start detector**, not force-`l`.
- Expectations updated off the old 500-slot contract.

### Docs

- `json_protocol.md`, ADR-003, requirements F02/F05, README unit contracts.

## What must NOT be “fixed” in firmware to please old units

- Do not shrink pre-roll back to 500.
- Do not make force-`l` run the end detector (breaks S04 semantics).
- Do not reintroduce LittleFS on the LOGGING hot path.

## How to verify on hardware

```powershell
# Build/upload FW 4.81, then from unit_tests/:
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Get-ChildItem test_*.py | ForEach-Object {
  py sgc_test_harness.py --port COM8 $_ --run-id $runId
}
# Or device suite only:
# py ..\..\system_tests\run_device_suite.py COM8 -R --with-s03
```

---

## Follow-up — run_20260809_0102 (FW 4.80 + harness 2.21)

### Scoreboard
| File | 08-08 | 09-01 |
|------|-------|-------|
| bit_packer / start / state / sensor / S03–S06 | mixed / fail | **all ✅** |
| edge_cases | 43–46/68 | **53/83** |
| end_detector | 9/15 | **12/16** |
| flash | 6/8 | **6/10** |
| ring_buffer | 12/18 | **8/21** |

### Remaining root cause (fixed in **FW 4.81** + harness **2.22**)
1. **POST_RUN called `test_stream_reset()` which cleared `g_manual_frame`.**  
   First cycle with `enable_test_mode` → B/Q/L → ARM filled ring ✅.  
   After `run_saved`, next ARM saw `!g_manual_frame` → opened **stream** with no PC server → `r=0` forever → `left ARMED (st=IDLE)` / `not_armed` cascade (E01 re-arm, E02×3, U09–U11, U07–U08, U13…).
2. **Stream not cleared on ARMED→IDLE** (only on POST_RUN) — sticky pull mode.
3. **Harness `_read_all` gap-broke** during `prepare_preroll` erase → E04/`i` only saw `preroll_prep`, missed `st`; U12 `f` empty during SPI erase.

### FW 4.81
- `test_stream_reset()` no longer clears `g_manual_frame`
- Enter IDLE clears `g_stream_active` + pull flags (keeps manual)
- Manual ARM forces `g_stream_active=false`
- `Serial.flush()` before preroll erase so `st` is not lost under SPI IRQ mask

### Harness 2.22
- `MIN_FW_VERSION=(4,81)`
- `enable_test_mode`: always `i` + re-assert B/Q/L
- `_read_all` waits through quiet erase windows
- `i`/`f` longer JSON windows; `i`→IDLE accepts `preroll_prep` or status IDLE fallback
