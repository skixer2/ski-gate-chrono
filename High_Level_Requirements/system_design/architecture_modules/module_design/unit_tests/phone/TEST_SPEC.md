# SGC Phone App — Test Specification (v1.0)

*2026-06-24 — Initial release. Covers Phase 1 (Tier 1: Pure Dart unit tests).*

> 📋 **Part of the SGC test documentation ensemble:** [MASTER_TEST_PLAN.md](../../implementation/MASTER_TEST_PLAN.md) (overall strategy) · [TEST_COVERAGE_MATRIX.md](../../implementation/TEST_COVERAGE_MATRIX.md) (all v1 requirements mapped) · [requirements_traceability.md](../device/requirements_traceability.md) (device RTM)

---

## 1. Test Strategy

The SGC phone app follows a **three-tier test pyramid**, matching the firmware test architecture:

```
         ┌──────────────────────┐
         │ Tier 3: ADB          │  ← Real phone + real Nicla hardware
         │ Integration Tests    │     Python harness via ADB
         ├──────────────────────┤
         │ Tier 2: Widget +     │  ← Flutter widget_test + mock BLE
         │ Mock BLE Tests       │     No phone needed
         ├──────────────────────┤
         │ Tier 1: Pure Dart    │  ← flutter test, zero hardware
         │ Unit Tests (80%+)    │     Runs in CI
         └──────────────────────┘
```

| Phase | Tier | Description | Status |
|-------|------|-------------|--------|
| **Phase 1** | Tier 1 | Pure Dart unit tests — processing pipeline + models | ✅ DONE — 71 tests, 7 files |
| **Phase 2** | Tier 2 | Widget tests + mock BLE — UI components, BLE service mocks, banana/barometric modules | ⬜ NEXT |
| **Phase 3** | Tier 3 | Python + ADB integration — real phone ↔ BLE ↔ Nicla (×2) | ⬜ PLANNED |

---

## 2. Phase 1 — Test Case Inventory

### 2.1 Test Files

| # | Test file | Tests | Category | Covers |
|---|-----------|-------|----------|--------|
| 1 | `test/processing/decompressor_test.dart` | 14 | Processing | F17 |
| 2 | `test/processing/impact_detector_test.dart` | 8 | Processing | F18 |
| 3 | `test/processing/cross_correlator_test.dart` | 8 | Processing | F16 |
| 4 | `test/processing/gate_time_estimator_test.dart` | 9 | Processing | F26 |
| 5 | `test/models/vec3_test.dart` | 14 | Models | F26 (utility) |
| 6 | `test/models/run_test.dart` | 12 | Models | F15, F44 |
| 7 | `test/data/synthetic_data.dart` | — | Fixtures | All above |

**Total:** 65 unit tests (6 test files + 1 data module)

### 2.2 decompressor_test.dart (14 tests)

| # | Test Name | Requirement |
|---|-----------|-------------|
| 1 | Parses valid 16-byte header | F17 |
| 2 | Throws on short data (< 16 bytes) | F17 |
| 3 | Decodes Type 1 frame (delta quaternion + full la) | F17 |
| 4 | Decodes Type 2 frame (delta quaternion + passthrough la) | F17 |
| 5 | Decodes Type 3 frame (absolute quaternion reset + passthrough la) | F17 |
| 6 | Accumulates delta quaternion across consecutive Type 1 frames | F17 |
| 7 | Absolute reset restores full quaternion (Type 3) | F17 |
| 8 | Handles sign extension for negative delta values | F17 |
| 9 | Decodes mixed packet types in a single run stream | F17 |
| 10 | Extracts pressure from header and computes altitude | F17 |
| 11 | Detects CRC mismatch via metadata | F17 |
| 12 | Handles empty input | F17 |
| 13 | Decodes steady descent stream | F17 |
| 14 | Produces correct frame count | F17 |

### 2.3 impact_detector_test.dart (8 tests)

| # | Test Name | Requirement |
|---|-----------|-------------|
| 1 | Detects single impact spike | F18 |
| 2 | Detects multiple impacts (6-gate slalom) | F18 |
| 3 | Respects cooldown between impacts | F18 |
| 4 | Filters below-threshold spikes | F18 |
| 5 | Applies child multiplier (×1.5 sensitivity) | F18 |
| 6 | Uses adult multiplier (×1.0, default) | F18 |
| 7 | Returns empty for short frames (< 50) | F18 |
| 8 | Returns empty for empty input | F18 |

