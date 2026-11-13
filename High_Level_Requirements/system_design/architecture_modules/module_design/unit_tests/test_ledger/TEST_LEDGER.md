# OpenClaw Unified Test Ledger — Ski Gate Chrono (SGC)

**Owner role:** Lead Systems Coordinator (this chat / `ski_gate_chrono` session)  
**Test base folder:** `.../module_design/unit_tests/`  
**Ledger root:** `unit_tests/test_ledger/`  
**Last updated:** 2026-11-13 (Zephyr-era state refresh; Arduino-era case archived to history/)
**Current baselines:** FW **0.25.8** (`94b617d`, Zephyr 4.4.0 / NCS v3.4.0, sysbuild + MCUboot dual-slot, OTA stage 1) · App **1.72.x** · HW **Nicla Sense ME P0** (2 MB MX25R1635F) + P1 schematic (validate_sgc_p1.py **GREEN** 11-13) · Port **COM3**
**Last harness:** solo build+flash certification PASS (b0259b first-try, 11-13) · 0.25.8 flash **Verified OK** + boot banner v0.25.8 (11-12) · hexsafe archive protocol active  
**Results dir:** `unit_tests/tmp_test_results/` · auto-push via `run_*.ps1`

This ledger is the **living test + debug notebook** for cross-stack SGC work
(firmware · Flutter · BLE · hardware). Automated catalogs stay in
`TEST_CATALOG.md`; this file captures **architecture identity**, the **active
case**, raw logs, verdict, and coordinator tasking.

---

## 0. How to use (coordinator protocol)

1. **One active case at a time** in §2. When closed, move a snapshot to
   `history/YYYY-MM-DD_<slug>.md` and clear §2 for the next case.
2. Paste **raw** serial / logcat / Flutter console into §3 (do not paraphrase).
3. Coordinator decides architecture + fix direction → writes **atomic tasks**
   in §5 (file, method, inputs, outputs, constraints).
4. **DeepSeek subagents** implement tasks; coordinator reviews diffs, updates
   ledger verdict, and only then claims green.
5. Automated gates still run via `TEST_CATALOG.md` tiers (smoke / core / full).
   Ledger does **not** replace harness runs — it **frames** them.

### Task card format (mandatory for subagent work)

```text
TASK-ID: T-###
GOAL: <one sentence>
FILE(S): <path relative to repo>
ACTION: create | modify | delete | document | schematic
SYMBOL: <function/class/method or N/A>
BEHAVIOR: <what it must do>
INPUTS: <args / events / bus / BLE char>
OUTPUTS: <return / JSON event / GATT notify / file>
CONSTRAINTS: <stack, timing, no-float-scanf, SPI isolation, …>
DONE WHEN: <observable proof: serial line, test id, screenshot>
OUT OF SCOPE: <explicit non-goals>
```

---

## 1. System Architecture Map

### 1.1 Stack

| Layer | Technology | Location (repo) | Notes |
|-------|------------|-----------------|-------|
| **Device FW** | C / **Zephyr 4.4.0** (NCS v3.4.0, sysbuild + MCUboot dual-slot, MCUmgr over BLE) on **nRF52832** | `implementation/Firmware_Zephyr/apps/03_pull_proto` | Zephyr migration COMPLETE; Arduino/PlatformIO era retired; OTA stage 1 live |
| **Sensors** | BHI260AP (IMU/fusion), BMP390 (baro), **Piezo button** (arm/wake), LDC1612 (diag-only, lazy-init) | FW `src/sensors`, `src/state_machine` | SPI0 shared: BHI260 CS + MX25R CS — **no concurrent SPI** |
| **Flash** | MX25R1635F **2 MB** (bench Nicla) / BOM path **MX25R6435F 8 MB** | `src/storage/*` | SFDP size → layout (v5.01+); pre-roll **fixed** `0x0000–0x13FFF` |
| **Phone app** | **Flutter** · Android target | `implementation/Phone_app_prototype/` | iOS not primary |
| **PC tests** | Build/flash harness on JP-PC `C:\sgc\tools` (zbuild/zflash/bootcap/gate_version.py) + `unit_tests/` | COM3; versioned build dirs + hexsafe archive + Intel-HEX decode gate |
| **Hardware** | Nicla Sense ME (P0 prototype); production path = ANNA-B402 custom PCB | `PCB_layout/`, sgc_architecture_hardware.md v2.6 | P1 schematic gate GREEN (11-13); TS divider R42/R43 placed; D5 PMEG4005EPK placed |

