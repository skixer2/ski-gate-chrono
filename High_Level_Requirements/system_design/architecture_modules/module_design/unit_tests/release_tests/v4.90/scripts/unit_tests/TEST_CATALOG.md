# SGC Test Catalog (device serial) — 2026-08-12

**FW baseline:** 4.90 · **Port:** COM8 · **Harness:** `sgc_test_harness.py`

Maps every automated device test to a **keep / merge / drop** decision.  
Phone/BLE/cloud (📱 I01–I03, Dart) are out of scope here.

---

## Recommended run tiers

| Tier | When | Command (from `unit_tests/`) | ~Time |
|------|------|------------------------------|-------|
| **smoke** | After every FW flash | `test_sensor_injection.py` + `test_flash.py` + `test_s04_bhy2_rate.py` | ~2–3 min |
| **core** | Daily / before claim “green” | smoke + `test_s03` + `test_s05` + `test_s06` + `test_start_detector` + `test_state_machine` | ~8–12 min |
| **full** | Release / tag candidate | all `test_*.py` in harness loop | ~15–25 min |
| **device-only** | Rate/pre-roll bench | `system_tests/run_device_suite.py COM8 -R --with-s03` | ~5–8 min |

```powershell
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
# smoke
foreach ($t in 'test_sensor_injection.py','test_flash.py','test_s04_bhy2_rate.py') {
  py sgc_test_harness.py --port COM8 $t --run-id $runId
}
# core (add)
foreach ($t in 'test_s03_stream_run.py','test_s05_ring_fill.py','test_s06_ring_drain.py',
               'test_start_detector.py','test_state_machine.py') {
  py sgc_test_harness.py --port COM8 $t --run-id $runId
}
# full
Get-ChildItem test_*.py | % { py sgc_test_harness.py --port COM8 $_ --run-id $runId }
```

---

## KEEP — unique value (run these)

### Device system (gate product quality)

| ID | File | Proves | Notes |
|----|------|--------|-------|
| **S03** | `test_s03_stream_run.py` → `system_tests/test_stream_run.py` | Stream SM + start/end + encode + **hex integrity** | **Must keep.** USB fps ~40–55 OK. Integrity dump can flake — not start-det. |
| **S04** | `test_s04_*` → `test_bhy2_rate.py` | Live BHY2 LOGGING ≥90 fps, `store=raw` | **Must keep.** Opt-A rate gate. |
| **S05** | `test_s05_*` → `test_ring_fill.py` | ARMED linear fill ≥90 fps | **Must keep.** |
| **S06** | `test_s06_*` → `test_ring_drain.py` | LOGGING pop2+push1 drain ~10 s | **Must keep.** |

### Unit — state / detectors / inject / flash

| ID | File | Proves | Notes |
|----|------|--------|-------|
| **U01–U03** | `test_state_machine.py` | SLEEP/IDLE/ARMED + full natural cycle | Keep. U03 = start+end path (not force-`l`). |
| **U04–U06** | `test_start_detector.py` | 2.5 m / 3 m / no false start | Keep. |
| **U07–U08** | `test_end_detector.py` | Flat end / no false end on ascent | Keep. |
| **U12** | `test_flash.py` | SPI erase/program reserved sector | Keep (I04). |
| **U13** | `test_flash.py` | Run cycle increments storage | Keep (light F08). |
| **U16–U19** | `test_sensor_injection.py` | T/B/Q/L inject + echo Pa | Keep. **U19** is the known 4.90 flake. |

---

## MERGE / DEMOTE — overlapping (keep file, don’t treat as gate)

| ID | Overlaps | Decision |
|----|----------|----------|
| **U09–U11** ring_buffer | S05 fill + U02/U13 re-arm | **Demote from core.** Still useful debug; S05 is the rate truth. |
| **U14–U15** bit_packer | S03 integrity + encode path | **Demote.** S03 proves pack/unpack end-to-end; U14/U15 are weak proxies (force-`l`, no decompress assert). |
| **E01** ring wrap name | U09–U11 + S05 | **Demote.** Name stale (“wraparound”); behavior = fill+re-arm. |
| **E02** multi-run | U13 × 3 | **Demote.** Slower duplicate of U13. |
| **E10–E11** | U03 / U07 / U13 | **Demote.** Covered by detector + flash path. |
| **U03** vs **S03** | both full cycle | **Both keep** — U03 = inject+detectors (fast); S03 = stream+integrity (slow, unique). |

