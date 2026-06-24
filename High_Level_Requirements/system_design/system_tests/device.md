# System Tests — Device (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

System tests verify the device's end-to-end behavior as a complete system.
All modules are integrated; only the external interface (serial/BLE) is exercised.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../architecture_modules/module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../architecture_modules/module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** `phone.md`

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Status |
|----|----------|-------------|--------|----------|--------|
| **S01** | Full run pipeline: ARM → LOGGING → POST_RUN | F01-F08 | 🤖 Serial harness | Nicla ×1 | ✅ |
| **S02** | Factory reset + state preservation | F42 | 🤖 Serial harness | Nicla ×1 | ✅ |

### S01 — Full Run Pipeline

**Objective:** Verify the complete state machine, data acquisition, compression,
storage, and retrieval pipeline works end-to-end.

**Procedure:**
1. Power-on → verify BLE advertising + sensor streaming
2. Send `a` (arm) → verify ARMED, ring buffer filling (F02)
3. Inject synthetic barometric descent → verify LOGGING transition (F04)
4. After 500 frames of log, inject flatline + stillness → verify POST_RUN (F06)
5. Verify run appears in list (`l` command) with correct metadata (F09)
6. Verify bit-packing via file inspection (F07)
7. Verify ring buffer drain completed in 2.5 s (F05)
8. Verify 100 Hz sensor data throughout (F01)

**Automated:** `sgc_test_harness.py test_state_machine.py` → exercises S01

### S02 — Factory Reset

**Objective:** Verify factory reset clears all persistent state.

**Procedure:**
1. Store multiple runs on device
2. Send `R` (factory reset via serial) → verify LED confirmation
3. Reconnect → verify BLE bonding lost
4. Verify device name reverted to default
5. Verify run count = 0
6. Verify Flash is erased

**Automated:** `sgc_test_harness.py test_flash.py` → exercises S02

---

## Hardware-Dependent Extension (Phase 9)

| ID | Extension | Requirements | Hardware |
|----|-----------|-------------|----------|
| S01-HW | Full run with real sensor data (not synthetic injection) | F01-F08 | Nicla + Pressure chamber |
| S02-HW | Factory reset via 20s inductive hold | F42 | Nicla ×2 (LDC1612) |

---

## Pass Criteria

| Scenario | Pass Condition |
|----------|---------------|
| S01 | Valid compressed run file produced, all data integrity checks pass |
| S02 | All state cleared: runs, bonding, name. Device returns to factory defaults |
