# Integration Tests — Phone (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

Integration tests verify phone modules work together: BLE ↔ processing,
storage ↔ cloud, UI ↔ processing pipeline.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** `device.md` · `hardware.md`

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Status |
|----|----------|-------------|--------|----------|--------|
| **PI01** | BLE scan + connect + service discovery | F21, F51 | 🤖 Mock BLE | None | ⬜ Phase 2 |
| **PI02** | File download + decompress + store | F17, F43 | 🤖 Mock BLE | None | ⬜ Phase 2 |
| **PI03** | Processing pipeline: decompress → impact → gate → correlate | F16, F17, F18, F26 | 🤖 Dart unit | None | ✅ |
| **PI04** | Storage CRUD: save runs, read back, queue | F20, F43, F45 | 🤖 Storage test | None | ⬜ Phase 2 |
| **PI05** | Cloud API: bootstrap, upload, query | F23, F24, F25 | 🤖 Mock HTTP | None | ⬜ Phase 2 |
| **PI06** | UI ↔ Processing: gate table data flow | F19, F27, F29, F44 | 🤖 Widget test | None | ⬜ Phase 2 |

### PI01 — BLE Scan + Connect + Service Discovery

**Objective:** Verify BLE manager can discover, connect, and enumerate GATT services.

**Procedure:**
1. Mock `flutter_blue_plus` to simulate device advertisement
2. Verify scan results show SGC devices
3. Connect to device → verify bond/encrypt
4. Read GATT characteristics → verify expected values
5. Write UTC time → verify time sync callback
6. Read calibration accuracy → verify UI indicator (F51)

**Status:** ⬜ Phase 2 (mock BLE test)

### PI02 — File Download + Decompress + Store

**Objective:** Verify BLE file transfer integrates with decompressor and local DB.

**Procedure:**
1. Mock file transfer chunks from device
2. Stream chunks → decompressor → SensorFrame list
3. Verify decompressed frame count (F17)
4. Store Run + SensorFrames in local DB
5. Queue for cloud upload (F43)
6. Verify run appears in local run list

**Status:** ⬜ Phase 2

### PI03 — Processing Pipeline

**Objective:** Verify all processing modules chain correctly.

**Procedure:**
1. Feed synthetic compressed blob → decompressor → frames
2. Frames → impact_detector → impact timestamps (F18)
3. Frames + impacts → gate_time_estimator → gate timestamps (F26)
4. Left frames + right frames → cross_correlator → time offset (F16)
5. Merge left + right runs with offset → CombinedRun

**Status:** ✅ Covered by Phase 1 Dart unit tests (modular, not pipelined together yet)

### PI04 — Storage CRUD

**Objective:** Verify local database operations.

**Procedure:**
1. Insert Run + frames → verify read back
2. Update run label (F45) → verify persisted
3. Update user profile (F20) → verify across app restart
4. Queue run for upload (F43) → verify queue persists
5. Delete run → verify removed from DB

**Status:** ⬜ Phase 2 (storage test)

### PI05 — Cloud API

**Objective:** Verify cloud API client with mocked HTTP.

**Procedure:**
1. Bootstrap: GET hardcoded URL → receive endpoint (F24)
2. Upload: POST timestamps + baro to cloud → verify 201 (F23)
3. Verify raw 100 Hz frame data NOT in payload (F23)
4. Query: GET athlete runs → verify response (F25)
5. Set visibility: PUT group visibility → verify 200 (F35, F36)

**Status:** ⬜ Phase 2 (mock HTTP test)

### PI06 — UI ↔ Processing Data Flow

**Objective:** Verify UI correctly consumes processing output.

**Procedure:**
1. Feed CombinedRun → gate table widget → verify alternating L/R columns (F19)
2. Verify estimated times rendered with `*` (F27)
3. Verify gate numbers rendered (F44)
4. Verify altitude pane alongside gate table (F29)
5. Feed two CombinedRuns → comparison widget → verify correct overlay

**Status:** ⬜ Phase 2 (widget tests)
