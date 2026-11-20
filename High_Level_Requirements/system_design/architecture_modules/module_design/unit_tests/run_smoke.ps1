# run_smoke.ps1 - run the SGC smoke tier, then auto-push results to the OpenClaw host.
# ASCII only (PowerShell 5 safe). No secrets: push uses an ed25519 key (path only).
#
# Usage (from unit_tests/):
#   .\run_smoke.ps1                 # smoke + push, port COM8
#   .\run_smoke.ps1 -Port COM7      # different serial port
#   .\run_smoke.ps1 -SkipPush       # run tests, do not push
#   .\run_smoke.ps1 -Clean          # prune old tmp_test_results before running
param(
  [string]$Port    = "COM8",
  [switch]$SkipPush,
  [switch]$NoPush,
  [switch]$Clean
)
$ErrorActionPreference = "Stop"

# Smoke tier = TEST_CATALOG "smoke": sensor injection + flash + S04 rate.
#
# 2026-11-20 additions, both driven by real field/bench failures:
#   test_spec_constants.py  - host-only, no serial, <1 s. Compares the
#       REQUIREMENTS document against the SOURCE constants. Catches the
#       regression class that shipped twice in two days (ARM window 120 s
#       vs R02's 30 s; baro *39/100 vs datasheet 1/128).
#   test_s07_sensor_liveness.py - device test. Arms and asserts the sensor
#       producer is actually producing (rv/lacc/baro alive). Catches
#       "armed, orange LED, no acquisition" in ~8 s instead of after a lost
#       field test.
$hostOnlyTests = @(
  "test_spec_constants.py"
)

$smokeTests = @(
  "test_sensor_injection.py",
  "test_flash.py",
  "test_s04_bhy2_rate.py",
  "test_s07_sensor_liveness.py"
)

$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
Write-Host "=== SGC smoke run: $runId (port $Port) ==="

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

# Host-only gates first: they need no serial port and no hardware, so they
# should fail the run before we ever touch a device.
foreach ($t in $hostOnlyTests) {
  Write-Host "--- $t (host-only) ---"
  py $t
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "FAILED: $t (exit $LASTEXITCODE)"
    $failed = $true
  }
}

foreach ($t in $smokeTests) {
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
  Write-Host "=== SMOKE FAILED ($runId) ==="
  exit 1
}
Write-Host "=== SMOKE PASSED ($runId) ==="
exit 0
