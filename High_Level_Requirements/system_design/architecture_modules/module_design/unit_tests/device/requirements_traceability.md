# SGC Requirements Traceability Matrix (v1 — v5.8)

*2026-06-19 — Generated from sgc_requirements.md v5.7 + test framework.*
*2026-06-24 — Cross-referenced with [MASTER_TEST_PLAN.md](../../implementation/MASTER_TEST_PLAN.md) and [TEST_COVERAGE_MATRIX.md](../../implementation/TEST_COVERAGE_MATRIX.md).*

Maps every v1 requirement to the tests that verify it.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../../implementation/MASTER_TEST_PLAN.md) (overall strategy) · [TEST_COVERAGE_MATRIX.md](../../implementation/TEST_COVERAGE_MATRIX.md) (complete mapping with hardware/peripherals/method columns) Requirements marked `⚠️ v2 ONLY` are excluded from v1 coverage. Requirements with no automated test are flagged with the verification method.

## Legend

| Symbol | Meaning |
|--------|---------|
| U01–U19 | Unit test (serial) |
| I01–I03 | Integration test (serial + BLE) |
| S01–S02 | System test (end-to-end) |
| A01–A04 | Acceptance test (requirements traceability) |
| 🔧 | Manual / bench test only |
| 📱 | Phone-side test (Dart `flutter test`) |
| ☁️ | Cloud test (requires live server) |
| ⚠️ | v2 only — excluded from v1 |
| ⏳ | Not yet implemented |

---

## REQ-FUNC — Device Functional Requirements

| ID | Requirement | Unit | Integration | System | Acceptance | Notes |
|----|-------------|------|-------------|--------|------------|-------|
| **F01** | 100 Hz sensor acquisition ± 1% | U04 (descent triggers), U14 (compression flow) | | S01 (full run) | A01 | 🔧 Frequency verification needs oscilloscope; test mode exercises data flow |
| **F02** | 5-second rolling ring buffer (500 samples) | U09 (ring fills 500), U10 (stays at 500), U11 (resets on re-arm) | | S01 | A01 | |
| **F03** | Arm via 1000 ms inductive proximity | 🔧 | | | A01 | 🔧 Needs LDC1612 hardware + copper target disc at known distance |
| **F04** | Dual-mode barometric start: speed > 1.5 m/s OR drop > 2.0 m | U04 (speed), U05 (drop), U06 (no false trigger) | | S01 | A02 | |
| **F05** | Drain ring buffer 2 samples/cycle, 500 in 2.5s | U13 (run cycle exercises drain) | | S01 | A02 | 🔧 Buffer drain timing verification needs oscilloscope |
| **F06** | Auto-terminate after 10s flatline + stillness | U07 (flatline → POST_RUN), U08 (no premature stop) | | S01 | A02 | |
| **F07** | Adaptive bit-packing, ≥ 35% compression | U14 (T1/T2/T3 exercised), U15 (all packet types) | | S01 | A03 | 🔧 Compression ratio verification needs decompressed file inspection |
| **F08** | Circular Flash buffer, auto-overwrite, Flash % via BLE | U13 (run stored), U12 (flash self-test) | I01 (BLE reads flash %) | S01 | A03 | 🔧 Auto-overwrite needs 12+ runs (flash fill) |
| **F09** | Run metadata via BLE GATT | | I01 (read run count, run list) | | A03 | |
| **F10** | Run file transfer via BLE + CRC32 | | I03 (file transfer, CRC) | | A03 | |
| **F11** | Config R/W via BLE: name, arm side, discipline | | I02 (read config chars) | | | 🔧 Write + persist test planned (BLE write + reboot check) |
| **F12** | Sleep after 5 min inactivity | U01 (SLEEP ↔ IDLE forced) | | | A04 | 🔧 Natural sleep timeout needs 5-minute wait; force-tested via serial |
| **F13** | Wake from sleep via LDC1612 INTB | 🔧 | | | A04 | 🔧 Needs LDC1612 hardware + GPIO interrupt verification |
| **F14** | Audible beeper when armed | 🔧 | | | | 🔧 Arm device, listen for beep (PWM on P0.09) |
| **F37** | Time sync from phone on every connect | | I01 (BLE connect triggers time write) | | | 📱 Phone writes `ABC0`; serial echo confirms |
| **F38** | BLE MTU ≥ 247 | | I01 (verify MTU via BLE) | | | 🔧 BLE sniffer for definitive verification; test mode confirms connection |
| **F39** | BLE bonding (LE Secure Connections) | 🔧 | | | | 🔧 Pair/unpair/reconnect cycle; ArduinoBLE handles bonding |
| **F40** | BLE OTA firmware update | ⏳ | | | | ⏳ OTA trigger char added (ABCF); full DFU flow not yet tested |
| **F41** | RGB LED visual status | 🔧 | | | | 🔧 Visual: verify LED pattern per state (onboard IS31FL3194) |
| **F42** | Factory reset via 20s inductive hold | 🔧 | | S02 (serial factory reset) | | 🔧 Serial `R` tests flash reset; inductive hold needs LDC1612 |
| **F52** | ⚠️ v2 ONLY — UHF RFID footprint | | | | | ⚠️ |
| **F53** | ⚠️ v2 ONLY — RFID inventory rounds | | | | | ⚠️ |
| **F54** | ⚠️ v2 ONLY — RSSI nearest-tag | | | | | ⚠️ |
| **F55** | ⚠️ v2 ONLY — Gate events logged | | | | | ⚠️ |
| **F56** | ⚠️ v2 ONLY — RFID doesn't impact 100 Hz | | | | | ⚠️ |
| **F57** | ⚠️ v2 ONLY — SETUP mode | | | | | ⚠️ |

