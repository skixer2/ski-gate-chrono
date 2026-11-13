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

### TC-2026-11-13-001 — P1 DIODE SYMBOL POLARITY (JP visual-symbol rework) — OPEN, gate FAIL 5 errors

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

