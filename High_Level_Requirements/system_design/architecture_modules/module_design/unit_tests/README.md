# SGC Test Framework

```
unit_tests/          ← Module-level tests (serial, per .cpp module)
  sgc_test_harness.py   ← Shared harness
  test_state_machine.py ← U01-U03
  test_start_detector.py← U04-U06
  test_end_detector.py  ← U07-U08
  test_ring_buffer.py   ← U09-U11
  test_flash.py         ← U12-U13
  test_bit_packer.py    ← U14-U15
  test_sensor_injection.py ← U16-U19

integration_tests/   ← Cross-module tests (BLE required)
  test_ble_service.py    ← I01-I02
  test_file_transfer.py  ← I03

system_tests/        ← End-to-end (full run cycle)
  test_full_run.py       ← S01-S02

acceptance_tests/    ← Requirements validation
  acceptance_tests.py    ← A01-A04
```

## Quick Start

```bash
pip install pyserial

# Single test file
python sgc_test_harness.py --port COM3 test_state_machine.py

# All unit tests
for f in test_*.py; do python sgc_test_harness.py --port COM3 $f; done

# Integration tests (need bleak)
pip install bleak
cd ../integration_tests
python ../module_design/unit_tests/sgc_test_harness.py --port COM3 test_ble_service.py
```

## Test Hierarchy

| Layer | Location | Depends on | What it tests |
|-------|----------|-----------|---------------|
| **Unit** | `unit_tests/` | Serial only | Single module: state machine, ring buffer, flash |
| **Integration** | `integration_tests/` | Serial + BLE | Modules together: BLE service, file transfer |
| **System** | `system_tests/` | Full device | End-to-end run cycle |
| **Acceptance** | `acceptance_tests/` | All above | Requirements traceability |