---

## REQ-PHONE — Phone Application Functional Requirements

| ID | Requirement | Unit | Integration | System | Acceptance | Notes |
|----|-------------|------|-------------|--------|------------|-------|
| **F15** | Pair left/right arms by RTC timestamp ±3s | 📱 | | | | 📱 Dart unit test with synthetic timestamps |
| **F16** | Cross-correlation alignment < 10 ms | 📱 | | | | 📱 Dart unit test with offset-injected quaternion traces |
| **F17** | Decompress and display run data | 📱 | I03 (decompress after transfer) | | | 📱 Dart unit test: decompress known binary, verify frame count |
| **F18** | Detect pole/gate impacts from accel spikes | 📱 | | | | 📱 Dart unit test with synthetic accel data + known spike times |
| **F19** | Gate timestamp table: R-pole left-aligned, L-pole right-aligned | 📱 | | | | 📱 Dart widget test: verify table layout |
| **F20** | User profile parameters R/W | 📱 | | | | 📱 Dart test: write profile, close app, reopen, verify persistence |
| **F21** | Read/modify device params via BLE | 📱 | I02 (BLE config read) | | | 📱 Phone writes ABC1, device advertises new name |
| **F22** | Compare two runs side-by-side | 📱 | | | | 📱 Dart widget test: load two runs, verify overlaid gate table |
| **F23** | Push only timestamps + barometric data to cloud | ☁️ | | | | ☁️ Requires cloud server; verify upload payload |
| **F24** | Retrieve cloud endpoint from bootstrap URL | ☁️ | | | | ☁️ Requires bootstrap server + redirect |
| **F25** | Trainer views group athletes' gate times | ☁️ | | | | ☁️ Requires cloud server with group/athlete data |
| **F26** | Kinematics-driven missed gate estimation | 📱 | | | | 📱 Dart unit test with synthetic quaternion + known gate positions |
| **F27** | Guessed times displayed with `*` | 📱 | | | | 📱 Dart widget test: verify asterisk on guessed timestamps |
| **F28** | Banana detection (same-side consecutive gates) | 📱 | | | | 📱 Dart unit test with known banana course |
| **F29** | Altitude + gate timestamp layout | 📱 | | | | 📱 Dart widget test: verify pane layout |
| **F30** | Vertical speed from barometric 10 Hz decimation | 📱 | | | | 📱 Dart unit test with known pressure trace → speed |
| **F31** | Vertical speed graph + gate markers | 📱 | | | | 📱 Dart widget test: verify graph + green/red markers |
| **F32** | Compare speed graphs between two runs | 📱 | | | | 📱 Dart widget test: two runs overlaid |
| **F33** | Barometric altitude graph + gate markers | 📱 | | | | 📱 Dart widget test: altitude graph + markers |
| **F34** | Compare altitude graphs between two runs | 📱 | | | | 📱 Dart widget test: overlaid altitude graphs |
| **F35** | Group-level default visibility | ☁️ | | | | ☁️ Requires cloud server |
| **F36** | Trainer override per-run visibility | ☁️ | | | | ☁️ Requires cloud server |
| **F43** | Offline upload queue | 📱 ☁️ | | | | 📱 Dart test: queue runs while offline; ☁️ end-to-end |
| **F44** | Gate numbers in timestamp table | 📱 | | | | 📱 Dart widget test: verify gate numbers render |
| **F45** | User-editable run name/label | 📱 | | | | 📱 Dart test: write label, read back |
| **F46** | Trainer deletes runs (device + local + cloud) | 📱 ☁️ | | | | 📱 Dart test: delete from local DB; ☁️ end-to-end |
| **F47** | Export runs as CSV + JSON | 📱 | | | | 📱 Dart test: export known run, verify CSV/JSON content |
| **F48** | Browse cloud runs for comparison | ☁️ | | | | ☁️ Requires cloud server |
| **F49** | Single-arm mode gate classification | 📱 | | | | 📱 Dart test: single-arm run, verify gate side from device config |
| **F50** | GDPR deletion with warning | ☁️ | | | | ☁️ Requires cloud server; 📱 verify warning dialog |
| **F51** | Display calibration status + prevent arming if < 2 | 📱 | | | | 📱 Dart test: read accuracy from BLE, verify arming refusal |
| **F57.1** | Course setup Mode A (sequential recording) | 📱 | | | | 📱 Dart test: tap sequence, verify gate list |
| **F57.2** | Course setup Mode B (update existing) | 📱 | | | | 📱 Dart test: move/delete/add gates |
| **F57.3** | Dual course view (map ↔ list) | 📱 | | | | 📱 Dart widget test: toggle view |
| **F57.4** | Delta-based course map format | 📱 | | | | 📱 Dart test: verify ΔP from START, ΔGPS from previous |
| **F58** | ⚠️ v2 ONLY — Tracing mode | | | | | ⚠️ |
| **F59** | ⚠️ v2 ONLY — Duplicate tag detection | | | | | ⚠️ |
| **F60** | ⚠️ v2 ONLY — Cloud course persistence | | | | | ⚠️ |
| **F61** | ⚠️ v2 ONLY — Spatial correlation | | | | | ⚠️ |