## 2. Active Session Test Case

### TC-2026-11-13-001 — P1 DIODE SYMBOL POLARITY (JP visual-symbol rework) — CLOSED: PASS (2026-11-13 14:2x UTC; opened with 5 gate errors, resolved by JP flips 22645f5, see 14:2x entry)

| Field | Value |
|---|---|
| FW / App / HW | 0.25.8 (94b617d) / n/a / P1 schematic |
| Commits | ed74bce (rectangle → real Schottky graphics), 2e30428 (inverted diode pin numbering: pin 1 = bottom passive, pin 2 = top power_out) |
| Gate state | validate_sgc_p1.py **FAIL 5** (was GREEN before the pin swap) |

**Errors (authoritative, 11-13):**
1. `BUTTON_RAW`: 1 sheet / 3 islands — needs a global label or rewiring
2. `D5.2`: expected VBUS_USB, sheet gives VBUS_IN (30.48, 30.48)
3. `D5.1`: expected VBUS_IN, sheet gives VBUS_USB (30.48, 38.1)
4. `D6.1`: expected +1V8, sheet gives None (330.2, 191.77)
5. `D7.2`: expected GND, sheet gives None (330.2, 199.39)

**Root cause:** lib pin-position swap (2e30428) moved ALL placed D_SCHOTTKY connection points; only some sheet wires were re-anchored. Reference netlist unchanged and still authoritative: D5 pin 1 = VBUS_IN (K), pin 2 = VBUS_USB (A); D6/D7 = piezo clamp on BUTTON_RAW/+1V8/GND.

