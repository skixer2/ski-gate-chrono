# System Tests — Device

*Updated 2026-08-07 — Opt-A storage + linear pre-roll (FW **v4.79**, tag `v4.79-best-s03`).*

System tests verify end-to-end device behavior over **serial** (JSON-lines).  
Hardware: Nicla Sense ME on COM port (e.g. `COM8`). Prefer **`-R`** after flash map changes.

> 📋 **Also:** unit harness under `module_design/unit_tests/` · [ADR-003](../architecture_modules/module_design/unit_tests/device/adr_003_littlefs_storage.md) · tag **`v4.79-best-s03`**  
> **Sibling:** `phone.md`

---

## Included in unit_tests harness loop

From `module_design/unit_tests/` the usual:

```powershell
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Get-ChildItem test_*.py | ForEach-Object {
  py sgc_test_harness.py --port COM8 $_ --run-id $runId
}
```

now also runs **`test_s03_stream_run.py`**, **`test_s04_bhy2_rate.py`**, **`test_s05_ring_fill.py`**, **`test_s06_ring_drain.py`**
(wrappers → these scripts). One `$runId.md` + per-file `.log`.

**Note:** bare `py test_stream_run.py …` from `system_tests/` is the same S03 body as the harness wrapper (defaults: duration 25, gates 10, seed 45, `-R`).

## Quick suite (bench rate + pre-roll only)

From `system_tests/`:

```powershell
# After flash of matching FW (v4.79+):
py run_device_suite.py COM8 -R --with-s03
# or individually:
py test_bhy2_rate.py COM8 --duration 20 -R          # S04
py test_ring_fill.py COM8 -R                        # S05 (target 1000)
py test_ring_drain.py COM8 -R --fill-s 12           # S06
py test_stream_run.py COM8 --duration 25 --gates 10 --seed 45 -R  # S03
```

| ID | Script | What it proves | Pass (v4.79) |
|----|--------|----------------|--------------|
| **S03** | `test_stream_run.py` | USB stream SM + integrity | 100% frame match (USB fps ~40–55 is OK) |
| **S04** | `test_bhy2_rate.py` | Live BHY2 → encode → RawRunStore | **fps ≥ 90**, `store=raw`, `we=0` (~**99.5**) |
| **S05** | `test_ring_fill.py` | ARMED linear fill program-only | fill **≥ 90 fps** to target (default 1000) |
| **S06** | `test_ring_drain.py` | LOGGING pop2+push1 drain | drain **~10–14 s** for keep=1000; `fr` ≳ keep; no r=1 hang |

**Tag:** `v4.79-best-s03` — current best (S03 integrity race fix + skip redundant preroll prep).  
Prior: `v4.78-best-preroll`, `v4.71-best-s04`.

---

## Architecture under test (summary)

```
ARMED:   linear pre-roll program-only (cap 3000 ≈ 30 s)
         prepare_preroll() on enter IDLE + boot (erase off fill path)
LOGGING: trim_to_newest(1000) → pop2 encode + push1 live → then live direct
         force 'l': no ring (S04 pure rate)
Runs:    RawRunStore @ 0x14000+  (Opt-A, pre-erase slot @ POST_RUN/boot)
```

Flash map (v4.77+):

```
0x0000–0x13FFF   Pre-roll 4000×20B (3000 ARM + 1000 drain headroom)
0x14000–0x1FBFFF RawRunStore 8 slots
0x1FC000         Config
0x1FD000         Index RRS1
```

Serial of note:

| Cmd | Meaning |
|-----|---------|
| `a` / `l` / `p` / `i` | ARM / force LOGGING (no drain) / POST_RUN / IDLE |
| `L` | Bench **drain** LOGGING (`g_bench_drain`, no desk end-det); tm must be 0 |
| `O 0/1` | Onboard RGB off/on (default off when strip path built) |
| `?` | Status — **must not** `persist_index` mid-LOGGING |
| `T` | Toggle test mode only |

---

## Scenario catalog