### 2.4 cross_correlator_test.dart (8 tests)

| # | Test Name | Requirement |
|---|-----------|-------------|
| 1 | Recovers known 150ms offset | F16 |
| 2 | Recovers 0ms offset (perfectly aligned) | F16 |
| 3 | Recovers negative offset (right before left, -200ms) | F16 |
| 4 | Returns 0 for very short runs (< 100 frames) | F16 |
| 5 | Returns 0 for empty frames | F16 |
| 6 | Reproducible with same seed | F16 |
| 7 | Offset precision is a multiple of 10ms (frame granularity) | F16 |
| 8 | Within ±3s window (spec requirement F16) | F16 |

### 2.5 gate_time_estimator_test.dart (9 tests)

| # | Test Name | Requirement |
|---|-----------|-------------|
| 1 | Bronze tier: estimates timestamps without course data | F26 |
| 2 | Gold tier: uses pressure matching for better precision | F26 |
| 3 | Prefers impact-detected over estimated timestamps | F26 |
| 4 | Generates alternating L/R gate sequence | F26 |
| 5 | Produces monotonic timestamps | F26 |
| 6 | Handles no-course edge case gracefully | F26 |
| 7 | Handles no-impact edge case (all gates estimated) | F26 |
| 8 | Handles empty input | F26 |
| 9 | Consistent between runs with same input | F26 |

### 2.6 vec3_test.dart (14 tests)

| # | Test Name | Requirement |
|---|-----------|-------------|
| 1 | Addition: (1,2,3) + (4,5,6) = (5,7,9) | F26 (utility) |
| 2 | Subtraction: (5,7,9) - (4,5,6) = (1,2,3) | F26 (utility) |
| 3 | Scalar multiply: 2 × (1,2,3) = (2,4,6) | F26 (utility) |
| 4 | Dot product: (1,2,3) · (4,5,6) = 32 | F26 (utility) |
| 5 | Cross product: î × ĵ = k̂ | F26 (utility) |
| 6 | Cross product: anti-commutative (a×b = −b×a) | F26 (utility) |
| 7 | Length: |(3,4,0)| = 5 | F26 (utility) |
| 8 | Normalization: (3,4,0) → (0.6, 0.8, 0) | F26 (utility) |
| 9 | Zero vector: |(0,0,0)| = 0 | F26 (utility) |
| 10 | Normalization of zero vector returns zero | F26 (utility) |
| 11 | Addition identity: v + (0,0,0) = v | F26 (utility) |
| 12 | Scalar zero: 0 × v = (0,0,0) | F26 (utility) |
| 13 | Dot product: perpendicular vectors = 0 | F26 (utility) |
| 14 | Cross product: parallel vectors = 0 | F26 (utility) |

### 2.7 run_test.dart (12 tests)

| # | Test Name | Requirement |
|---|-----------|-------------|
| 1 | SensorFrame equality by value | F15 |
| 2 | GateTimestamp equality by value | F44 |
| 3 | GateTimestamp.side enum values | F44 |
| 4 | Run.fromJson / toJson round-trip | F15 |
| 5 | CombinedRun merges left and right arms | F15 |
| 6 | MergedGate resolves side during pipeline | F44 |
| 7 | BarometricPoint equality and altitude | F30 |
| 8 | ArmSide enum values (left/right) | F15 |
| 9 | Discipline enum values (SL/GS/SG/DH) | — |
| 10 | Run formatVersion field | F15 |
| 11 | Run visibility field default | F35 |
| 12 | GateTimestamp.guessed flag default | F27 |

---

## 3. Requirement Coverage Matrix

### 3.1 Covered (Phase 1)

