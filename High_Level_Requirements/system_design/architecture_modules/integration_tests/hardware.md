# Integration Tests — Hardware (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

Hardware integration tests verify that physical peripherals interact correctly
with the nRF52832 SoC: sensor I²C buses, SPI Flash, BLE radio, PWM, Qi charging.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** `device.md` · `phone.md`

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Peripherals | Status |
|----|----------|-------------|--------|----------|-------------|--------|
| **HI01** | BHI260AP I²C sensor streaming | I01, F01, P05 | 🔧 Bus analyzer | Nicla + I²C analyzer | Accel, Gyro, Mag | ✅ |
| **HI02** | BMP390 I²C barometric streaming | I02, F04, F30 | 🔧 Bus analyzer | Nicla + I²C analyzer | Baro | ✅ |
| **HI03** | LDC1612 proximity + INTB wake | I03, F03, F13 | 🔧 Scope + bus analyzer | Nicla ×2 + Scope | Inductive | 🔧 |
| **HI04** | SPI Flash read/write/erase | I04, F07, F08, R05 | 🤖 Serial | Nicla | Flash | ✅ |
| **HI05** | BLE 2M PHY + MTU negotiation | I05, F38, P04, P08 | 🔧 BLE sniffer | Nicla + Sniffer | BLE | 🔧 |
| **HI06** | Cross-arm inductive proximity | I06, F03 | 🔧 Bench | Nicla ×2 | Inductive (×2) | 🔧 |
| **HI07** | Beeper PWM output | I08, F14 | 🔧 Scope | Nicla + Scope | Beeper | 🔧 |
| **HI08** | Qi charging | I09, H10 | 🔧 Bench | Nicla + Qi pad | Qi coil | 🔧 |
| **HI09** | LED I²C driver (IS31FL3194) | F41 | 👁️ Visual | Nicla | LED | 🔧 |
| **HI10** | BMM150 magnetometer stability | H08 | 🔧 Bench | Nicla + Qi coil + Transducer | Mag, Qi, Beeper | 🔧 |
| **HI11** | DW3000 UWB footprint (unpopulated) | I11, H13 | 👁️ Visual | Custom PCB | UWB footprint, Mag | 🔧 |

### HI01 — BHI260AP I²C Streaming

**Objective:** Verify Bosch sensor hub delivers fused data at 100 Hz.

**Procedure:**
1. Connect I²C bus analyzer to SDA/SCL
2. Verify FIFO watermark interrupt at 10-sample threshold
3. Verify 100 Hz ± 1% sample rate (F01, P01)
4. Perform figure-8 calibration → verify accuracy field ≥ 2 (P05)
5. Verify quaternion magnitude 0.8 < |q| < 1.2 (sensor readiness check)

**Status:** ✅ Sensor streaming verified in firmware phases 2-7. 🔧 Bus analyzer for definitive timing.

### HI02 — BMP390 Barometric Streaming

**Objective:** Verify pressure sensor delivers 100 Hz data.

**Procedure:**
1. Connect I²C bus analyzer
2. Verify pressure reads at 100 Hz (I02)
3. Verify altitude computation from pressure
4. Test in pressure chamber: known ΔP → verify altitude accuracy

**Status:** ✅ Sensor verified in firmware. 🔧 Chamber for definitive accuracy.

### HI03 — LDC1612 Proximity + INTB Wake

**Objective:** Verify inductive sensor detects cross-arm approach and wakes CPU.

**Procedure:**
1. Connect scope to INTB pin
2. Bring copper target disc within ~30 mm → verify threshold crossing
3. Verify INTB asserts → nRF52 GPIO interrupt triggers
4. Verify wake-from-sleep within < 100 µs (F13)
5. Verify 1000 ms continuous hold required for arming (F03, R01)

**Status:** 🔧 Requires LDC1612 hardware + scope

### HI04 — SPI Flash Read/Write/Erase

**Objective:** Verify MX25R1635F Flash operations.

**Procedure:**
1. Erase sector → verify all 0xFF
2. Write 256 B → read back → verify match
3. Write across sector boundary → verify no corruption
4. Verify CRC32 computation matches known value
5. Inject bit errors → verify CRC mismatch → run skipped (R05)

**Status:** ✅ U12 (flash self-test) covers all operations

### HI05 — BLE 2M PHY + MTU

**Objective:** Verify BLE radio performance.

**Procedure:**
1. Connect BLE sniffer
2. Verify LE 2M PHY negotiated (P08)
3. Verify MTU exchange → ≥ 247 bytes (F38)
4. Measure file transfer throughput → ≥ 20 KB/s (P04)

**Status:** 🔧 BLE sniffer required

### HI06 — Cross-Arm Inductive Proximity

**Objective:** Verify each arm's LDC1612 detects the opposite arm's copper disc.

**Procedure:**
1. Position two devices with forearms-together distance
2. Verify left LDC1612 detects right arm's copper disc
3. Verify right LDC1612 detects left arm's copper disc
4. Verify each arms independently (no BLE handshake required)
5. Verify one arm failing doesn't block the other

**Status:** 🔧 Requires 2× Nicla with LDC1612 + copper target discs

### HI07 — Beeper PWM

**Objective:** Verify surface transducer produces audible signal.

**Procedure:**
1. Connect scope to P0.09 during arming
2. Verify PWM frequency + duty cycle
3. Audible test: arm device, listen for beep through enclosure wall

**Status:** 🔧 Scope + audible verification

### HI08 — Qi Charging

**Objective:** Verify wireless charging through enclosure.

**Procedure:**
1. Place device on Qi pad
2. Verify charging LED lights (F41)
3. Measure charge current
4. Charge 0 → 100% → verify time < 3 hours (H10)
5. Verify BMM150 calibration unaffected by Qi coil (H08)

**Status:** 🔧 Qi pad + multimeter required

### HI09 — LED Driver (IS31FL3194)

**Objective:** Verify onboard RGB LED status patterns.

**Procedure:**
1. Visual: off = SLEEP
2. Visual: blue breathing = BLE advertising
3. Visual: green = armed (F41)
4. Visual: red = logging (F41)
5. Visual: yellow blink = low battery (F41)

**Status:** 👁️ Visual inspection

### HI10 — BMM150 Stability

**Objective:** Verify magnetometer is not interfered with by nearby components.

**Procedure:**
1. Calibrate BMM150 with all peripherals off
2. Enable Qi coil + surface transducer
3. Verify calibration accuracy unchanged (H08)
4. Verify accuracy ≥ 2 maintained

**Status:** 🔧 Bench test with Qi pad + transducer active

### HI11 — DW3000 UWB Footprint

**Objective:** Verify unpopulated UWB footprint doesn't interfere.

**Procedure:**
1. Visual PCB inspection: verify QFN footprint, SPI traces, CSn pad
2. Scope: verify CSn never asserted, VDD_UWB = 0V
3. Verify BMM150 calibration stable with footprint present (H13)
4. Verify no shorts or leakage on UWB power rail

**Status:** 🔧 Requires custom PCB (currently Nicla prototype)
