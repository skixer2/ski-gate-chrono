# SGC FW 4.90 — Release test board

**Baseline run:** `results/run_20260812_2223`  
**FW:** 4.90 (`firmware_snapshot/config.h`)  
**Port:** COM8  
**Date:** 2026-08-12  

## Scoreboard (all green)

| Test file | Result |
|-----------|--------|
| test_bit_packer.py | ✅ 20/20 |
| test_edge_cases.py | ✅ 83/83 |
| test_end_detector.py | ✅ 15/15 |
| test_flash.py | ✅ 10/10 |
| test_ring_buffer.py | ✅ 21/21 |
| test_s02_factory_reset.py | ✅ 4/4 |
| test_s03_stream_run.py | ✅ 1/1 |
| test_s04_bhy2_rate.py | ✅ 1/1 |
| test_s05_ring_fill.py | ✅ 1/1 |
| test_s06_ring_drain.py | ✅ 1/1 |
| test_sensor_injection.py | ✅ 21/21 |
| test_start_detector.py | ✅ 23/23 |
| test_state_machine.py | ✅ 27/27 |

See `results/run_20260812_2223.md` and per-test `*.log`.

Prior near-miss: `results/run_20260812_2201*` (S02 harness double-wait only; device wipe OK).

## Layout

```
v4.90/
  README.md                 ← this file
  FIRMWARE_IDENTITY.txt     ← git head + FW_VERSION
  MANIFEST.txt
  results/                  ← run_20260812_2223.* logs + summary
  scripts/
    unit_tests/             ← test_*.py + TEST_CATALOG.md + README
    harness/                ← sgc_test_harness.py, stream sim, decompressor
    system_tests/           ← S03–S06 bodies + run_device_suite.py + device.md
  firmware_snapshot/
    config.h                ← FW_VERSION "4.90"
```

## Reproduce (PC)

```powershell
cd F:\Documents\Progetti\ski-gate-chrono
git pull
cd High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Firmware_implementation
pio run -t upload -e nicla   # ver=4.90

cd ..\..\..\..\architecture_modules\module_design\unit_tests
# Or use scripts from this package (copy harness path):
# $env:PYTHONPATH = "...\release_tests\v4.90\scripts\harness;...\scripts\unit_tests"

$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Get-ChildItem test_*.py | ForEach-Object {
  py sgc_test_harness.py --port COM8 $_.FullName --run-id $runId
}
```

Tiers (smoke/core/full): see `scripts/unit_tests/TEST_CATALOG.md`.

## Notes

- Do **not** use FW 4.91–4.92 (serial regressions).
- S03 integrity dump can still flake rarely; functional path is solid on this board.
- S02 in this package is test_s02 2.26.1 (single-burst factory_reset/reboot).