| Req ID | Requirement | Test File(s) | Status |
|--------|-------------|-------------|--------|
| **F16** | Cross-correlation alignment < 10 ms | `cross_correlator_test.dart` | ✅ 8 tests |
| **F17** | Decompress, store, display run data | `decompressor_test.dart` | ✅ 14 tests |
| **F18** | Detect pole/gate impacts from la spikes | `impact_detector_test.dart` | ✅ 8 tests |
| **F26** | Missed-gate kinematics pipeline | `gate_time_estimator_test.dart`, `vec3_test.dart` | ✅ 23 tests |
| **F15** | Pair arms by RTC timestamp proximity | `run_test.dart` (models only) | ⚠️ Partial — models tested, pairing logic not |
| **F44** | Gate numbers in timestamp table | `run_test.dart` (GateTimestamp model) | ⚠️ Partial — model tested, UI rendering not |
| **F30** | Vertical speed from barometric pressure | `run_test.dart` (BarometricPoint model) | ⚠️ Partial — model tested, barometric_speed not implemented |
| **F27** | Guessed times with trailing * | `run_test.dart` (guessed flag) | ⚠️ Partial — model tested, UI rendering not |
| **F35** | Group-level default visibility | `run_test.dart` (visibility field) | ⚠️ Partial — model tested, auth logic not |

### 3.2 Not Yet Covered

| Req ID | Requirement | Phase | Notes |
|--------|-------------|-------|-------|
| **F19** | Gate timestamps with alternating L/R layout | Phase 2 | Widget test for gate table rendering |
| **F20** | User profile read/write | Phase 2 | Storage + widget test |
| **F21** | Device parameters via BLE | Phase 3 | Mock BLE + ADB integration |
| **F22** | Side-by-side run comparison | Phase 2 | Widget test for comparison view |
| **F23** | Cloud upload (timestamps + baro only) | Phase 3 | Integration test |
| **F24** | Bootstrap cloud endpoint | Phase 2 | Mock HTTP test |
| **F25** | Trainer view of group athletes | Phase 3 | Integration test |
| **F28** | Banana detection (same-side gates) | Phase 2 | banana_detector not yet implemented |
| **F29** | Altitude left, gate timestamps right layout | Phase 2 | Widget test |
| **F31** | Vertical speed graph + gate markers | Phase 2 | Widget test |
| **F32** | Speed graph comparison | Phase 2 | Widget test |
| **F33** | Altitude graph + gate markers | Phase 2 | Widget test |
| **F34** | Altitude graph comparison | Phase 2 | Widget test |
| **F36** | Per-run visibility override | Phase 2 | Storage + integration test |
| **F43** | Offline sync queue | Phase 2 | Storage test |
| **F45** | User-editable run name | Phase 2 | Widget + storage test |
| **F46** | Trainer run deletion | Phase 3 | Integration test |
| **F47** | CSV/JSON export | Phase 2 | Unit test for export logic |
| **F48** | Cloud run browsing | Phase 3 | Integration test |
| **F49** | Single-arm operation | Phase 2 | Unit test for single-arm gate logic |
| **F50** | GDPR data deletion | Phase 3 | Integration test |
| **F51** | Calibration status display | Phase 2 | Widget + mock BLE test |
| **F57.1** | Course setup — Mode A (new, sequential) | Phase 2+ | Widget + storage test |
| **F57.2** | Course setup — Mode B (update existing) | Phase 2+ | Widget + storage test |
| **F57.3** | Course setup — Dual view (map/list toggle) | Phase 2+ | Widget test |
| **F57.4** | Course setup — Delta-based format | Phase 2+ | Unit test for format logic |

### 3.3 Coverage Summary

| Category | Total | Covered | Partial | Uncovered |
|----------|-------|---------|---------|-----------|
| Phone Reqs (F15–F61) | 31 | 4 (13%) | 5 (16%) | 22 (71%) |
| — v1 only (excl. v2 F52–F61) | 26 | 4 (15%) | 5 (19%) | 17 (65%) |

*Note: Most uncovered requirements are UI/widget-level (F19, F29, F31–F34) or integration-level (F21, F25, F48–F50) — these will be addressed in Phase 2 (Tier 2) and Phase 3 (Tier 3).*

---

## 4. Test Data Architecture

### 4.1 Synthetic Data Factory

**Location:** `test/data/synthetic_data.dart`

All test data is **deterministic** — same seed → same output. No random test flakiness.

| Generator | Returns | Used By |
|-----------|---------|---------|
| `buildCompressedBlob()` | `(List<int> blob, int frameCount, double startPressure)` | decompressor_test |
| `buildImpactFrames()` | `List<ImpactFrame>` with known impact indices | impact_detector_test |
| `buildCorrelatedFrames()` | `({left, right}, offsetMs)` pair with known time offset | cross_correlator_test |
| `buildSlalomRunFrames()` | `List<SensorFrame>` with alternating turns + impacts | gate_time_estimator_test |