| ID | Scenario | Method | Status |
|----|----------|--------|--------|
| **S01** | Full run pipeline (harness / stream) | unit harness / S03 | ✅ |
| **S02** | Factory reset | serial `R` | ✅ |
| **S03** | Stream injection + integrity | `test_stream_run.py` | ✅ **suite / harness** |
| **S04** | BHY2 LOGGING rate (Opt-A) | `test_bhy2_rate.py` | ✅ **suite** |
| **S05** | ARMED pre-roll fill rate | `test_ring_fill.py` | ✅ **suite** |
| **S06** | LOGGING pre-roll drain | `test_ring_drain.py` | ✅ **suite** |

### S03 — Stream injection

USB pull model; **not** a 100 Hz rate gate (host-bound ~40–55 fps).  
Pass: state transitions + decompressor match ≥95% (typically 100%).

```powershell
py test_stream_run.py COM8 --duration 25 --gates 10 --save run.ndjson --seed 45 -R
# Proven v4.79: 2406 frames, 100% integrity, LOGGING ~54.5 fps USB (not a rate gate)
```

### S04 — BHY2 real LOGGING rate

```powershell
py test_bhy2_rate.py COM8 --duration 20 -R
py test_bhy2_rate.py COM8 --duration 20 -R --onboard-led   # optional I2C RGB
```

- Sequence: IDLE → `a` → `l` → wait → `p`
- Expect: `store=raw`, fps ≥ 90, `we=0`
- Proven: ~99.5 fps with 10-LED strip bench + optional onboard

### S05 — ARMED fill

```powershell
py test_ring_fill.py COM8 -R                    # target 1000 (~10 s)
py test_ring_fill.py COM8 --target 0 --timeout 35  # toward 3000; ARM times out @ 30 s → use peak_r
py test_ring_fill.py COM8 -R --onboard-led
```

- Expect fill_fps ≥ 90; default target 1000
- Full 3000 races `ARM_TIMEOUT` 30 s — script accepts peak_r ≥ ~95% target

### S06 — Drain (pop2 + push1)

```powershell
py test_ring_drain.py COM8 -R --fill-s 12
```

- `L` (not `l`): drain path + bench skip end-det on desk
- Expect empty in **~10–14 s** for keep=1000 (fail if ≫20 s → r=1 deadlock regression)
- `fr` ≳ ~0.75 × min(r_at_L, 1000) plus live during drain

---

## Lessons learned (2026-08-07) — do not regress

1. **`?` must not `persist_index`/erase during LOGGING** — S04 heartbeats caused 99.4→97 fps.
2. **`T` is toggle, not query** — status carries `tm`; S04 must not toggle blindly.
3. **Test-mode `L` (set_la) only when tm=1** — bare `L` is drain LOGGING when tm=0.
4. **Circular 2-half ring ≠ 10 s live** — need linear buffer or larger multi-region design.
5. **prepare_preroll on enter IDLE** — erase off ARMED fill path.
6. **Drain needs +1000 headroom** after ARM cap 3000 for live pushes.
7. **pop2+push1:** if count==1, pop1 then **do not** push back (else r=1 forever).
8. **S03 USB fps ≠ ARMED/LOGGING sensor rate** — use S04/S05 for Hz.
9. **Onboard LED optional** (`O` / `--onboard-led`); strip GPIO may have no visible LEDs on Nicla.

---

## Legacy S01 / S02

Still valid via unit harness paths (see older sections / MASTER_TEST_PLAN).  
S01 stream-oriented detail superseded in practice by **S03 + S04 + S06** for current FW.

### S02 — Factory Reset

Serial `R` → boot → runs=0. Used as `-R` preamble on S04–S06.

---

## Pass criteria (suite)

| ID | Pass |
|----|------|
| S04 | fps ≥ 90, store=raw, we=0, full duration window |
| S05 | fill_fps ≥ 90 to target (or peak under ARM timeout for target=0) |
| S06 | drain_s ≤ ~18 s for keep=1000; fr sufficient; run_saved ok |
| S03 | integrity ≥95% (default in harness loop; `--with-s03` in run_device_suite) |
