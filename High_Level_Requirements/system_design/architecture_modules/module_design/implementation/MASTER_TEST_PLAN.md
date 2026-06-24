# SGC — Master Test Plan (v1.0)

*2026-06-24 — Initial release. Covers all v1 testing: firmware (Nicla), phone (Flutter/Dart), and cloud.*

---

## 1. Document Map

This is the top-level test document for the SGC project. All links below are clickable relative paths.

| Document | Location | Scope |
|----------|----------|-------|
| **Master Test Plan** | *this document* | Overall strategy, phases, risk, resources |
| [Test Coverage Matrix](TEST_COVERAGE_MATRIX.md) | `implementation/` | Every requirement → method, hardware, peripheral, status |
| [Acceptance Tests — Device](../../../../acceptance_tests/device.md) | `High_Level_Requirements/acceptance_tests/` | End-user device scenarios (A01-A04) |
| [Acceptance Tests — Phone](../../../../acceptance_tests/phone.md) | `High_Level_Requirements/acceptance_tests/` | End-user phone scenarios (PA01-PA07) |
| [System Tests — Device](../../../system_tests/device.md) | `system_design/system_tests/` | End-to-end device behavior (S01-S02) |
| [System Tests — Phone](../../../system_tests/phone.md) | `system_design/system_tests/` | End-to-end phone behavior (PS01-PS03) |
| [Integration Tests — Device](../../integration_tests/device.md) | `architecture_modules/integration_tests/` | Cross-module device (I01-I05) |
| [Integration Tests — Phone](../../integration_tests/phone.md) | `architecture_modules/integration_tests/` | Cross-module phone (PI01-PI06) |
| [Integration Tests — Hardware](../../integration_tests/hardware.md) | `architecture_modules/integration_tests/` | Peripheral ↔ SoC (HI01-HI11) |
| [Unit Tests — Device](../unit_tests/device/requirements_traceability.md) | `module_design/unit_tests/device/` | Device RTM, protocol, CI strategy, 38 test scenarios |
| [Unit Tests — Phone](../unit_tests/phone/TEST_SPEC.md) | `module_design/unit_tests/phone/` | Phone test strategy, case inventory, requirement coverage |
| [Phone Test Runner](../scripts/run_phone_tests.py) | `module_design/scripts/` | Python runner for phone tests |
| [SGC Requirements](../../../../sgc_requirements.md) | `High_Level_Requirements/` | All v1 requirements (F, P, H, I, R categories) |
| [System Design](../../../sgc_system_design.md) | `system_design/` | System-level architecture and data flow |
| [Build Plan](../phased_build_plan.md) | `module_design/` | Firmware phases 1-9 |

### V-Model Alignment

```
Requirements ──→ [acceptance_tests/](../../../../acceptance_tests/)     ← High_Level_Requirements
     │
System Design ──→ [system_tests/](../../../system_tests/)               ← system_design
     │
Architecture ──→ [integration_tests/](../../integration_tests/)  ← architecture_modules
     │
Module Design ──→ [unit_tests/](../unit_tests/)               ← module_design
     │
Implementation → [MASTER_TEST_PLAN.md](.)                    ← you are here
```

---

## 2. Test Strategy

### 2.1 Philosophy

SGC testing follows the **V-model**: each requirement has a corresponding test at the appropriate level.

```
Requirements ──→ Acceptance Tests (A01–A04)
     │
System Design ──→ System Tests (S01–S02)
     │
Architecture ──→ Integration Tests (I01–I03)
     │
Module Design ──→ Unit Tests (U01–U19, 📱 Dart)
     │
Implementation
```

**Key principles:**
- **Always-JSON** (ADR-001): firmware outputs one format. Test the code you ship — no `#ifdef` on output.
- **Deterministic fixtures**: all test data is seeded — same input → same output. No flaky tests.
- **Progressive coverage**: unit first → integration → system. Each tier gates the next.
- **Hardware where it matters, simulation where it doesn't**: processing logic is tested in pure Dart/Python; hardware-specific tests need the real device.

### 2.2 Test Levels