**Companion Python script:** `scripts/generate_phone_test_data.py`
Produces identical binary + JSON fixtures for cross-validation between Dart and Python.

**Seed:** All generators use a fixed seed (42). Determinism verified by `cross_correlator_test` — test #6 confirms same input → same output.

### 4.2 Test Data Regeneration

```bash
# Dart: regenerated on every flutter test run (in-memory, no file fixtures)
cd Phone_app_prototype
flutter test

# Python cross-validation fixtures
python scripts/generate_phone_test_data.py
```

---

## 5. Test Runner

### 5.1 Python Runner (`scripts/run_phone_tests.py`)

**Usage:**
```bash
python scripts/run_phone_tests.py                  # run all, per-file mode (fast)
python scripts/run_phone_tests.py --per-test       # run each test individually (slower, richer output)
python scripts/run_phone_tests.py --file cross     # filter by test file name
python scripts/run_phone_tests.py --latest         # show latest results
```

**Output structure:**
```
test/results/YYYY-MM-DD_HHMMSS/
├── summary.json                    # Machine-readable index
├── summary.md                      # Human-readable with clickable links
├── decompressor_test/
│   ├── _raw.log                    # Raw flutter test stdout
│   ├── 01_parses_valid_16_byte_header.log
│   ├── 02_throws_on_short_data.log
│   └── ...
├── cross_correlator_test/
│   ├── _raw.log
│   └── ...
└── latest.json                     # Symlink to most recent summary
```

### 5.2 Direct Flutter Test

```bash
cd Phone_app_prototype
flutter test                           # all tests
flutter test test/processing/          # processing only
flutter test test/models/              # models only
flutter test --plain-name "recovers"   # single test by name
```

---

## 6. CI Pipeline

### 6.1 GitHub Actions (Planned)

```yaml
name: SGC Phone Tests
on: [push, pull_request]
jobs:
  unit:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: subosito/flutter-action@v2
        with:
          flutter-version: '3.22.0'
      - run: flutter test
        working-directory: Phone_app_prototype
```

### 6.2 Local Pre-Commit (Optional)

```bash
#!/bin/bash
cd Phone_app_prototype
flutter test || exit 1
```

---

## 7. Phase 2 — Planned Test Scope

| Module | Test Type | Covers |
|--------|-----------|--------|
| `banana_detector` | Pure Dart unit | F28 |
| `barometric_speed` | Pure Dart unit | F30 |
| `ble_manager` | Mock BLE unit | F21, F51 |
| `sgc_service` | Mock BLE unit | GATT characteristics |
| `file_transfer` | Mock BLE unit | Chunked download, CRC validation |
| `course_setup_screen` | Widget test | F57.1–F57.4 |
| `run_viewer` (gate table) | Widget test | F19, F27, F29, F44 |
| `run_viewer` (graphs) | Widget test | F31, F33 |
| `run_comparison` | Widget test | F22, F32, F34 |
| `run_export` | Pure Dart unit | F47 |
| `local_db` | Storage test | F20, F43, F45 |

---

## 8. Phase 3 — Planned Test Scope

| Module | Test Type | Covers |
|--------|-----------|--------|
| Full device ↔ phone BLE | Python + ADB | F10, F21, F37–F39 |
| Two-arm run download | Python + ADB + 2 Nicla | F15 |
| Cloud upload + sync | Python + ADB + real API | F23, F43 |
| Trainer dashboard | Python + ADB | F25, F46 |
| GDPR deletion | Python + ADB | F50 |
| Offline → online transition | Python + ADB | F43 |

---

## 9. Test Naming Conventions

| Convention | Example |
|------------|---------|
| `verb_noun_preposition_condition` | `recovers_known_150ms_offset` |
| `returns_value_for_edge_case` | `returns_0_for_empty_frames` |
| `handles_condition_gracefully` | `handles_no_course_edge_case_gracefully` |
| `method_property_constraint` | `cross_product_anticommutative` |

---

*Next: Phase 2 implementation — widget tests + mock BLE + banana_detector + barometric_speed.*