---

## REQ-PERF — Performance Requirements

| ID | Requirement | Unit | Integration | System | Acceptance | Notes |
|----|-------------|------|-------------|--------|------------|-------|
| **P01** | 100 Hz loop jitter < 500 µs σ | 🔧 | | | | 🔧 Oscilloscope on GPIO toggle per loop iteration |
| **P02** | Sensor → ring buffer < 2 ms | 🔧 | | | | 🔧 Oscilloscope: BHI read start → ring write end |
| **P03** | Start detection latency < 100 ms | 🔧 | | S01 | | 🔧 Pressure chamber + scope: descent onset → LOGGING transition |
| **P04** | BLE file transfer ≥ 20 KB/s | | I03 (measure throughput) | | | 🔧 BLE sniffer for definitive measurement |
| **P05** | Quaternion accuracy ≥ 2 before logging | 🔧 | | | | 🔧 Figure-8 calibration + BLE read accuracy field |
| **P06** | Cross-correlation T=0 < 10 ms | 📱 | | | | 📱 Dart test with known offset injection |
| **P07** | Compression ≥ 35% | 🔧 | | S01 | A03 | 🔧 Decode stored file, count bytes vs. frame count |
| **P08** | BLE 2M PHY, MTU ≥ 247 | | I01 | | | 🔧 BLE sniffer |
| **P09** | ⚠️ v2 ONLY — RFID round < 15 ms | | | | | ⚠️ |

---

## REQ-HW — Hardware & Environmental Requirements

| ID | Requirement | Verification |
|----|-------------|-------------|
| **H01** | −20°C to +40°C operating | 🔧 Environmental chamber |
| **H02** | ≥ 8 hours battery at −10°C | 🔧 Cold chamber + battery logger |
| **H03** | IP67 enclosure | 🔧 IP67 certification lab |
| **H04** | Inductive trigger through shell | 🔧 Bench: LDC1612 with enclosure material between coil and target |
| **H05** | Module thickness < 16 mm | 🔧 Caliper measurement |
| **H06** | Weight ≤ 40 g (prototype) | 🔧 Scale |
| **H07** | ≥ 10 runs storage before sync | 🔧 Fill flash with runs, verify count |
| **H08** | No ferromagnetic near BMM150 | 🔧 BMM150 calibration with Qi coil + transducer powered |
| **H09** | 200g shock survival | 🔧 Shock table / drop test |
| **H10** | Qi wireless charging | 🔧 Qi pad, measure charge time + verify LED |
| **H11** | ⚠️ v2 ONLY — UHF RFID footprint | ⚠️ |
| **H12** | ⚠️ v2 ONLY — RFID/BMM150 non-interference | ⚠️ |
| **H13** | DW3000 UWB footprint unpopulated | 🔧 Visual PCB inspection: verify QFN footprint, keepout, SPI traces, CSn pad; verify unpopulated = no effect on BMM150 |

---

## REQ-IF — Interface Requirements