---

## DROP or quarantine — useless / harmful / obsolete

| Item | Why | Action |
|------|-----|--------|
| **`system_tests/test_full_run.py` (S01/S02)** | **Obsolete contracts:** expects `ring_full` @ **r=500**, old circular pre-roll; not in harness loop; would fail on Opt-A 3000. | **Quarantine** — rename `.obsolete.py`; do not run. S02 factory-reset → use new thin wrapper if needed. |
| **E03 zero-length** | Asserts blocked POST_RUN from ARMED; low product value; brittle | Optional keep as edge only; not core |
| **E04 rapid toggle** | Stress noise; rarely catches real bugs vs U02 | Demote |
| **E05–E07 invalid inject / quat** | Soft acceptance; don’t match production arm_refuse rules well | Demote (E07 still documents tm vs production) |
| **E08 blocked transitions** | Thin; overlaps U01–U02 | Demote |
| **E09 noise rejection** | Overlaps U06 | Demote |
| **Spike NDJSON / plots** in `system_tests/` | Manual artifacts, not CI | Keep on disk; never gate |
| **I01–I03 BLE** | Not implemented in this folder | Still needed someday — not “useless”, **missing** |
| **run_*.md/log piles** | Result archives | Keep latest few; don’t commit |

---

## MISSING — add when ready (high value)

| Gap | Req | Priority |
|-----|------|----------|
| **S02 factory reset** in harness loop | Thin wrapper: `R` → reboot → IDLE, runs=0 | **Added:** `test_s02_factory_reset.py` |
| **ARM_TIMEOUT → IDLE (~30 s)** | R02 / aborted start | **Added:** U20 in `test_state_machine.py` (long; not in smoke) |
| **U19 negative LA soak** | 20× `L 0 0 -9810` after noise | Optional later — don’t expand serial stack |
| **9th-run overwrite** | F08 auto-overwrite | Medium — device full-flash |
| **BLE I01–I03** | run list / FT / CRC | High product, needs phone/BLE rig |
| **Compression ratio assert** | P07 on S03 dump | Medium — extend S03 metrics |
| **S03 integrity retry** | 1 automatic re-dump on Bad hex | Medium harness polish |

---

## Coverage vs requirements (device serial only)

| Req area | Covered by | Gap |
|----------|------------|-----|
| F02 pre-roll | S05, U09–U11 | — |
| F04 start det | U04–U06, S03, U03 | — |
| F05 drain | S06 | — |
| F06 end det | U07–U08, S03 | — |
| F07 pack | S03 integrity | ratio % not asserted |
| F08 storage | U12–U13, E02 | 9th overwrite |
| Rate 100 Hz hot path | **S04 only** | F01 lab still 🔧 |
| Inject/test mode | U16–U19 | U19 flake |
| Factory reset | S02 wrapper | inductive 20 s still 🔧 |
| BLE | — | I01–I03 missing |

---

## File checklist (`Get-ChildItem test_*.py`)

| File | Tier | Verdict |
|------|------|---------|
| `test_sensor_injection.py` | smoke/core/full | KEEP |
| `test_flash.py` | smoke/core/full | KEEP |
| `test_s04_bhy2_rate.py` | smoke/core/full | KEEP |
| `test_s03_stream_run.py` | core/full | KEEP |
| `test_s05_ring_fill.py` | core/full | KEEP |
| `test_s06_ring_drain.py` | core/full | KEEP |
| `test_start_detector.py` | core/full | KEEP |
| `test_state_machine.py` | core/full | KEEP (+ U20 long opt) |
| `test_end_detector.py` | full | KEEP |
| `test_s02_factory_reset.py` | full | KEEP (new) |
| `test_ring_buffer.py` | full | DEMOTE (debug) |
| `test_bit_packer.py` | full | DEMOTE (debug) |
| `test_edge_cases.py` | full | DEMOTE (most E*) |
| `system_tests/test_full_run.py` | — | **OBSOLETE — do not run** |

---

## Traceability doc debt

`device/requirements_traceability.md` still says S01–S02 and ring 500 in places.  
**Source of truth for what to run:** this file + `device.md` (S03–S06).  
Update the matrix later; do not resurrect S01@r=500.
