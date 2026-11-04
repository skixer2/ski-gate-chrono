# run_core.ps1 - run the SGC core tier, then auto-push results to the OpenClaw host.
# ASCII only (PowerShell 5 safe). No secrets: push uses an ed25519 key (path only).
#
# Usage (from unit_tests/):
#   .\run_core.ps1                  # core + push, port COM8
#   .\run_core.ps1 -Port COM7       # different serial port
#   .\run_core.ps1 -SkipPush        # run tests, do not push
#   .\run_core.ps1 -Clean           # prune old tmp_test_results before running
param(
  [string]$Port    = "COM8",
  [switch]$SkipPush,
  [switch]$NoPush,
  [switch]$Clean
)
$ErrorActionPreference = "Stop"

# Core tier = TEST_CATALOG "core": smoke + S03/S05/S06 + start_detector + state_machine.
$coreTests = @(
  "test_sensor_injection.py",
  "test_flash.py",
  "test_s04_bhy2_rate.py",
  "test_s03_stream_run.py",
  "test_s05_ring_fill.py",
  "test_s06_ring_drain.py",
  "test_start_detector.py",
  "test_state_machine.py"
)

$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Write-Host "=== SGC core run: $runId (port $Port) ==="

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
foreach ($t in $coreTests) {
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
  Write-Host "=== CORE FAILED ($runId) ==="
  exit 1
}
Write-Host "=== CORE PASSED ($runId) ==="
exit 0
