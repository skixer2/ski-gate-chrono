# run_full.ps1 - run the SGC full tier, then auto-push results to the OpenClaw host.
# ASCII only (PowerShell 5 safe). No secrets: push uses an ed25519 key (path only).
#
# FULL = release / tag candidate only. Day-to-day = smoke / core.
#
# Usage (from unit_tests/):
#   .\run_full.ps1                 # full + push, port COM8
#   .\run_full.ps1 -Port COM7      # different serial port
#   .\run_full.ps1 -SkipPush       # run tests, do not push
#   .\run_full.ps1 -Clean          # prune old tmp_test_results before running
#
# NOTE: test_s02_factory_reset.py is destructive (serial R) and wipes stored
# runs. It is intentional and runs near the END of full only, so earlier tests
# see the accumulated run history.
param(
  [string]$Port    = "COM8",
  [switch]$SkipPush,
  [switch]$NoPush,
  [switch]$Clean
)
$ErrorActionPreference = "Stop"

# Full tier = every keep/merge test_*.py in a sensible order (smoke/core first,
# then the rest). test_full_run.py (obsolete) is NOT included.
$fullTests = @(
  "test_sensor_injection.py",
  "test_flash.py",
  "test_s04_bhy2_rate.py",
  "test_s03_stream_run.py",
  "test_s05_ring_fill.py",
  "test_s06_ring_drain.py",
  "test_start_detector.py",
  "test_state_machine.py",
  "test_end_detector.py",
  "test_s02_factory_reset.py",
  "test_ring_buffer.py",
  "test_bit_packer.py",
  "test_edge_cases.py"
)

$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Write-Host "=== SGC full run: $runId (port $Port) ==="

if ($Clean) {
  Write-Host "Cleanup: pruning old tmp_test_results (keep 15 runs) ..."
  try {
    py cleanup_test_results.py --keep-runs 15
    if ($LASTEXITCODE -ne 0) {
      Write-Warning "Cleanup failed (exit $LASTEXITCODE); continuing anyway."
    }
  } catch {
    Write-Warning ("Cleanup failed: " + $_.Exception.Message)
  }
}

$failed = $false
foreach ($t in $fullTests) {
  Write-Host "--- $t ---"
  py sgc_test_harness.py --port $Port $t --run-id $runId
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "FAILED: $t (exit $LASTEXITCODE)"
    $failed = $true
  }
}

if (-not $SkipPush -and -not $NoPush) {
  Write-Host "=== Pushing $runId to OpenClaw host ==="
  $pushScript = Join-Path $PSScriptRoot "push_test_results.ps1"
  try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $pushScript -RunId $runId
    if ($LASTEXITCODE -ne 0) {
      Write-Warning "Push failed (exit $LASTEXITCODE). Results remain local in tmp_test_results."
    }
  } catch {
    Write-Warning ("Push failed: " + $_.Exception.Message)
    Write-Warning "Results remain local in tmp_test_results."
  }
}

if ($failed) {
  Write-Host "=== FULL FAILED ($runId) ==="
  exit 1
}
Write-Host "=== FULL PASSED ($runId) ==="
exit 0