| Level | What It Tests | Environment | Examples |
|-------|--------------|-------------|---------|
| **Unit** | Individual module/driver in isolation | PC + serial (firmware), `flutter test` (phone) | Ring buffer push/pop, decompressor header parse, Vec3 math |
| **Integration** | Two or more modules communicating | PC + serial + BLE (firmware), mock BLE (phone) | BLE file transfer, flash ↔ ring buffer, phone ↔ device GATT |
| **System** | Full system end-to-end | Nicla device + PC + serial | ARMED → LOGGING → POST_RUN complete cycle |
| **Acceptance** | Requirements traceability verification | Nicla device + PC + serial | All F01-F08 verified, all test categories exercised |

### 2.3 Test Phases (Build-Aligned)

| Phase | Build Phase | What's Tested | Status |
|-------|-------------|---------------|--------|
| **Phase 0** | Setup | Harness + mock runner + CI skeleton | ✅ |
| **Phase 1** | 1-5 (BLE/BHY2/LED/Battery/SM) | State machine, sensors, BLE basics | ✅ |
| **Phase 2** | 6 (Flash) | Flash read/write/erase, ring buffer | ✅ |
| **Phase 3** | 7 (Full Pipeline) | BitPacker, start/end detection, compressed runs | ✅ |
| **Phase 4** | 7d-7e (Completeness) | Sensor readiness, calibration, edge cases | ✅ |
| **Phase 5** | 8a-8e (Phone App) | Phone processing pipeline (Dart unit tests) | ✅ |
| **Phase 6** | 8b-8e (Phone BLE/UI) | Widget tests + mock BLE | ⬜ NEXT |
| **Phase 7** | 8f (Cloud) | Cloud upload + sync | ⬜ |
| **Phase 8** | 8c (End-to-End) | Python + ADB: real phone ↔ real Nicla | ⬜ |
| **Phase 9** | 9 (Enclosure) | Hardware-dependent: environmental, shock, IP67 | 🔧 Manual |

---

## 3. Resource Requirements

### 3.1 Hardware

| Resource | Phase | For |
|----------|-------|-----|
| **Nicla Sense ME** (×1) | Phase 0–4 | Unit + integration + system + acceptance tests (serial) |
| **Nicla Sense ME** (×2) | Phase 8 | Cross-arm proximity, dual-arm BLE file transfer |
| **PC (Windows/Linux)** | Phase 0–8 | Test runner host, serial + BLE |
| **Android Phone** | Phase 6, 8 | Widget tests on device, ADB integration |
| **Oscilloscope** | Phase 9 | P01–P03 timing verification (100 Hz jitter, sensor latency) |
| **Pressure Chamber** | Phase 9 | H01–H02 environmental, P03 start latency |
| **BLE Sniffer** | Phase 9 | P04 throughput, P08 MTU verification |
| **Environmental Chamber** | Phase 9 | H01 (−20°C to +40°C) |
| **Qi Charging Pad** | Phase 9 | H10, I09 charging tests |
| **Programmable PSU** | Phase 9 | R04 low-battery shutdown, H02 battery life |
| **Shock Table** | Phase 9 | H09 200g shock survival |

### 3.2 Software

| Tool | Version | Purpose |
|------|---------|---------|
| Python | 3.10+ | Test harness (`sgc_test_harness.py`), mock runner, phone test runner |
| PySerial | 3.5+ | Serial communication with Nicla |
| PlatformIO | 6.x | Firmware build + upload |
| Flutter SDK | 3.22+ | Phone app build + `flutter test` |
| Dart | 3.x (bundled) | Phone processing unit tests |
| bleak | 0.21+ | BLE integration tests |
| GitHub Actions | — | CI pipeline |

---

## 4. Test Documentation Standards

### 4.1 Test Case Format

Every test case (unit or integration) follows this structure:

```
# Test: <descriptive name>
# File: <source file>
# Result: success | failure
# Exit code: <code>
# ============================================================

<stdout output>

# STDERR (if any):
<stderr output>
```

### 4.2 Naming Conventions

| Convention | Example |
|------------|---------|
| `verb_noun_preposition_condition` | `recovers_known_150ms_offset` |
| `returns_value_for_edge_case` | `returns_0_for_empty_frames` |
| `handles_condition_gracefully` | `handles_no_course_edge_case_gracefully` |
| `method_property_constraint` | `cross_product_anticommutative` |

