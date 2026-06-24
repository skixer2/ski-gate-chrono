# System Tests — Phone (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

System tests verify the phone app's end-to-end behavior with real
or simulated external dependencies (BLE devices, cloud API).

> 📋 **See also:** [MASTER_TEST_PLAN.md](../architecture_modules/module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../architecture_modules/module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** `device.md`

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Status |
|----|----------|-------------|--------|----------|--------|
| **PS01** | Full 2-arm run download + display | F15-F19, F26-F27, F29, F44 | 🔧 ADB | Phone + Nicla ×2 | ⬜ Phase 8 |
| **PS02** | Run comparison + export | F22, F31-F34, F47 | 🤖 Widget + 🤖 Unit | None (widget), Phone (ADB) | ⬜ Phase 6/8 |
| **PS03** | Cloud upload + visibility lifecycle | F23, F35, F36, F43 | ☁️ | Phone + Cloud | ⬜ Phase 7 |

### PS01 — Full 2-Arm Download + Display

**Objective:** Connect to two devices, download runs, process and display.

**Procedure:**
1. Pair both arms via BLE → verify device list shows left + right (F15)
2. Download both run files → verify decompressed data (F17)
3. Verify cross-correlation alignment (F16)
4. Verify impact timestamps detected (F18)
5. Verify gate table renders alternating L/R (F19)
6. Verify estimated times marked with `*` (F27)
7. Verify gate numbers displayed (F44)
8. Verify altitude pane left, gate table right (F29)

**Status:** ⬜ Phase 8 (ADB + real hardware)

### PS02 — Run Comparison + Export

**Objective:** Athlete/coach can compare runs and export data.

**Procedure:**
1. Load two runs → verify side-by-side comparison (F22)
2. Verify speed graphs with gate markers (F31, F32)
3. Verify altitude graphs with gate markers (F33, F34)
4. Export to CSV → verify timestamps + metadata (F47)
5. Export to JSON → verify same data

**Status:** ⬜ Phase 6 (widget tests) + Phase 8 (ADB)

### PS03 — Cloud Lifecycle

**Objective:** Verify end-to-end cloud integration.

**Procedure:**
1. Complete run with push-to-cloud enabled → verify upload (F23)
2. Verify only timestamps + baro uploaded (not raw 100 Hz)
3. Set group visibility → verify propagated (F35)
4. Override per-run visibility → verify propagated (F36)
5. Go offline, queue runs → reconnect → verify sync (F43)

**Status:** ⬜ Phase 7 (live cloud server required)

---

## Pass Criteria

| Scenario | Pass Condition |
|----------|---------------|
| PS01 | Both runs downloaded, aligned, displayed with correct gate table |
| PS02 | Comparison view correct, export matches display data |
| PS03 | Data upload selective, visibility enforced, offline queue syncs |
