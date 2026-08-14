# SGC Test Framework

**What to run:** see **[TEST_CATALOG.md](TEST_CATALOG.md)** (smoke / core / full).
Default day-to-day = **core**, not full `Get-ChildItem test_*.py`.

**Cross-stack debug notebook:** **[test_ledger/TEST_LEDGER.md](test_ledger/TEST_LEDGER.md)**  
(architecture map, active manual/BLE case, serial+Flutter logs, coordinator task queue).
Lead Systems Coordinator owns the ledger; automated gates stay in TEST_CATALOG.

**Result drop folder:** **[tmp_test_results/](tmp_test_results/)** — harness **v2.27+** writes
`--run-id` / `--ts` artifacts there. Prefer **`run_smoke.ps1` / `run_core.ps1`** (auto-push).
Cleanup: `cleanup_test_results.ps1` / `py cleanup_test_results.py`. Harness **v2.28** optional `--push`.

```
unit_tests/          ← Module unit tests + device system wrappers (test_*.py)
  TEST_CATALOG.md    ← keep/merge/drop + tiers (source of truth)
  test_ledger/       ← Unified Test Ledger (active case + logs + tasks)
  sgc_test_harness.py
  test_state_machine.py … test_edge_cases.py   ← U01+ (JSON steps)
  test_s02_factory_reset.py                    ← S02 serial R
  test_s03_stream_run.py … test_s06_*.py       ← device (delegates)

system_design/system_tests/   ← Device system scripts (S03–S06 bodies)
  test_bhy2_rate.py / test_ring_fill.py / test_ring_drain.py / test_stream_run.py
  run_device_suite.py
  device.md
  test_full_run.py.obsolete   ← DO NOT RUN (r=500 era)
```

## Core loop (recommended)

From `unit_tests/` on the PC with Nicla on COM8:

```powershell
.\run_smoke.ps1     # smoke tier: run + auto-push results to the coordinator
.\run_core.ps1      # core tier:  run + auto-push
.\run_full.ps1      # full tier:  release/tag only + auto-push
```

`run_smoke.ps1` / `run_core.ps1` / `run_full.ps1` set the `run_` id, run the tier
files from TEST_CATALOG, and **always** push `tmp_test_results/run_*` to the
OpenClaw host at the end (even on failure, so the coordinator gets the fail
logs). Options: `-SkipPush` (run, no push), `-Port COM7`, `-Clean` (prune old
runs first).

Manual equivalent (full tier only — prefer the wrappers):

```powershell
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Get-ChildItem test_*.py | ForEach-Object {
  py sgc_test_harness.py --port COM8 $_ --run-id $runId
}
.\push_test_results.ps1 -RunId $runId
```

### Push results to the coordinator (SSH)

Automatic with `run_smoke.ps1` / `run_core.ps1`. Run manually only if you used a
raw loop above (one-time key setup in
[`tmp_test_results/README.md`](tmp_test_results/README.md)):

```powershell
.\push_test_results.ps1 -RunId $runId
```


Full `test_*.py` picks up **S02–S06** wrappers + all units (including long U20).

| File | Layer | Duration (order) |
|------|--------|------------------|
| `test_bit_packer.py` … `test_state_machine.py` | Unit | short |
| `test_s03_stream_run.py` | Device S03 | ~60–90 s (+ factory reset + integrity) |
| `test_s04_bhy2_rate.py` | Device S04 | ~30–45 s (+ factory reset) |
| `test_s05_ring_fill.py` | Device S05 | ~15–25 s |
| `test_s06_ring_drain.py` | Device S06 | ~30–50 s |

S03–S06 call into `system_tests/` and use **`-R`** once each. Full loop can take **several minutes**.

Device-only (no unit files):

```powershell
# from system_tests/
py run_device_suite.py COM8 -R --with-s03   # S03+S04+S05+S06
py run_device_suite.py COM8 -R             # S04+S05+S06 only
```

## Single file

```bash
pip install pyserial
python sgc_test_harness.py --port COM8 test_state_machine.py
python sgc_test_harness.py --port COM8 test_s04_bhy2_rate.py --run-id smoke
```

## Device wrappers API

Harness loads either:

1. `SCENARIOS = [TestScenario(...), ...]` — classic unit tests, or  
2. `run_device_test(port: str) -> bool` — standalone system script (S03–S06)

Same `--run-id` summary `.md` + per-file `.log` behavior.

## Docs

- System tests detail: `../../system_tests/device.md`
- Storage/pre-roll: `device/adr_003_littlefs_storage.md`
- JSON protocol: `device/json_protocol.md` (rm=3000, B/echo in **Pa**)
- Baseline board: FW **4.90** / `run_20260812_2223` (historical device tag `v4.79-best-s03`)
- Stream orchestrator: `sgc_stream_simulator.py` **SIM 2.50.0** (S03 integrity host retry)

## S03 integrity host retry (SIM 2.50.0+)

S03 dump step uses `h <id> raw`. On 4.90, CDC can race that line → device
`hex_err` **`bad_id`** / **`no_args`** while the run path was green.

`sgc_stream_simulator.verify_data_integrity` (used by `system_tests/test_stream_run.py`
and harness `test_s03_stream_run.py`) automatically retries up to **3×**:

```text
i → ? (wait status) → h <id> raw
```

- Retry: `bad_id`, `no_args`, or no chunks  
- Fail immediately: `no_run`, `bad_size`, `read_fail`  
- Details: `system_tests/device.md` § S03

Host-only; **do not** flash a serial-stack FW experiment for this flake.

## GS synthetic profile (SIM 2.51.0+)

`generate_gs_run()` produces baro descent **plus**:
- carved **yaw/roll** turns phased to gate spacing (app ω zero-crossings)
- **pole impacts** ~28 000 mm/s² lateral (~2.9 g), dual-lobe ~100 ms
- start pole-pushes + finish decel

LA wire units = **mm/s²** (int16). Phone ImpactDetector |g| = **9810** (app **1.9**).

```powershell
py test_stream_run.py COM8 --duration 25 --gates 10 --seed 45 -R
# optional: --gate-strength 28000
```

## Unit vs device contracts (do not regress)

| Topic | Working code (keep) | Unit harness must |
|-------|---------------------|-------------------|
| Pre-roll | linear 3000 + keep 1000 | use `wait_for_ring_count`, not `r==500` / short `ring_full` |
| force `l` | skips end detector (S04) | enter LOGGING via start det for U03/U07/U08 |
| `B` / echo `p` | Pascals | expect Pa (not hPa) |
| Manual B/Q/L | suppresses ARM→stream | call `enable_test_mode()` before unit inject |
| Stream S03 | ARM + 0x3F pull | do not send B/Q/L before ARM |
| S03 integrity | `h id raw` + decompress | SIM 2.50.0 retries `i`/`?`/`h` on `bad_id`/`no_args` |
| `f` flash test | reserved `0x1FE000` | allow ≥2 s for response |

## Hierarchy

| Layer | Location | What |
|-------|----------|------|
| **Unit** | `unit_tests/test_*.py` (SCENARIOS) | Single-module JSON steps |
| **Device system** | `test_s03/04/05/06_*.py` → `system_tests/` | Stream integrity, rate, fill, drain |
| **Stream body** | `system_tests/test_stream_run.py` | S03 (also via harness wrapper) |
