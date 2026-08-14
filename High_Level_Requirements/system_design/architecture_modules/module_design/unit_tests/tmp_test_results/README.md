# tmp_test_results — harness staging drop

**Purpose:** Automatic landing zone for smoke/core/full artifacts so the Lead
Systems Coordinator can read them without manual copy into `unit_tests/` root.

## How files get here

From harness **v2.27.0+**, any run with `--run-id` or `--ts` writes here by default:

```powershell
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
foreach ($t in 'test_sensor_injection.py','test_flash.py','test_s04_bhy2_rate.py') {
  py sgc_test_harness.py --port COM8 $t --run-id $runId
}
# → tmp_test_results/run_….md
# → tmp_test_results/run_…_test_….log
```

| Override | Effect |
|----------|--------|
| `--results-dir PATH` | Custom folder (absolute or under `unit_tests/`) |
| `--results-dir .` | Legacy: write into cwd (`unit_tests/`) |
| env `RESULTS_DIR` | Same as `--results-dir` if flag omitted |

## Sync to OpenClaw workspace

If the PC tree and the agent workspace are **not** the same mount:

1. Prefer keeping this folder inside the git working tree and `git pull` on the
   agent host after you push, **or**
2. One-shot copy into the agent-visible tree (example):

```powershell
# From unit_tests on PC — adjust destination to your OpenClaw/sync path
$dest = "\\wsl$\…\ski_gate_chrono\…\unit_tests\tmp_test_results"  # example only
Copy-Item -Force .\tmp_test_results\* $dest
```

Or use the helper: `py stage_test_results.py` (moves loose `run_*` from cwd into here).

## Hygiene

- Treat as **scratch**: safe to delete old `run_*` after ledger/history capture.
- `*.log` is gitignored repo-wide; prefer not committing large dumps.
- Coordinator reads latest `run_*.md` + failing `*.log` when you say “smoke done”.
