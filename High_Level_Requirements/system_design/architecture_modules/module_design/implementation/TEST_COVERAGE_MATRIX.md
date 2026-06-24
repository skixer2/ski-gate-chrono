# SGC — Test Coverage Matrix (v1.0)

*2026-06-24 — Complete requirement-to-test mapping for all v1 requirements.*
*Covers: Device Functional (F01-F51), Phone Functional (F15-F57.4), Performance (P01-P08), Hardware (H01-H13), Interface (I01-I11), Robustness (R01-R08). v2-only requirements (F52-F61, P09, H11-H12, I10) are excluded.*

---

## Legend

| Column | Values |
|--------|--------|
| **Method** | 🤖 Automated (script) · 🔧 Manual (bench) · 👁️ Manual (visual) · ☁️ Cloud (live server) · ⏳ Not yet · ⚠️ v2 only |
| **Hardware** | None · Nicla ×1 · Nicla ×2 · Phone · PC · Oscilloscope · Pressure chamber · Environmental chamber · BLE sniffer · Qi pad · Programmable PSU · Shock table |
| **Peripherals** | Accel (BHI260AP accelerometer) · Gyro (BHI260AP gyroscope) · Mag (BMM150 magnetometer) · Baro (BMP390 barometer) · Inductive (LDC1612) · LED (IS31FL3194 / SK6812) · Beeper (PWM transducer) · Flash (MX25R1635F SPI) · BLE (nRF52832 radio) · Qi coil · UWB footprint · RFID footprint |
| **Status** | ✅ Done · ⬜ Planned · 🔧 Manual only · ⚠️ v2 · ⏳ Not yet implemented |

---

## 1. REQ-FUNC — Device Functional Requirements

| ID | Requirement | Method | Hardware | Peripherals | Test Ref | Status |
|----|-------------|--------|----------|-------------|----------|--------|
| **F01** | 100 Hz sensor acquisition ± 1% | 🤖 + 🔧 | Nicla + Oscilloscope | Accel, Gyro, Mag | U04, U14, S01, A01 | ✅ |
| **F02** | 5 s rolling ring buffer (500 samples) | 🤖 | Nicla | Accel | U09, U10, U11, S01, A01 | ✅ |
| **F03** | Arm via 1000 ms inductive proximity | 🔧 | Nicla ×2 | Inductive (LDC1612) | Bench: copper disc approach | 🔧 |
| **F04** | Dual-mode barometric start (speed > 1.5 m/s OR drop > 2.0 m) | 🤖 + 🔧 | Nicla + Pressure chamber | Baro (BMP390) | U04, U05, U06, S01, A02 | ✅ |
| **F05** | Drain ring buffer 2 samples/cycle, 500 in 2.5 s | 🤖 + 🔧 | Nicla + Oscilloscope | Flash, Accel | U13, S01, A02 | ✅ |
| **F06** | Auto-terminate after 10s flatline + stillness | 🤖 | Nicla | Baro, Accel | U07, U08, S01, A02 | ✅ |
| **F07** | Adaptive bit-packing, ≥ 35% compression | 🤖 + 🔧 | Nicla | Flash | U14, U15, S01, A03 | ✅ |
| **F08** | Circular Flash buffer, auto-overwrite | 🤖 + 🔧 | Nicla | Flash, BLE | U13, U12, I01, S01, A03 | ✅ |
| **F09** | Run metadata via BLE GATT | 🤖 | Nicla + PC (BLE) | BLE | I01 | ✅ |
| **F10** | Run file transfer via BLE + CRC32 | 🤖 | Nicla + PC (BLE) | BLE, Flash | I03 | ✅ |
| **F11** | Config R/W via BLE: name, arm side, discipline | 🤖 + 🔧 | Nicla + PC (BLE) | BLE | I02 (read), 🔧 (write+reboot) | ⬜ |
| **F12** | Sleep after 5 min inactivity | 🤖 + 🔧 | Nicla | (all idle) | U01 (forced), 🔧 (natural timeout) | ✅ |
| **F13** | Wake from sleep via LDC1612 INTB | 🔧 | Nicla ×2 | Inductive (LDC1612) | Bench: forearms together | 🔧 |
| **F14** | Audible beeper when armed | 👁️ | Nicla | Beeper (PWM) | Arm → listen | 🔧 |
| **F37** | Time sync from phone on every connect | 🤖 | Nicla + Phone | BLE | I01 (BLE time write, serial echo) | ✅ |
| **F38** | BLE MTU ≥ 247 | 🔧 | Nicla + BLE sniffer | BLE | Sniffer: verify MTU exchange | 🔧 |
| **F39** | BLE bonding (LE Secure Connections) | 🔧 | Nicla + Phone | BLE | Manual: pair/unpair/reconnect | 🔧 |
| **F40** | BLE OTA firmware update | ⏳ | Nicla + Phone | BLE, Flash | DFU flow not yet tested | ⏳ |
| **F41** | RGB LED visual status (onboard IS31FL3194) | 👁️ | Nicla | LED (IS31FL3194) | Visual: verify patterns per state | 🔧 |
| **F42** | Factory reset via 20s inductive hold | 🤖 + 🔧 | Nicla | Inductive, Flash | S02 (serial R command), 🔧 (inductive hold) | ✅ |
| **F52** | ⚠️ v2 ONLY — UHF RFID footprint | — | — | — | — | ⚠️ |
| **F53** | ⚠️ v2 ONLY — RFID inventory rounds | — | — | — | — | ⚠️ |
| **F54** | ⚠️ v2 ONLY — RSSI nearest-tag | — | — | — | — | ⚠️ |
| **F55** | ⚠️ v2 ONLY — Gate events logged | — | — | — | — | ⚠️ |
| **F56** | ⚠️ v2 ONLY — RFID doesn't impact 100 Hz | — | — | — | — | ⚠️ |
| **F57** | ⚠️ v2 ONLY — SETUP mode | — | — | — | — | ⚠️ |

