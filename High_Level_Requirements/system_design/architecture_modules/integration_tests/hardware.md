# Integration Tests — Hardware (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

Hardware integration tests verify that physical peripherals interact correctly
with the nRF52832 SoC: sensor I²C buses, SPI Flash, BLE radio, button GPIO, USB-C charging.

> ⚠️ **v6.0 (2026-09-10):** LDC1612 removed from board (inductive era over); beeper DNP; Qi → USB-C; BMM150 unused. HI03/HI06/HI07/HI08/HI10 rewritten accordingly.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** `device.md` · `phone.md`

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Peripherals | Status |
|----|----------|-------------|--------|----------|-------------|--------|
| **HI01** | BHI260AP I²C sensor streaming | I01, F01, P05 | 🔧 Bus analyzer | Nicla + I²C analyzer | Accel, Gyro, Mag | ✅ |
| **HI02** | BMP390 I²C barometric streaming | I02, F04, F30 | 🔧 Bus analyzer | Nicla + I²C analyzer | Baro | ✅ |
| **HI03** | Piezo button: arm + wake (P0.02) | I12, F03, F13 | 🔧 Bench + scope | Bench device + Scope | Button | 🔧 |
| **HI04** | SPI Flash read/write/erase | I04, F07, F08, R05 | 🤖 Serial | Nicla | Flash | ✅ |
| **HI05** | BLE 2M PHY + MTU negotiation | I05, F38, P04, P08 | 🔧 BLE sniffer | Nicla + Sniffer | BLE | 🔧 |
| **HI06** | ~~Cross-arm inductive proximity~~ — **REMOVED** (LDC off board; independent button arm per device) | — | — | — | — | ⛔ |
| **HI07** | ~~Beeper PWM~~ — **DROPPED** (no beeper in v1, DNP footprint — F14) | — | PCB inspection | Custom PCB | — | 👁️ |
| **HI08** | USB-C charging (sealable cap) | I09, H10 | 🔧 Bench | Device + USB-C PSU | USB-C, charger | 🔧 |
| **HI09** | LED I²C driver (IS31FL3194) | F41 | 👁️ Visual | Nicla | LED | 🔧 |
| **HI10** | ~~BMM150 stability~~ — **N/A v1** (magnetometer unused; H08 dropped) | — | — | — | — | ⛔ |
| **HI11** | DW3000 UWB footprint (unpopulated) | I11, H13 | 👁️ Visual | Custom PCB | UWB footprint | 🔧 |

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

### HI03 — Piezo Button: Arm + Wake (P0.02)

**Objective:** Verify sealed button arms the device and wakes it from sleep.

**Procedure:**
1. Scope P0.02: press → clean falling edge, 20 ms debounce effective
2. SLEEP → press → ARMED within 1 s, LED green confirms (F03)
3. Wake latency < 100 ms, no reboot (F13)
4. 5 quick presses within 3 s → factory reset sequence (F42)
5. System Off → press → cold boot (rr:4)

**Status:** 🔧 Bench device has the button — no extra hardware needed

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
3. Verify MTU exchange → request 517 / target ≥ 500 B payload (F38)
4. Measure file transfer throughput → ≥ 20 KB/s (P04)

**Status:** 🔧 BLE sniffer required

### HI06 — REMOVED (v6.0)

Cross-arm inductive proximity abandoned: LDC1612 physically removed from the
board (2026-08-22). Each device arms independently on its own button (F03).
See F03a (proposed, not implemented): one-press-arms-both via advertising flag.

### HI07 — DROPPED (v6.0)

No beeper in v1 (F14): transducer footprint is DNP on the custom PCB, reserved
for a future user-requested variant. Verification = PCB inspection only.

### HI08 — USB-C Charging

**Objective:** Verify charging via sealable USB-C connector.

**Procedure:**
1. Connect USB-C PSU → verify charge status on GATT/LED
2. Measure charge current into battery
3. Charge 0 → 100% → verify completion (H10)
4. Fit tethered cap → verify IP67 sealing maintained

**Status:** 🔧 USB-C PSU + multimeter required

### HI09 — LED Driver (IS31FL3194)

**Objective:** Verify onboard RGB LED status patterns.

**Procedure:**
1. Visual: off = SLEEP
2. Visual: blue breathing = BLE advertising
3. Visual: green = armed (F41)
4. Visual: red = logging (F41)
5. Visual: yellow blink = low battery (F41)

**Status:** 👁️ Visual inspection

### HI10 — N/A (v6.0)

Magnetometer stability test dropped: the BMM150 is unused in the v1 product
(no calibration, no constraint — H08 not applicable). The part remains
physically present on the Nicla module.

### HI11 — DW3000 UWB Footprint

**Objective:** Verify unpopulated UWB footprint doesn't interfere.

**Procedure:**
1. Visual PCB inspection: verify QFN footprint, SPI traces, CSn pad
2. Scope: verify CSn never asserted, VDD_UWB = 0V
3. Verify no functional interference with active peripherals (H13)
4. Verify no shorts or leakage on UWB power rail

**Status:** 🔧 Requires custom PCB (currently Nicla prototype)
