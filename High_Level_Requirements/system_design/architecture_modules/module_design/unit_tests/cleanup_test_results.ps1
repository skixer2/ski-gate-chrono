# cleanup_test_results.ps1 - prune old tmp_test_results artifacts.
# Thin wrapper around cleanup_test_results.py (cross-platform source of truth).
# ASCII only. Never deletes README.md / secrets*.env.
#
# Usage (from unit_tests/):
#   .\cleanup_test_results.ps1                  # keep 10 runs / 14 days
#   .\cleanup_test_results.ps1 -KeepRuns 15
#   .\cleanup_test_results.ps1 -MaxAgeDays 7
#   .\cleanup_test_results.ps1 -DryRun
param(
  [int]$KeepRuns     = 10,
  [int]$MaxAgeDays   = 14,
  [switch]$DryRun,
  [string]$ResultsDir = ""
)
$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "cleanup_test_results.py"
$pyArgs = @($script, "--keep-runs", "$KeepRuns", "--max-age-days", "$MaxAgeDays")
if ($DryRun) { $pyArgs += "--dry-run" }
if ($ResultsDir) { $pyArgs += @("--results-dir", $ResultsDir) }

& py @pyArgs
exit $LASTEXITCODE