---

## 2. REQ-PHONE — Phone Application Functional Requirements

| ID | Requirement | Method | Hardware | Peripherals | Test Ref | Status |
|----|-------------|--------|----------|-------------|----------|--------|
| **F15** | Pair left/right arms by RTC timestamp ±3s | 🤖 | None (models), Nicla ×2 (integration) | — | `run_test.dart` (models), 📱 ADB (integration) | ⚠️ Partial |
| **F16** | Cross-correlation alignment < 10 ms | 🤖 | None | — | `cross_correlator_test.dart` (8 tests) | ✅ |
| **F17** | Decompress and display run data | 🤖 | None | — | `decompressor_test.dart` (14 tests) | ✅ |
| **F18** | Detect pole/gate impacts from accel spikes | 🤖 | None | — | `impact_detector_test.dart` (8 tests) | ✅ |
| **F19** | Gate timestamps: R-pole left-aligned, L-pole right-aligned | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F20** | User profile parameters R/W | 🤖 | None (storage test) | — | 📱 Storage test | ⬜ |
| **F21** | Read/modify device params via BLE | 🤖 | Phone + Nicla | BLE | 📱 Mock BLE + ADB integration | ⬜ |
| **F22** | Compare two runs side-by-side | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F23** | Push only timestamps + barometric data to cloud | ☁️ | Phone + Cloud server | BLE | ☁️ End-to-end | ⬜ |
| **F24** | Retrieve cloud endpoint from bootstrap URL | ☁️ | Phone + Cloud server | — | ☁️ Bootstrap redirect test | ⬜ |
| **F25** | Trainer views group athletes' gate times | ☁️ | Phone + Cloud server | — | ☁️ Auth + query test | ⬜ |
| **F26** | Kinematics-driven missed gate estimation | 🤖 | None | — | `gate_time_estimator_test.dart` (9 tests), `vec3_test.dart` (14 tests) | ✅ |
| **F27** | Guessed times displayed with `*` | 🤖 | None (widget test) | — | 📱 Widget test (asterisk rendering) | ⬜ |
| **F28** | Banana detection (same-side consecutive gates) | 🤖 | None | — | `banana_detector_test.dart` (not yet implemented) | ⏳ |
| **F29** | Altitude + gate timestamp layout | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F30** | Vertical speed from barometric 10 Hz decimation | 🤖 | None | — | `barometric_speed_test.dart` (not yet implemented) | ⏳ |
| **F31** | Vertical speed graph + gate markers | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F32** | Compare speed graphs between two runs | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F33** | Barometric altitude graph + gate markers | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F34** | Compare altitude graphs between two runs | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F35** | Group-level default visibility | ☁️ | Phone + Cloud server | — | ☁️ Auth + visibility test | ⬜ |
| **F36** | Trainer override per-run visibility | ☁️ | Phone + Cloud server | — | ☁️ Auth + visibility test | ⬜ |
| **F43** | Offline upload queue | 🤖 + ☁️ | Phone + Cloud server | — | 📱 Storage test (queue), ☁️ (sync) | ⬜ |
| **F44** | Gate numbers in timestamp table | 🤖 | None (model + widget) | — | `run_test.dart` (model), 📱 widget | ⚠️ Partial |
| **F45** | User-editable run name/label | 🤖 | None (storage test) | — | 📱 Storage test | ⬜ |
| **F46** | Trainer deletes runs (device + local + cloud) | 🤖 + ☁️ | Phone + Nicla + Cloud | — | 📱 Storage + ☁️ end-to-end | ⬜ |
| **F47** | Export runs as CSV + JSON | 🤖 | None | — | 📱 Export unit test | ⬜ |
| **F48** | Browse cloud runs for comparison | ☁️ | Phone + Cloud server | — | ☁️ End-to-end | ⬜ |
| **F49** | Single-arm mode gate classification | 🤖 | None | — | 📱 Dart unit test | ⬜ |
| **F50** | GDPR deletion with warning | 🤖 + ☁️ | Phone + Cloud server | — | 📱 Widget (warning dialog), ☁️ (deletion) | ⬜ |
| **F51** | Display calibration status + prevent arming if < 2 | 🤖 | None (mock BLE) | — | 📱 Mock BLE test | ⬜ |
| **F57.1** | Course setup Mode A (sequential recording) | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F57.2** | Course setup Mode B (update existing) | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F57.3** | Dual course view (map ↔ list) | 🤖 | None (widget test) | — | 📱 Widget test | ⬜ |
| **F57.4** | Delta-based course map format | 🤖 | None | — | 📱 Dart unit test | ⬜ |
| **F58** | ⚠️ v2 ONLY — Tracing mode | — | — | — | — | ⚠️ |
| **F59** | ⚠️ v2 ONLY — Duplicate tag detection | — | — | — | — | ⚠️ |
| **F60** | ⚠️ v2 ONLY — Cloud course persistence | — | — | — | — | ⚠️ |
| **F61** | ⚠️ v2 ONLY — Spatial correlation | — | — | — | — | ⚠️ |

