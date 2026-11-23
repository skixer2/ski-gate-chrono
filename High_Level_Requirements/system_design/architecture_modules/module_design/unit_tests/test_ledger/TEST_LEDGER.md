# OpenClaw Unified Test Ledger — Ski Gate Chrono (SGC)

**Owner role:** Lead Systems Coordinator (this chat / `ski_gate_chrono` session)  
**Test base folder:** `.../module_design/unit_tests/`  
**Ledger root:** `unit_tests/test_ledger/`  
**Last updated:** 2026-11-23 (0.25.21 serial 'V' query; F41 LED temporal encoding shipped in 0.25.20)
**Current baselines:** FW **0.25.21** (`V` command added; 0.25.20 = F41 temporal LED, on device) · Zephyr 4.4.0 / NCS v3.4.0, sysbuild + MCUboot dual-slot, OTA stage 1 · App **1.72.x** · HW **Nicla Sense ME P0** (2 MB MX25R1635F) + P1 schematic (validate_sgc_p1.py **GREEN** 11-13) · Port **COM4** (Unit B; COM3 = Unit A)
**Last harness:** 0.25.20 flashed + **Verified OK** + boot banner v0.25.20 (b0263, 11-23) · RX proven working on Unit B ('W' and 'D' both answered) · hexsafe archive protocol active
**Results dir:** `unit_tests/tmp_test_results/` · auto-push via `run_*.ps1`
**⚠️ Harness port default is COM8** — stale (Arduino era). Pass `-Port COM4` explicitly until the defaults are corrected.

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

### TC-2026-11-23-001 — SERIAL 'V' VERSION QUERY + 0.25.21 — OPEN (code committed, build not yet run)

| Field | Value |
|---|---|
| FW / App / HW | 0.25.21 (code) / n/a / Nicla P0 |
| Device | Unit B, **COM4** |
| Change | `pull_proto.c` console branch `V` → `{"ev":"version","ver":"0.25.21"}`; `VERSION` + `app_version.h` bumped to 0.25.21 |
| Why | `app_version.h` has claimed a "serial 'V' path" since 0.23.6 — it never existed. The harness sends `V` in `_check_fw_version()` and got `E 4` (unknown command), so every run reported "could not read FW version". Version was only observable at boot. |
| Gate | `test_protocol_conformance.py` now lists `V` in the console set (was a FAIL: "harness sends 'V' but the Zephyr console has no 'V' branch"). |
| DONE WHEN | 0.25.21 built, flashed, `V` over COM4 returns `{"ev":"version","ver":"0.25.21"}`, and `test_protocol_conformance.py` no longer lists `V` as a FAIL. |

**State at open (2026-11-23 18:2x UTC):** code patched and committed; **build not yet run**. 0.25.20 remains the flashed image (F41 LED temporal encoding, verified boot banner). This case is NOT closed — do not claim 0.25.21 as running until the flash is verified.

---

## 2b. Carried open items (not this case, but not closed either)

| Item | Status | Note |
|---|---|---|
| **Unit A serial RX dead** | OPEN — hardware suspicion | Unit A does not answer `W`/`D`; Unit B does. Documented in `Firmware_Zephyr/docs/unitA_serial_rx_fault.md`. Not a firmware defect. |
| **`recorder` stack 240 B free** | MONITOR | 784/1024 used at 0.25.20 (measured via `W`). No overflow with current code — no dynamic allocation anywhere in `src/`, and the tightest thread is static. Risk is future growth, not present behaviour. Zephyr stack guard (`CONFIG_MPU_STACK_GUARD` + `CONFIG_HW_STACK_PROTECTION`) faults instead of corrupting. |
| **2 × `pin_to_pin` tri-state MISO contention** | OPEN — awaiting JP convention | Schematic gate. |
| **3 × `power_pin_not_driven`** | OPEN — needs `PWR_FLAG` | Schematic gate. |
| **Harness port default COM8** | OPEN — stale | Arduino-era default in `run_smoke.ps1` / `run_core.ps1` / `run_full.ps1`. Pass `-Port COM4`. |