| ID | Requirement | Verification |
|----|-------------|-------------|
| **I01** | BHI260AP ↔ nRF52832 I²C | 🔧 I²C bus analyzer + FIFO watermark scope |
| **I02** | BMP390 ↔ nRF52832 I²C | 🔧 I²C bus analyzer, verify 100 Hz read rate |
| **I03** | LDC1612 ↔ nRF52832 I²C + INTB | 🔧 I²C analyzer + scope on INTB for wake-from-sleep |
| **I04** | SPI Flash ↔ nRF52832 SPI | U12 (flash self-test) — exercises read/write/erase |
| **I05** | Device ↔ Phone BLE 5.0 + GATT + DFU | I01 (connect + read chars), I03 (file transfer) |
| **I06** | Left ↔ Right cross-arm proximity | 🔧 Bench: bring two arms together, verify each LDC1612 triggers independently |
| **I07** | Phone ↔ Cloud HTTPS | ☁️ Requires cloud server |
| **I08** | Beeper ↔ nRF52832 GPIO PWM | 🔧 Scope on P0.09 during arming |
| **I09** | Qi ↔ Battery charger | 🔧 Bench: Qi pad → measure charge current |
| **I10** | ⚠️ v2 ONLY — RFID ↔ nRF52832 SPI | ⚠️ |
| **I11** | DW3000 UWB ↔ nRF52832 SPI | 🔧 Visual: footprint + traces; scope: verify CSn never asserted, power-gate OFF |

---

## REQ-ROB — Robustness Requirements

| ID | Requirement | Unit | Integration | System | Acceptance | Notes |
|----|-------------|------|-------------|--------|------------|-------|
| **R01** | False arm reject (< 500 ms) | 🔧 | | | | 🔧 Serial pulse < 500ms, verify no state change |
| **R02** | Aborted start timeout (30s) | 🔧 | | | | 🔧 Arm, wait 30s, verify → IDLE |
| **R03** | Mid-run inductive trigger ignored | 🔧 | | | | 🔧 Bench: trigger LDC1612 during LOGGING, verify no effect |
| **R04** | Low-battery shutdown < 3.3V | 🔧 | | | | 🔧 Variable PSU → simulate VBAT drop, verify file closed |
| **R05** | Flash CRC mismatch → mark corrupt | | I03 (CRC verification) | | | 🔧 Inject bit errors in flash, verify run skipped in list |
| **R06** | BLE disconnect mid-transfer resume | | I03 (partial transfer recovery) | | | 🔧 Disconnect during chunk streaming, reconnect |
| **R07** | Sensor failure detection | 🔧 | | | | 🔧 Disconnect BHI260AP, verify BLE error flag |
| **R08** | Single-arm operation accepted | 📱 | | | | 📱 Dart test: phone handles single arm data |

---

## Coverage Summary

| Category | Total v1 Reqs | Automated | Manual 🔧 | Phone 📱 | Cloud ☁️ | v2 ⚠️ |
|----------|--------------|-----------|-----------|----------|----------|-------|
| Device Functional (F) | 24 | 15 (U+I+S+A) | 8 (F03,F08,F14,F38,F39,F40,F41,F42) | — | — | 6 |
| Phone Functional (F) | 24 | 0 | — | 19 | 7 | 4 |
| Performance (P) | 8 | 1 (I03) | 6 | 1 (P06) | — | 1 |
| Hardware (H) | 12 | 0 | 10 | — | — | 2 |
| Interfaces (I) | 10 | 2 (U12+I01+I03) | 7 | — | 1 (I07) | 1 |
| Robustness (R) | 8 | 1 (I03) | 6 | 1 | — | — |
| **TOTAL v1** | **86** | **19 (22%)** | **37 (43%)** | **21 (24%)** | **8 (9%)** | **14** |

### Test Coverage by Level

| Level | Count | What it covers |
|-------|-------|---------------|
| U01–U19 (Unit) | 19 | F01-F07, F12, I04, R01-R02 |
| I01–I03 (Integration) | 3 | F08-F10, F37, P04, I05, R05-R06 |
| S01–S02 (System) | 2 | F01-F08 (end-to-end), F42 |
| A01–A04 (Acceptance) | 4 | F01-F08, F12-F13 (requirements trace) |
| 📱 Dart tests (future) | ~21 | F15-F36, F43-F51, F57.1-F57.4 |
| 🔧 Manual/Bench | ~37 | HW, environmental, sensor-specific |
| ☁️ Cloud | ~8 | Cloud API, auth, visibility |

### Uncovered v1 Requirements (with reason)

| ID | Reason |
|----|--------|
| F03 | LDC1612 hardware needed for proximity test |
| F08 auto-overwrite | Requires 12+ runs in flash |
| F13 wake-from-sleep | LDC1612 INTB → GPIO wake needs hardware |
| F14 beeper | Audio verification — manual listen test |
| F38 MTU | BLE sniffer needed |
| F39 bonding | Manual pair/unpair cycle |
| F40 OTA DFU | Not yet implemented (planned) |
| F41 LED | Visual inspection |
| H01-H10 | Environmental/hardware — lab equipment needed |
| I01-I03, I06-I09 | Hardware interfaces — lab equipment needed |
| R01-R04, R07 | Hardware-specific edge cases |