**Fix (either lane):** JP completes wiring in KiCad (swap D5's two wire labels: top → VBUS_USB, bottom → VBUS_IN; re-anchor D6/D7 wires to the new pin positions; re-bridge BUTTON_RAW) **or** ZioClaw programmatic pass via kiutils.

**DONE WHEN:** validate_sgc_p1.py FULLY GREEN.

**2026-11-13 12:4x — DATASHEET GROUND TRUTH (settles the polarity question):** Nexperia PMEG4005EPK datasheet §5 Pinning (Table 2, DFN1608D-2/SOD1608): **pin 1 = K = cathode** ("the marking bar indicates the cathode"), **pin 2 = A = anode**. Source: assets.nexperia.com/documents/data-sheet/PMEG4005EPK.pdf (fetched via r.jina.ai).

Consequences:
- The REFERENCE is correct as-is (D5 pin 1 = VBUS_IN = K, pin 2 = VBUS_USB = A — series diode conducts USB→BQ-IN). Original symbol also agreed: pre-change lib named pin 1 "K" (in-repo evidence: ed74bce diff of the old lib block).
- JP's redraw kept the physical wire nets but the new pin numbers invert the mapping: as-wired now, pad 1 (physically K) would get VBUS_USB and pad 2 (A) VBUS_IN = **diode reversed on the board = USB charging blocked**. The gate's 5 errors are real.
- JP's "in my graph 1 is anode" is contradicted by BOTH the datasheet AND his own graphics (bar/cathode side = pin 1) AND the pre-change symbol name (pin 1 = "K").
- Fix confirmed: swap the two global labels on D5's stubs (upper → VBUS_USB, lower → VBUS_IN) + re-anchor D6/D7 wires + re-bridge BUTTON_RAW. Then gate must be FULLY GREEN.

Open question logged for JP: is Documents\projects OneDrive/sync-redirected? (MASTER_TEST_PLAN.md insert was auto-reverted with a preserved old mtime on 11-13.)

**2026-11-13 13:2x — FULL VERDICT on the diode rework (independent kiutils extraction of JP's sheet + datasheets + DS-arbitration history):**

Independent extraction (extract_diodes.py, kiutils, JP's current 12:32-save file):
- D5: pin 1 (bar side) -> VBUS_USB; pin 2 -> VBUS_IN
- D6: pin 2 -> +1V8; pin 1 -> piezo chain side (unlabeled)
- D7: pin 1 -> GND; pin 2 -> piezo chain side (unlabeled)
- J5.1, R41.1: unlabeled islands; BUTTON_RAW labels deleted (JP doctrine 6e4f3fa: label useless for local cluster — the GATE accepts islands, it checks MEMBERSHIP)

Electrical verdicts:
1. **D5 (PMEG4005EPK, pin1=K per Nexperia Table 2)**: JP wired pad1(K) = VBUS_USB, pad2(A) = VBUS_IN -> diode conducts VBUS_IN -> VBUS_USB = REVERSED. On the board: USB charging BLOCKED. Fix: swap the two stub labels (upper -> VBUS_USB, lower -> VBUS_IN). His symbol graphics + pin positions are otherwise correct and self-consistent.
2. **D6/D7 (BAT42WS SOD-323)**: JP wired D6 pad2 = +1V8, pad1 = signal; D7 pad1 = GND, pad2 = signal — under his "pin 1 = anode" reading this reproduces the EXACT configuration DS pass-2 (1d4d2f3) flagged as wrong physics: "crushed positive pulses, left negative undershoot unclamped into U1.20 abs-max". The 11-07 arbitration (ad0c8e5) settled it: D7 flip STANDS — K = signal side, A = GND; D6 = K = +1V8, A = signal. As-drawn now: D7 clamps positive at ~0.3 V (destroys button HIGH), D6 clamps low at ~+1.5 V (destroys button LOW) -> button signal unusable. Fix per reference: D6.1 = +1V8 (re-anchor), D6.2 + D7.1 = piezo chain island (join J5.1/R41.1/D6.2/D7.1), D7.2 = GND.
   - CAVEAT: BAT42WS pin-1 A/K not re-verified today (datasheet bot-walled); the DS pass-2/arbitration (11-07, "verified against primary sources") is the recorded authority for K=signal on pad 1.
3. Why the morning "no divergence" missed this: at 11:23 the sheet matched the reference (gate GREEN — true at that time). The divergence ARRIVED with JP's afternoon commits (ed74bce, 2e30428) + redraws. The reference staleness (BUTTON_RAW group still expected) is a separate pre-existing gap — reference needs updating to JP's final design once he states the clamp-chain decision.

Fix options: JP in KiCad (file is open) OR ZioClaw kiutils pass on request. Gate = validate_sgc_p1.py FULLY GREEN.

**2026-11-13 14:2x — CLOSED: PASS.** JP flipped all three diodes in KiCad (save 14:13 UTC, commit 22645f5 "Flipped diodes... ZioClaw was right!"). Post-flip extraction + gate: D5.1=VBUS_IN(K), D5.2=VBUS_USB(A), D6.1=+1V8, piezo chain CONNECTED (islands resolved), census 70/359/72. **validate_sgc_p1.py FULLY GREEN, 0 errors.**

**RESIDUAL (new, TC-2026-11-13-002 candidate):** sgc_p1.kicad_pcb (PCB layout) is NOT under version control and NOT covered by the gate. Schematic diode flips (22645f5) are committed, but if the layout predates the flips, copper/nets for D5/D6/D7 may still reflect the OLD orientation. Action: update PCB from schematic in KiCad, verify diode pad nets on layout, then commit the .kicad_pcb (it is a load-bearing artifact - gitignore bypass means no history/protection). Note: my earlier "piezo chain open in the file" was an artifact of reading the truncated mid-sync file state — JP was right, everything was connected. TC-2026-11-13-001 CLOSED.

**2026-11-13 13:0x — BUTTON_RAW mystery RESOLVED + island-membership mechanism understood:**
- JP: "BUTTON_RAW was deleted a long ago" — CONFIRMED for LABELS: zero BUTTON_RAW strings in any of the 6 sheets (Unicode-safe Select-String; findstr false-negatives ruled out).
- Gate mechanism (validate_sgc_p1.py L195-219): a reference net that appears NOWHERE as a global label is checked by ISLAND MEMBERSHIP — the pins the reference puts on it must sit on ONE connected wire cluster. Deleting the label does NOT clear the error; connectivity must match.
- Current sheet state (12:32 UTC save): J5, R41, D6, D7, D5 all still PLACED (D2 DNP-absent). Reference expects on one BUTTON_RAW island: J5.1 + R41.1 + D6.2 + D7.1, with D6.1 → +1V8 and D7.2 → GND (piezo clamp chain, 11-04 design, DS-reviewed 11-07). JP's edits split it into 3 islands + float D6.1/D7.2.
- DESIGN QUESTION to JP (blocks gate-green): keep the piezo clamp chain as designed (then re-anchor wires to the new pin positions + restore net labels: BUTTON_RAW optional — islands suffice — but +1V8/GND labels required) OR drop the clamp chain entirely (then JP deletes J5/R41/D6/D7 instances in KiCad and ZioClaw removes the group from sgc_netlist.py).
- D5 fix unchanged: swap the two stub labels (upper → VBUS_USB, lower → VBUS_IN); datasheet-backed (pin 1 = K).