---

## 3. REQ-PERF — Performance Requirements

| ID | Requirement | Method | Hardware | Peripherals | Test Ref | Status |
|----|-------------|--------|----------|-------------|----------|--------|
| **P01** | 100 Hz loop jitter < 500 µs σ | 🔧 | Nicla + Oscilloscope | Accel | Scope: GPIO toggle per loop iteration | 🔧 |
| **P02** | Sensor → ring buffer < 2 ms | 🔧 | Nicla + Oscilloscope | Accel, Gyro | Scope: BHI read start → ring write end | 🔧 |
| **P03** | Start detection latency < 100 ms | 🔧 | Nicla + Pressure chamber + Oscilloscope | Baro | Chamber: descent onset → LOGGING transition | 🔧 |
| **P04** | BLE file transfer ≥ 20 KB/s | 🤖 + 🔧 | Nicla + BLE sniffer | BLE, Flash | I03 (throughput measurement) | 🔧 |
| **P05** | Quaternion accuracy ≥ 2 before logging | 🔧 | Nicla | Accel, Gyro, Mag | Figure-8 calibration + BLE read | 🔧 |
| **P06** | Cross-correlation T=0 < 10 ms | 🤖 | None | — | `cross_correlator_test.dart` (offset injection) | ✅ |
| **P07** | Compression ≥ 35% | 🔧 | Nicla | Flash | Decode stored file, count bytes vs frames | 🔧 |
| **P08** | BLE 2M PHY, MTU ≥ 247 | 🔧 | Nicla + BLE sniffer | BLE | Sniffer: PHY + MTU verification | 🔧 |
| **P09** | ⚠️ v2 ONLY — RFID round < 15 ms | — | — | — | — | ⚠️ |

---

## 4. REQ-HW — Hardware & Environmental Requirements

