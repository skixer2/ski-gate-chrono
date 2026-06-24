# Integration Tests — Device (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

Integration tests verify that two or more device modules interact correctly:
BLE ↔ Flash, Flash ↔ Ring Buffer, BLE ↔ Serial, etc.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** `phone.md` · `hardware.md`

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Peripherals | Status |
|----|----------|-------------|--------|----------|-------------|--------|
| **I01** | BLE connection + GATT service read | F09, F37, F38, I05 | 🤖 Serial + BLE | Nicla + PC | BLE, Flash | ✅ |
| **I02** | BLE config R/W (device params) | F11 | 🤖 + 🔧 | Nicla + PC | BLE | ⬜ |
| **I03** | BLE file transfer + CRC32 | F10, R05, R06 | 🤖 Serial + BLE | Nicla + PC | BLE, Flash | ✅ |
| **I04** | Flash + Ring Buffer integration | F05, F07, F08 | 🤖 Serial | Nicla | Flash, Accel | ✅ |
| **I05** | Sensor → Ring → Flash pipeline | F01, F02, F05, F07 | 🤖 Serial | Nicla | Accel, Baro, Flash | ✅ |

### I01 — BLE Connection + GATT Read

**Objective:** Verify phone can connect, read GATT characteristics.

**Procedure:**
1. BLE scan → discover SGC device
2. Connect → verify bonding (first time) or encryption (subsequent)
3. Write UTC time to `ABC0` → verify serial echo confirms (F37)
4. Read run count from `ABC6` → verify matches serial `l` command (F09)
5. Verify MTU ≥ 247 (F38)

**Automated:** Test harness BLE client (`sgc_test_harness.py`)

### I02 — BLE Config R/W

**Objective:** Verify device parameters can be read and written via BLE.

**Procedure:**
1. Read device name → verify matches expected
2. Write new device name via BLE → verify serial echo
3. Read back → verify new name persisted
4. Set arm side (left/right) → verify via `ABC2`
5. Reboot device → verify config persisted

**Status:** ⬜ Read path tested, write+persist pending

### I03 — BLE File Transfer + CRC32

**Objective:** Verify complete file transfer with integrity check.

**Procedure:**
1. Request run download by run ID (F10)
2. Receive chunked data via `ABCB` characteristics
3. Verify CRC32 from `ABCC` matches computed CRC (F10)
4. Inject bit error in chunk → verify CRC mismatch → run rejected (R05)
5. Disconnect mid-transfer → reconnect → verify resume from last chunk (R06)

**Automated:** `sgc_test_harness.py` + `sgc_mock_runner.py`

### I04 — Flash + Ring Buffer

**Objective:** Verify ring buffer drain to Flash is correct.

**Procedure:**
1. Fill ring buffer (500 samples) in ARMED
2. Trigger LOGGING → drain 2 samples/cycle
3. After drain complete, verify Flash contents
4. Verify no gaps in timestamps across buffer/Flash boundary

**Automated:** Serial test scripts (U13, U14, U15)

### I05 — Sensor → Ring → Flash Pipeline

**Objective:** Verify data flow from sensors through ring buffer to Flash.

**Procedure:**
1. Enable sensors (BHI260AP + BMP390)
2. Arm → verify ring buffer receiving at 100 Hz (F01)
3. Verify ring buffer depth = 500 (F02)
4. Trigger start → verify drain begins (F05)
5. Verify compressed data written to Flash (F07)
6. Decompress and verify frame continuity

**Automated:** Serial test scripts