### 4.3 Results Output

All test runs produce structured output in `test/results/YYYY-MM-DD_HHMMSS/`:
- `summary.json` — machine-readable index
- `summary.md` — human-readable with clickable links to each test log
- `latest.json` — pointer to most recent run
- Per-module subdirectories with `_raw.log` + per-test `.log` files

---

## 5. Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Hardware-dependent tests blocked (no device) | Medium | High | Mock runner (`sgc_mock_runner.py`) for development; real hardware for CI |
| BLE flakiness in CI | High | Medium | Retry logic in harness; mock BLE for unit-level phone tests |
| Serial port contention | Medium | Medium | Single-runner lock; dedicated CI machine |
| Environmental chamber availability | High | Low | Scheduled batch testing; manual acceptance before release |
| Cross-arm proximity reliability | Medium | High | Bench validation with copper targets at known distances; fallback to manual button-press in early field tests |
| Test data drift (synthetic diverges from real) | Low | Medium | Periodic regeneration from recorded field sessions; update seeds |

---

## 6. Test Execution

### 6.1 Daily Workflow (Development)

```bash
# Firmware changes: run all unit tests
cd Firmware_implementation
pio run -t upload -e nicla
python ../unit_tests/sgc_test_harness.py --port COM8 test_*.py

# Phone changes: run all Dart tests
cd Phone_app_prototype
python ../../scripts/run_phone_tests.py
```

### 6.2 CI Pipeline (Automated)

| Trigger | What Runs | Where |
|---------|-----------|-------|
| Push to main | All firmware unit tests | Self-hosted GitHub Actions (Windows + Nicla) |
| Push to main | All phone Dart tests | GitHub Actions (ubuntu-latest) |
| Cron (every 4h) | All firmware unit tests | Self-hosted runner |
| Manual `workflow_dispatch` | Full suite | Self-hosted runner |

### 6.3 Pre-Release Checklist

| Check | Method | Phase |
|-------|--------|-------|
| All unit tests pass (firmware + phone) | Automated | CI gate |
| All integration tests pass | Automated + manual | Phase 8 |
| All acceptance tests pass | Automated | CI gate |
| Performance timing (P01-P03) verified | 🔧 Oscilloscope | Phase 9 |
| BLE throughput (P04) verified | 🔧 BLE sniffer | Phase 9 |
| Environmental (H01-H03) verified | 🔧 Lab | Phase 9 |
| Field test (on-slope with 2 devices) | 🔧 Manual | Phase 9 |

---

## 7. Glossary

| Term | Definition |
|------|-----------|
| **SGC** | Ski Gate Chrono — the overall system |
| **Nicla Sense ME** | Arduino board with nRF52832 + BHI260AP + BMP390 + LDC1612 + MX25R1635F |
| **BHI260AP** | Bosch 6-axis IMU + sensor fusion (quaternion + linear acceleration at 100 Hz) |
| **BMP390** | Bosch barometric pressure sensor (altitude + vertical speed) |
| **LDC1612** | TI inductive proximity sensor (cross-arm arming trigger, wake-from-sleep) |
| **MX25R1635F** | Macronix 2 MB SPI NOR Flash (run storage) |
| **IS31FL3194** | ISSI I²C RGB LED driver (status indication) |
| **nRF52832** | Nordic BLE 5.0 SoC (application processor) |
| **BLE** | Bluetooth Low Energy 5.0 (phone ↔ device communication) |
| **GATT** | Generic Attribute Profile (BLE service/characteristic framework) |
| **MTU** | Maximum Transmission Unit (BLE packet size, target ≥ 247 bytes) |
| **DFU** | Device Firmware Update (Nordic OTA protocol) |
| **RTM** | Requirements Traceability Matrix |
| **ADR** | Architecture Decision Record |

---

*Next document: [TEST_COVERAGE_MATRIX.md](TEST_COVERAGE_MATRIX.md) — Complete requirement-by-requirement mapping with method, hardware, and peripheral information.*
