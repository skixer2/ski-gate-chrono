# SGC CI Strategy

## Phase 1: Cron Script (Week 1)

**Setup:** PowerShell scheduled task on JP's Windows machine (where the Nicla is physically connected).

```
# Run every 4 hours, log to timestamped file
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logDir = "F:\Projects\sgc_test_logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

Set-Location "F:\Projects\...\unit_tests"

$testFiles = @(
    "test_state_machine.py", "test_start_detector.py",
    "test_end_detector.py", "test_ring_buffer.py",
    "test_flash.py", "test_bit_packer.py",
    "test_sensor_injection.py", "test_edge_cases.py"
)

$allPassed = $true
foreach ($tf in $testFiles) {
    python sgc_test_harness.py --port COM8 -o "..\..\..\..\..\results_all.md" $tf
    if ($LASTEXITCODE -ne 0) { $allPassed = $false }
}

if (-not $allPassed) {
    # Optional: send alert via email or other notification
    Write-Host "SOME TESTS FAILED — check results_all.md"
}
```

## Phase 2: Self-Hosted GitHub Actions Runner (Week 2)

**Prerequisites:**
- Windows PC with Nicla permanently connected via USB
- GitHub Actions self-hosted runner installed on that PC

```yaml
# .github/workflows/sgc-firmware-test.yml
name: SGC Firmware Test

on:
  push:
    branches: [main]
  schedule:
    - cron: '0 */4 * * *'  # every 4 hours
  workflow_dispatch:        # manual trigger

jobs:
  test:
    runs-on: [self-hosted, windows, sgc-nicla]
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: pip install pyserial
      - name: Run all unit tests
        working-directory: unit_tests
        run: |
          $failed = 0
          Get-ChildItem test_*.py | ForEach-Object {
            python sgc_test_harness.py --port COM8 -o results_all.md $_.Name
            if ($LASTEXITCODE -ne 0) { $failed++ }
          }
          if ($failed -gt 0) { exit 1 }
      - name: Upload results
        uses: actions/upload-artifact@v4
        with:
          name: test-results
          path: unit_tests/results_all.md
```

## Phase 3: Hardware Test Jig (Month 2+)

For performance tests (P01-P03) and hardware-dependent tests (H01-H10, I01-I03):

**Minimal jig:**
- Raspberry Pi or ESP32 acting as a "sensor emulator" — generates controlled I²C traffic
- Programmable power supply for brownout tests
- Scripted BLE client (bleak on Pi) for concurrent BLE stress

**Full jig (production):**
- Pressure chamber for altitude simulation
- Motorized arm for inertial tests
- Environmental chamber for temperature cycles (-20°C to +40°C)
- BLE sniffer for throughput verification

## Pre-Commit Hook (Optional)

```bash
#!/bin/bash
# .git/hooks/pre-commit
# Requires Nicla connected — skip on machines without hardware
if [ -c /dev/ttyACM0 ]; then
    cd unit_tests
    for f in test_*.py; do
        python sgc_test_harness.py --port /dev/ttyACM0 "$f" || exit 1
    done
fi
```

## What We Can't CI (Yet)

| Test | Reason | Workaround |
|------|--------|------------|
| Oscilloscope timing (P01-P03) | Needs physical scope | Manual before release |
| LDC1612 proximity (F03, F13) | Needs copper target + coil | Bench test jig |
| BLE file transfer (I03) | Needs bleak + BLE radio | Phase 2 runner has BLE |
| Qi charging (H10, I09) | Needs Qi pad | Manual |
| Environmental (H01-H03) | Needs chamber | Manual before release |
