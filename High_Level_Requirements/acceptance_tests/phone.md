# Acceptance Tests — Phone (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

Acceptance tests verify that the phone app meets its high-level requirements
from an end-user (athlete/trainer) perspective.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../system_design/architecture_modules/module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../system_design/architecture_modules/module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** [device.md](device.md)

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Status |
|----|----------|-------------|--------|----------|--------|
| **PA01** | Download + view a complete 2-arm run | F15, F16, F17, F18, F19 | 🤖 + 🔧 | Phone + Nicla ×2 | ⬜ Phase 3 |
| **PA02** | Gate time estimation accuracy | F26, F27 | 🤖 Dart unit | None | ✅ |
| **PA03** | Course setup + gate numbering | F57.1-F57.4 | 🤖 Widget test | None | ⬜ Phase 2 |
| **PA04** | Cloud sync + visibility controls | F23, F35, F36, F43 | ☁️ | Phone + Cloud | ⬜ Phase 7 |
| **PA05** | Trainer dashboard + athlete management | F25, F46, F50 | ☁️ | Phone + Cloud | ⬜ Phase 7 |
| **PA06** | Offline operation + sync | F43, F45 | 🤖 + ☁️ | Phone + Cloud | ⬜ Phase 7 |
| **PA07** | Single-arm operation | F49, R08 | 🤖 Dart unit | None | ⬜ Phase 2 |

### PA01 — Download + View 2-Arm Run

**Objective:** End-to-end: connect both devices, download runs, align, display.

**Procedure:**
1. Connect phone to left + right Nicla via BLE
2. Download run from both devices (F10, F17)
3. Verify arm pairing by RTC timestamp (F15)
4. Verify cross-correlation alignment < 10 ms (F16)
5. Verify impact timestamps detected (F18)
6. Verify gate table renders with alternating L/R columns (F19)

**Status:** ⬜ Requires Phase 3 (ADB integration, real hardware)

### PA02 — Gate Time Estimation

**Objective:** Verify kinematics pipeline accuracy ±50 ms.

**Automated:** `flutter test test/processing/gate_time_estimator_test.dart` (9 tests)  
**Status:** ✅ Covered by Phase 1 Dart unit tests

### PA03 — Course Setup

**Objective:** Trainer can record/modify course gate positions.

**Procedure:**
1. Mode A: Walk course, tap at each gate → verify sequential recording (F57.1)
2. Mode B: Move/delete/add gates in existing course (F57.2)
3. Toggle map ↔ list view (F57.3)
4. Verify delta-based format storage (F57.4)

**Status:** ⬜ Phase 2 (widget tests)

### PA04 — Cloud Sync + Visibility

**Objective:** Runs upload automatically, visibility controls work.

**Procedure:**
1. Complete a run with push-to-cloud enabled → verify upload (F23)
2. Verify raw 100 Hz data stays local (F23)
3. Set group visibility to `denied` → verify other group members can't see runs (F35)
4. Override one run to `full` → verify that run is visible (F36)
5. Queue runs offline → verify auto-sync on reconnect (F43)

**Status:** ⬜ Phase 7 (cloud server required)

### PA05 — Trainer Dashboard

**Objective:** Trainer can manage group athletes and their data.

**Procedure:**
1. Login as trainer → view group athlete list (F25)
2. Select athlete → view runs + gate times
3. Delete a run → verify removal from device, phone, cloud (F46)
4. Initiate GDPR deletion → verify warning dialog + irreversible deletion (F50)

**Status:** ⬜ Phase 7 (cloud server required)

### PA06 — Offline Operation

**Objective:** App works without internet, syncs when reconnected.

**Procedure:**
1. Disable internet → complete runs → assign labels (F45)
2. Verify runs stored locally with labels
3. Enable internet → verify auto-sync (F43)
4. Verify queued runs persist across app restart (F43)

**Status:** ⬜ Phase 7

### PA07 — Single-Arm Operation

**Objective:** Phone handles runs from only one arm gracefully.

**Procedure:**
1. Download run from single device → verify gate detection still works (F49)
2. Verify cross-correlation is skipped
3. Verify gate side from device config (F11 arm side)
4. Compare two single-arm runs of same side → verify comparison works (F49)

**Status:** ⬜ Phase 2 (Dart unit test)
