# SGC Test Framework

```
unit_tests/          ← Module unit tests + device system wrappers (test_*.py)
  sgc_test_harness.py
  test_state_machine.py … test_edge_cases.py   ← U01+ (JSON steps)
  test_s04_bhy2_rate.py                        ← S04 device (delegates)
  test_s05_ring_fill.py                        ← S05 device (delegates)
  test_s06_ring_drain.py                       ← S06 device (delegates)

system_design/system_tests/   ← Device system scripts (S03–S06 bodies)
  test_bhy2_rate.py / test_ring_fill.py / test_ring_drain.py / test_stream_run.py
  run_device_suite.py
  device.md
```

## All tests (your usual command)

From `unit_tests/` on the PC with Nicla on COM8:

```powershell
# All tests, one .md, multiple .log
rm $runId*
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Get-ChildItem test_*.py | ForEach-Object {
  py sgc_test_harness.py --port COM8 $_ --run-id $runId
}
```

This now picks up **S04 / S05 / S06** as well as the unit scenarios (U01…).

| File | Layer | Duration (order) |
|------|--------|------------------|
| `test_bit_packer.py` … `test_state_machine.py` | Unit | short |
| `test_s04_bhy2_rate.py` | Device S04 | ~30–45 s (+ factory reset) |
| `test_s05_ring_fill.py` | Device S05 | ~15–25 s |
| `test_s06_ring_drain.py` | Device S06 | ~30–50 s |

S04–S06 call into `system_tests/` and use **`-R`** once each. Full loop can take **several minutes**.

Device-only (no unit files):

```powershell
py run_device_suite.py COM8 -R
# from system_tests/
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
2. `run_device_test(port: str) -> bool` — standalone system script (S04–S06)

Same `--run-id` summary `.md` + per-file `.log` behavior.

## Docs

- System tests detail: `../../system_tests/device.md`
- Storage/pre-roll: `device/adr_003_littlefs_storage.md`
- Baseline tag: **`v4.78-best-preroll`**

## Hierarchy

| Layer | Location | What |
|-------|----------|------|
| **Unit** | `unit_tests/test_*.py` (SCENARIOS) | Single-module JSON steps |
| **Device system** | `test_s04/05/06_*.py` → `system_tests/` | Rate, fill, drain |
| **Stream** | `system_tests/test_stream_run.py` | S03 integrity (optional; not in Get-ChildItem unless you add a wrapper) |