---

## 3. Raw logs

*(Protocol says paste raw serial here. Empty because no run has been captured for TC-2026-11-23-001 yet — the build has not been run. Populate this with the actual flash + `V` transcript before closing the case.)*

---

## 4. Automated test readiness — honest position (2026-11-23)

Asked directly: "Are the automated tests ready?" Answer: **partly — and the host-only half is more ready than the device half.**

### What runs and passes right now (no hardware)

| Gate | Result | What it proves |
|---|---|---|
| `test_spec_constants.py` | **PASS** (exit 0) | Requirements document constants match firmware source (ARM window, baro scale, etc.). Caught two real shipped regressions. |
| `test_protocol_conformance.py` | **FAIL** (exit 1, 10 drift errors) | Parses the firmware's real event names + console commands and fails when a test asserts something the firmware cannot emit. **This is failing today and should be treated as a real signal, not noise.** |

### The 10 protocol-conformance failures — what they mean

These are **not** firmware bugs. They are the test suite still speaking the Arduino dialect, plus dead commands:

| Failure | Reality |
|---|---|
| `{"ev":"cmd"}`, `{"ev":"echo"}`, `{"ev":"flash"}`, `{"ev":"state_blocked"}`, `{"ev":"status"}` | Arduino-era event names. The Zephyr firmware emits `sm`, `rec`, etc. |
| console commands `L`, `Q`, `R`, `T`, `i` | No such branches in the Zephyr console — they answer `E 4`. |

The Arduino-era device tests (`test_s03`…`test_s08`, `test_state_machine`, `test_start_detector`, `test_ring_buffer`, `test_bit_packer`, `test_edge_cases`, `test_end_detector`, `test_sensor_injection`, `test_flash`, `test_s02_factory_reset`) still contain those assertions. **`test_protocol_conformance.py` is doing exactly its job by failing** — it is the tripwire that says "this suite cannot be trusted against this firmware."

### Coverage ledger (mechanical, from `coverage_ledger.py`)

91 requirements in `sgc_requirements.md`:
- **COVERED 11 (12.1 %)**
- **NONE 29 (31.9 %)** — device requirements with no automated test
- PHONE 33 · MANUAL 4 · OUT_OF_SCOPE 14

The 29 uncovered device requirements are the real work list. Sample of high-value ones: F09 (run metadata over BLE), F10 (run transfer + CRC32), F13 (wake on piezo press), F37 (UTC time sync on connect), F38 (MTU negotiation), F40 (OTA), F62 (discipline budget), F63 (capacity estimate), R01–R08 (the field-failure requirements).

### Verdict

**Host-only gates: ready and meaningful.** **Device suite: not ready** — it needs the Arduino-dialect purge before its results mean anything. Running it today would produce failures that are test bugs, not firmware bugs, and that is worse than not running it: it trains you to ignore red.

**Recommended next step:** purge the Arduino dialect from the device tests (mechanical: `st`→`sm`, delete `?`/`L`/`Q`/`R`/`T`/`i` assertions or re-point them at real branches), then re-run `test_protocol_conformance.py` until it exits 0. Only then does a device-suite green mean anything.

---

## 5. Coordinator tasking

*(Protocol section. Empty — no subagent tasks open.)*

### Change register — requested additions

| ID | Requested | Source | Status |
|---|---|---|---|
| C-001 | Serial `V` version query | JP 2026-11-23 18:22 | **DONE in code** (0.25.21); build+flash pending |
| C-002 | Purge Arduino dialect from device tests | ZioClaw 2026-11-23 (from conformance FAIL) | OPEN — recommended next |
| C-003 | Correct harness port defaults (COM8 → COM4) | ZioClaw 2026-11-23 | OPEN |
| C-004 | `recorder` stack headroom review | JP 2026-11-23 18:22 | OPEN — monitor, not urgent |