| ID | Requirement | Method | Hardware | Peripherals | Test Ref | Status |
|----|-------------|--------|----------|-------------|----------|--------|
| **H01** | −20°C to +40°C operating | 🔧 | Nicla + Environmental chamber | Accel, Gyro, Baro, BLE, Flash | Chamber: functional verification at temp extremes | 🔧 |
| **H02** | ≥ 8 hours battery at −10°C | 🔧 | Nicla + Environmental chamber + PSU | (all running) | Cold chamber + battery logger | 🔧 |
| **H03** | IP67 enclosure | 🔧 | Nicla (enclosed) | (all) + enclosure | IP67 certification lab | 🔧 |
| **H04** | Inductive trigger through polycarbonate shell | 🔧 | Nicla ×2 (enclosed) | Inductive (LDC1612) | Bench: copper disc approach through enclosure | 🔧 |
| **H05** | Module thickness < 16 mm | 🔧 | Nicla + caliper | — | Caliper measurement | 🔧 |
| **H06** | Weight ≤ 40 g (prototype) | 🔧 | Nicla + scale | — | Scale | 🔧 |
| **H07** | ≥ 10 runs storage before sync | 🔧 | Nicla | Flash | Fill flash with runs, verify count | 🔧 |
| **H08** | No ferromagnetic near BMM150 | 🔧 | Nicla + Qi coil + transducer | Mag (BMM150), Qi coil, Beeper | Calibration with Qi/transducer powered | 🔧 |
| **H09** | 200g shock survival | 🔧 | Nicla + Shock table | Accel, Gyro | Shock test + post-impact functional check | 🔧 |
| **H10** | Qi wireless charging | 🔧 | Nicla + Qi pad | Qi coil | Charge 0→100%, verify LED | 🔧 |
| **H11** | ⚠️ v2 ONLY — UHF RFID footprint | — | — | — | — | ⚠️ |
| **H12** | ⚠️ v2 ONLY — RFID/BMM150 non-interference | — | — | — | — | ⚠️ |
| **H13** | DW3000 UWB footprint unpopulated (no interference) | 👁️ | Custom PCB | Mag (BMM150), UWB footprint | Visual: PCB inspection; verify BMM150 stable | 🔧 |

---

## 5. REQ-IF — Interface Requirements

| ID | Requirement | Method | Hardware | Peripherals | Test Ref | Status |
|----|-------------|--------|----------|-------------|----------|--------|
| **I01** | BHI260AP ↔ nRF52832 I²C | 🤖 + 🔧 | Nicla + I²C analyzer | Accel, Gyro, Mag | U04-U15 (exercises sensor), 🔧 (scope) | ✅ |
| **I02** | BMP390 ↔ nRF52832 I²C | 🤖 + 🔧 | Nicla + I²C analyzer | Baro (BMP390) | U04-U08 (exercises baro), 🔧 (scope) | ✅ |
| **I03** | LDC1612 ↔ nRF52832 I²C + INTB | 🔧 | Nicla + I²C analyzer + Oscilloscope | Inductive (LDC1612) | Bench: scope on INTB for wake | 🔧 |
| **I04** | SPI Flash ↔ nRF52832 SPI | 🤖 | Nicla | Flash (MX25R1635F) | U12 (flash self-test: read/write/erase) | ✅ |
| **I05** | Device ↔ Phone BLE 5.0 + GATT + DFU | 🤖 | Nicla + PC (BLE) | BLE | I01 (connect + chars), I03 (file transfer) | ✅ |
| **I06** | Left ↔ Right cross-arm proximity (passive inductive) | 🔧 | Nicla ×2 | Inductive (LDC1612 ×2) | Bench: forearms together, verify each arms | 🔧 |
| **I07** | Phone ↔ Cloud HTTPS | ☁️ | Phone + Cloud server | — | ☁️ API endpoint test | ⬜ |
| **I08** | Beeper ↔ nRF52832 GPIO PWM | 🔧 | Nicla + Oscilloscope | Beeper (PWM) | Scope on P0.09 during arming | 🔧 |
| **I09** | Qi ↔ Battery charger | 🔧 | Nicla + Qi pad | Qi coil | Bench: Qi pad → charge current | 🔧 |
| **I10** | ⚠️ v2 ONLY — RFID ↔ nRF52832 SPI | — | — | — | — | ⚠️ |
| **I11** | DW3000 UWB footprint (not populated, power-gated) | 👁️ | Custom PCB | UWB footprint | Visual: CSn never asserted, VDD off | 🔧 |

---

## 6. REQ-ROB — Robustness & Edge Cases

| ID | Requirement | Method | Hardware | Peripherals | Test Ref | Status |
|----|-------------|--------|----------|-------------|----------|--------|
| **R01** | False arm reject (< 500 ms) | 🔧 | Nicla | Inductive (LDC1612) | Serial pulse < 500ms, verify no arm | 🔧 |
| **R02** | Aborted start timeout (30s) | 🔧 | Nicla | Baro (BMP390) | Arm, wait 30s, verify → IDLE | 🔧 |
| **R03** | Mid-run inductive trigger ignored | 🔧 | Nicla | Inductive (LDC1612), Baro | Trigger LDC1612 during LOGGING, verify ignored | 🔧 |
| **R04** | Low-battery shutdown < 3.3V | 🔧 | Nicla + Programmable PSU | Flash, BLE | Simulate VBAT drop, verify file closed | 🔧 |
| **R05** | Flash CRC mismatch → mark corrupt | 🤖 | Nicla | Flash | I03 (inject bit errors, verify skip) | ✅ |
| **R06** | BLE disconnect mid-transfer resume | 🤖 | Nicla + PC (BLE) | BLE, Flash | I03 (disconnect during chunk stream, reconnect) | ✅ |
| **R07** | Sensor failure detection | 🔧 | Nicla | Accel, Gyro | Disconnect BHI260AP, verify BLE error flag | 🔧 |
| **R08** | Single-arm operation accepted | 🤖 | None (Dart test) | — | 📱 Dart unit test | ⬜ |

