# tmp_test_results — harness staging drop

**Purpose:** Automatic landing zone for smoke/core/full artifacts so the Lead
Systems Coordinator can read them without manual copy into `unit_tests/` root.

## Fast path (recommended)

Use the tier wrappers — they run the tests **and auto-push** the results to the
OpenClaw host, so the coordinator sees them without a manual push:

```powershell
.\run_smoke.ps1              # smoke tier + auto-push (port COM8)
.\run_smoke.ps1 -SkipPush    # run only, no push
.\run_smoke.ps1 -Clean       # prune old runs first (keep 15), then run + push
.\run_core.ps1               # core tier + auto-push
.\run_full.ps1               # full tier (release/tag only) + auto-push
```

`run_smoke.ps1` / `run_core.ps1` push even when tests fail (the coordinator
needs fail logs). Add `-Port COM7` to change the serial port.

## How files get here

**Preferred (auto-push at end):**

```powershell
.\run_smoke.ps1              # smoke + push
.\run_core.ps1               # core + push
.\run_smoke.ps1 -Clean       # prune old runs first (keep 15), then smoke + push
.\run_smoke.ps1 -SkipPush    # local only
```

From harness **v2.27.0+**, any run with `--run-id` or `--ts` writes here by default.
Harness **v2.28+** can also `--push` / `SGC_RESULTS_AUTO_PUSH=1` (optional; wrappers
already push once at the end).

Manual loop:

```powershell
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
foreach ($t in 'test_sensor_injection.py','test_flash.py','test_s04_bhy2_rate.py') {
  py sgc_test_harness.py --port COM8 $t --run-id $runId
}
.\push_test_results.ps1 -RunId $runId
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

## Push to OpenClaw host (SSH)

Preferred path when the PC tree and agent workspace are **not** the same mount:
`scp` the `run_*` artifacts straight to the VPS path the coordinator reads.

**Auth: ed25519 key only — no passwords in any script.**

Helpers (run from `unit_tests/`):

- `push_test_results.ps1` (Windows PowerShell 5+)
- `push_test_results.sh` (WSL / Linux mirror)

```powershell
.\push_test_results.ps1                         # newest run_*.md + its *.log
.\push_test_results.ps1 -RunId run_20260814_0900
.\push_test_results.ps1 -All                    # every run_* file
```

### One-time server setup (no secrets shown)

1. On the VPS as root: ensure user `node` (or `ubuntu`) exists, and that the
   workspace path `/home/node/.openclaw/workspace/ski_gate_chrono/…/unit_tests/tmp_test_results`
   is writable by that user.
2. Install **only the public key** into `~node/.ssh/authorized_keys`
   (dir mode `700`, file mode `600`).
3. On the PC generate the key (empty passphrase is OK for lab; otherwise use a
   passphrase + `ssh-agent`):

   ```powershell
   ssh-keygen -t ed25519 -f $HOME\.ssh\id_ed25519_sgc -C sgc-results
   ```

4. Copy **only** the `.pub` file to the server (e.g. append its contents to
   `authorized_keys`). Never copy the private key to the repo.
5. Test once:

   ```powershell
   ssh -i $HOME\.ssh\id_ed25519_sgc node@<host>
   ```

6. Run the push helper after the harness:

   ```powershell
   .\push_test_results.ps1
   ```

### Env vars (optional overrides)

| Var | Default |
|-----|---------|
| `SGC_RESULTS_HOST` | `72.60.88.60` (example only) |
| `SGC_RESULTS_PORT` | `22` |
| `SGC_RESULTS_USER` | `node` (or `ubuntu`) |
| `SGC_RESULTS_PATH` | `/home/node/.openclaw/workspace/…/unit_tests/tmp_test_results` |
| `SGC_RESULTS_KEY` | `%USERPROFILE%\.ssh\id_ed25519_sgc` |

See `secrets.example.env` for the placeholder template — copy to `secrets.env`
(never commit it).

### Security notes

- **Do NOT `git commit` private keys.** Keys live in the user profile, not the repo.
- `*.log` is gitignored repo-wide — SSH push is the transfer path for the
  coordinator, not git.
- Never commit `secrets.env`, `*.pem`, or `id_*` private keys. `.gitignore` covers
  these patterns; don't remove those lines.

## Cleanup

Prune old `run_*` artifacts so the drop folder does not grow forever. Keeps the
**10** most recent run groups (and anything newer than **14** days); deletes the
rest. Never touches `README.md` or `secrets*.env`.

```powershell
py cleanup_test_results.py --dry-run    # preview what would be deleted
py cleanup_test_results.py              # keep 10 runs / 14 days
py cleanup_test_results.py --keep-runs 15 --max-age-days 7
.\cleanup_test_results.ps1 -KeepRuns 15
```

`run_smoke.ps1 -Clean` (or `run_core.ps1 -Clean`) runs this automatically with
`--keep-runs 15` before the tests. The coordinator may also prune after ledger
capture (see `test_ledger/TEST_LEDGER.md`).

## Cleanup / hygiene

```powershell
.\cleanup_test_results.ps1                 # keep 10 run groups, drop >14 days
.\cleanup_test_results.ps1 -KeepRuns 15
.\cleanup_test_results.ps1 -DryRun
py cleanup_test_results.py --keep-runs 10 --max-age-days 14
```

Never deletes `README.md` / `secrets*.env`. Safe after coordinator has read a run.
`run_smoke.ps1 -Clean` prunes (keep 15) before the suite.

- Treat as **scratch**: safe to delete old `run_*` after ledger/history capture.
- `*.log` is gitignored repo-wide; prefer not committing large dumps.
- Coordinator reads latest `run_*.md` + failing `*.log` after auto-push.
