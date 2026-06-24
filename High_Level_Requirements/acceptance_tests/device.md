# Acceptance Tests — Device (v1.0)

*2026-06-24 — Part of the SGC test documentation ensemble.*

Acceptance tests verify that the device meets its high-level requirements
from an end-user perspective. They map directly to `sgc_requirements.md` REQ-FUNC.

> 📋 **See also:** [MASTER_TEST_PLAN.md](../system_design/architecture_modules/module_design/implementation/MASTER_TEST_PLAN.md) · [TEST_COVERAGE_MATRIX.md](../system_design/architecture_modules/module_design/implementation/TEST_COVERAGE_MATRIX.md)  
> **Sibling docs:** [phone.md](phone.md)

---

## Test Scenarios

| ID | Scenario | Requirements | Method | Hardware | Status |
|----|----------|-------------|--------|----------|--------|
| **A01** | Full run cycle: ARM → LOGGING → POST_RUN with data integrity | F01, F02, F03 | 🤖 Serial harness | Nicla ×1 | ✅ |
| **A02** | Start detection accuracy (dual-mode barometric trigger) | F04, F05, F06 | 🤖 + 🔧 Pressure chamber | Nicla + Chamber | ✅ |
| **A03** | Compressed file storage + retrieval via BLE | F07, F08, F09, F10 | 🤖 Serial + BLE | Nicla + PC | ✅ |
| **A04** | Sleep/wake cycle with RTC preservation | F12, F13 | 🔧 Manual (5-min wait) | Nicla ×2 | 🔧 |

### A01 — Full Run Cycle

**Objective:** Verify the complete state machine pipeline produces a valid, decompressible run file.

**Procedure:**
1. Device starts in SLEEP or IDLE
2. Serial command `a` → ARMED (simulates inductive trigger F03 for bench testing)
3. Serial injection of barometric descent data → LOGGING (F04)
4. 500+ frames logged, then barometric flatline + stillness → POST_RUN (F06)
5. Verify run metadata via `l` command (F09)
6. Download run via test harness (F10)
7. Decompress and verify frame count, timestamps, no gaps

**Automated:** `sgc_test_harness.py test_state_machine.py` (S01 scenario)

### A02 — Start Detection Accuracy

**Objective:** Verify dual-mode barometric trigger works correctly.

**Procedure:**
1. Arm device → ARMED with P₀ captured
2. **Mode A (speed):** Inject descent > 1.5 m/s sustained 200 ms → verify LOGGING
3. **Mode B (drop):** Inject cumulative drop > 2.0 m from P₀ → verify LOGGING
4. Verify no false trigger on flat pressure

**Automated:** U04, U05, U06 (serial injection)  
**Manual:** Pressure chamber for real BMP390 verification

### A03 — Compressed Storage + BLE Retrieval

**Objective:** Verify end-to-end data pipeline: compress → store → retrieve → decompress.

**Procedure:**
1. Complete a run (A01)
2. Verify run appears in BLE run list (I01)
3. Download via BLE file transfer (I03)
4. Verify CRC32 matches
5. Decompress and verify ≥ 35% compression ratio (P07)
6. Fill Flash with 12+ runs, verify circular overwrite (F08)

**Automated:** I01, I03, U12, U13, U14, U15

### A04 — Sleep/Wake Cycle

**Objective:** Verify power-saving sleep with RTC preservation.

**Procedure:**
1. Leave device idle > 5 min → verify sleep (F12)
2. Bring forearms together → verify wake within 2 s (F13)
3. Verify RTC time preserved across sleep
4. Verify BLE advertising resumes after wake

**Manual:** Requires LDC1612 hardware + 5-minute wait

---

## Pass Criteria

| Scenario | Pass Condition |
|----------|---------------|
| A01 | Run file produced, frame count correct, decompression succeeds, no gaps |
| A02 | LOGGING transitions at correct thresholds, no false triggers |
| A03 | CRC matches, compression ≥ 35%, circular overwrite works |
| A04 | Device sleeps ≤ 5 min idle, wakes on proximity, RTC preserved |