---

## 7. Summary Statistics

### 7.1 By Method

| Method | Count | % of v1 |
|--------|-------|---------|
| 🤖 Automated (script) | 37 | 43% |
| 🔧 Manual (bench, scope, chamber) | 29 | 34% |
| 👁️ Manual (visual inspection) | 4 | 5% |
| ☁️ Cloud (live server) | 10 | 12% |
| ⏳ Not yet implemented | 6 | 7% |
| ⚠️ v2 only | 14 | — |
| **TOTAL v1** | **86** | **100%** |

### 7.2 By Hardware

| Hardware | Requirements |
|----------|-------------|
| **None** (pure software/algorithmic) | 22 |
| **Nicla ×1** (single device) | 41 |
| **Nicla ×2** (dual device, cross-arm) | 6 |
| **Phone** (Android, ADB) | 14 |
| **PC** (test runner, serial, BLE) | 15 |
| **Oscilloscope** | 6 |
| **Pressure chamber** | 2 |
| **Environmental chamber** | 2 |
| **BLE sniffer** | 3 |
| **Qi pad** | 2 |
| **Programmable PSU** | 2 |
| **Shock table** | 1 |
| **I²C analyzer** | 3 |
| **Cloud server** | 10 |

### 7.3 By Peripheral

| Peripheral | Requirements |
|------------|-------------|
| **Accelerometer + Gyro** (BHI260AP) | 12 |
| **Magnetometer** (BMM150) | 4 |
| **Barometer** (BMP390) | 8 |
| **Inductive** (LDC1612) | 8 |
| **LED** (IS31FL3194 / SK6812) | 2 |
| **Beeper** (PWM transducer) | 3 |
| **Flash** (MX25R1635F SPI) | 14 |
| **BLE** (nRF52832 radio) | 12 |
| **Qi coil** | 3 |
| **UWB footprint** | 2 |
| **RFID footprint** | ⚠️ v2 only |

### 7.4 By Status

| Status | Count | % of v1 |
|--------|-------|---------|
| ✅ Done | 24 | 28% |
| ⚠️ Partial (models tested, integration pending) | 3 | 3% |
| ⬜ Planned (Phase 2/3) | 27 | 31% |
| 🔧 Manual only (hardware-dependent) | 26 | 30% |
| ⏳ Not yet implemented | 6 | 7% |
| **TOTAL v1** | **86** | **100%** |

---

## 8. Per-Phase Coverage

| Phase | Requirements | Status |
|-------|-------------|--------|
| Phase 0–4 (Firmware Unit + Integration) | F01-F08, F12, F37, I04-I05, R05-R06 | ✅ 15/15 |
| Phase 5 (Phone Processing — Phase 1) | F16, F17, F18, F26 | ✅ 4/4 |
| Phase 6 (Phone Widget + Mock BLE — Phase 2) | F19-F22, F27-F34, F44-F45, F47, F49, F51, F57.1-F57.4 | ⬜ 22/22 |
| Phase 7 (Cloud — Phase 2+) | F23-F25, F35-F36, F43, F46, F48, F50, I07 | ⬜ 10/10 |
| Phase 8 (End-to-End — Phase 3) | F10, F11, F15, F21, F37, F39, F40, F46, F50, I05, R06, R08 | ⬜ 7/12 (5 already covered) |
| Phase 9 (Hardware + Environmental) | F03, F13-F14, F38, F41-F42, P01-P05, P07-P08, H01-H10, H13, I01-I03, I06, I08-I09, I11, R01-R04, R07 | 🔧 30/30 |

---

*Referenced from: [MASTER_TEST_PLAN.md](MASTER_TEST_PLAN.md) (top-level strategy), [unit_tests/device/requirements_traceability.md](../module_design/unit_tests/device/requirements_traceability.md) (device RTM), [unit_tests/phone/TEST_SPEC.md](../module_design/unit_tests/phone/TEST_SPEC.md) (phone test spec).*
