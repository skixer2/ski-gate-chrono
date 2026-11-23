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

### TC-2026-11-23-001 — SERIAL 'V' VERSION QUERY + 0.25.21 — CLOSED: PASS (2026-11-23 19:36 UTC)

| Field | Value |
|---|---|
| FW / App / HW | 0.25.21 (code) / n/a / Nicla P0 |
| Device | Unit B, **COM4** |
| Change | `pull_proto.c` console branch `V` → `{"ev":"version","ver":"0.25.21"}`; `VERSION` + `app_version.h` bumped to 0.25.21 |
| Why | `app_version.h` has claimed a "serial 'V' path" since 0.23.6 — it never existed. The harness sends `V` in `_check_fw_version()` and got `E 4` (unknown command), so every run reported "could not read FW version". Version was only observable at boot. |
| Gate | `test_protocol_conformance.py` now lists `V` in the console set (was a FAIL: "harness sends 'V' but the Zephyr console has no 'V' branch"). |
| DONE WHEN | 0.25.21 built, flashed, `V` over COM4 returns `{"ev":"version","ver":"0.25.21"}`, and `test_protocol_conformance.py` no longer lists `V` as a FAIL. |

**RESULT — CLOSED: PASS.**

| Step | Evidence |
|---|---|
| Build | `BUILD_EXIT_0`, build dir `C:\sgc\build\p0240a` (the `fwbuild_sched.bat` argument is ignored — the script hardcodes its output dir; worth fixing) |
| Image gate | `gate_version.py` → **GATE_VER_OK** on `zephyr.signed.hex` |
| Archive | `C:\sgc\hexsafe\b0264.hex` (476 373 bytes) |
| Flash | OpenOCD **Verified OK**, `ZFLASH_EXIT_0` |
| Live query | `V` over COM4 → **`{"ev":"version","ver":"0.25.21"}`** — answered with **no reset** |
| State after | `D` → `{"rec":"SLEEP"}`, 12 runs intact |
| Conformance | `V` no longer a FAIL; console set now `BDFHSVWabdegswxy` |

**Note on the build-dir bug:** `fwbuild_sched.bat` takes an argument but `buildfw_off.bat` hardcodes `C:\sgc\build\latest`-style output. Passing `b0264` had no effect; the build landed in `p0240a`. Cosmetic but confusing — worth correcting so build dirs match the tag.

---

## 2b. Carried open items (not this case, but not closed either)

| Item | Status | Note |
|---|---|---|
| **Unit A serial RX dead** | OPEN — hardware suspicion | Unit A does not answer `W`/`D`; Unit B does. Documented in `Firmware_Zephyr/docs/unitA_serial_rx_fault.md`. Not a firmware defect. |
| **Heap is ZERO** | PROVEN — `CONFIG_HEAP_MEM_POOL_SIZE=0` in the generated `.config` (0.25.21 build). Zero `malloc`/`calloc`/`k_malloc`/`k_heap_alloc` in `src/`. There is no heap to exhaust. RAM risk is stack-only. |
| **`recorder` stack headroom** | MONITOR — **state-dependent, and my first number was wrong.** Sleeping: 280/1024 used (744 free, 27 %). An earlier sample showed 784/1024 used (240 free) — the deeper call path runs when the work handler is active. So the tight figure is the armed/logging one, not the idle one. No overflow today; no dynamic allocation anywhere. Zephyr stack guard faults rather than corrupting. |
| **2 × `pin_to_pin` tri-state MISO contention** | OPEN — awaiting JP convention | Schematic gate. |
| **3 × `power_pin_not_driven`** | OPEN — needs `PWR_FLAG` | Schematic gate. |
| **Harness port default COM8** | OPEN — stale | Arduino-era default in `run_smoke.ps1` / `run_core.ps1` / `run_full.ps1`. Pass `-Port COM4`. |

---

## 3. Raw logs

### TC-2026-11-23-001 — flash + V query transcript (2026-11-23 19:36 UTC, Unit B / COM4)

Flash (`zflash_b0264.log`, OpenOCD):
```text
** Programming Started **
Info : nRF52832-CIAA(build code: E0) 512kB Flash, 64kB RAM
Warn : Adding extra erase range, 0x00039588 .. 0x00039fff
** Programming Finished **
** Verify Started **
** Verified OK **
shutdown command invoked
ZFLASH_EXIT_0
```

Live query — `zcmd.py COM4 3 V`:
```json
{"ev":"version","ver":"0.25.21"}
```

State after — `zcmd.py COM4 3 D`:
```json
{"rec":"SLEEP"}
```

### `W` thread analyzer — 0.25.21, sleeping state (COM4)

```text
 BT RX               : STACK: unused 504  usage 520  / 1024  (50%)
 recorder            : STACK: unused 744  usage 280  / 1024  (27%)
 sensor_prod         : STACK: unused 1488 usage 48   / 1536  ( 3%)
 sensor_prod_stats   : STACK: unused 648  usage 288  / 1024  (28%)
 mcumgr              : STACK: unused 1768 usage 280  / 2048  (14%)
 sysworkq            : STACK: unused 1472 usage 576  / 2048  (28%)
 idle                : STACK: unused 256  usage 64   / 320   (20%)
```

**This is the IDLE/SLEEPING profile.** An earlier sample taken while the work
handler was active showed `recorder` at 784/1024 used (240 B free). The tightest
reading is therefore a function of what the unit is doing — quote the state with
the number or the number means nothing.

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
| C-001 | Serial `V` command | JP 2026-11-23 18:22 | **DONE — verified live** (0.25.21, `{"ev":"version","ver":"0.25.21"}` at 19:36 UTC) |
| C-002 | Device-suite overhaul (NOT a mechanical purge) | ZioClaw 2026-11-23 | OPEN — see §4 update. Scope is larger than first assessed. |
| C-005 | Stale duplicate schematic tree renamed | ZioClaw 2026-11-23 | **DONE** — `sgc_verify\...\pcb\sgc_p1` → `OBSOLETE_sgc_p1_snapshot_20261121`. That copy (21/11, untracked, pre-dates the 11-23 refdes fixes) emitted "schematic has annotation errors"; the real tree exports a netlist with no warning. |
| C-003 | Build-dir/tag mismatch in fwbuild_sched.bat | JP 2026-11-23 19:46 | **DONE** — tag forwarded, dir derived; proven: `b0265` built into `C:\sgc\build\b0265` (`BUILD_EXIT_0`, FLASH 169016 = 89.86 %, RAM 58380 = 89.08 %). `buildfw*.bat` also now refuse a missing dir instead of defaulting to the poisoned `latest`. NOT flashed — b0264 already carries 0.25.21; b0265 existed only to prove the fix. |
| C-003b | Harness port defaults COM8 → COM4 | ZioClaw 2026-11-23 | OPEN |
| C-004 | `recorder` stack headroom review | JP 2026-11-23 18:22 | OPEN — monitor, not urgent |

