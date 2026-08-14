# OpenClaw Unified Test Ledger — Ski Gate Chrono (SGC)

**Owner role:** Lead Systems Coordinator (this chat / `ski_gate_chrono` session)  
**Test base folder:** `.../module_design/unit_tests/`  
**Ledger root:** `unit_tests/test_ledger/`  
**Last updated:** 2026-08-14 06:42 UTC  
**Current baselines:** FW **5.03** (in tree, unflashed) · App **1.10** · HW **v4.2** · Port **COM8**  
**Last harness:** `run_20260814_0829` smoke **ALL PASS** on **5.02** (after manual reset)

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
| **Device FW** | C++ / PlatformIO / Arduino_BHY2 / ArduinoBLE (Cordio) on **nRF52832** | `implementation/Firmware_implementation/` | Nicla Sense ME today; production = ANNA-B112 + SGC carrier |
| **Sensors** | BHI260AP (IMU/fusion), BMP390 (baro), LDC1612 (prox arm) | FW `src/sensors`, `src/state_machine` | SPI0 shared: BHI260 CS + MX25R CS — **no concurrent SPI** |
| **Flash** | MX25R1635F **2 MB** (bench Nicla) / BOM path **MX25R6435F 8 MB** | `src/storage/*` | SFDP size → layout (v5.01+); pre-roll **fixed** `0x0000–0x13FFF` |
| **Phone app** | **Flutter** · Android target | `implementation/Phone_app_prototype/` | iOS not primary |
| **PC tests** | Python harness + SIM | `unit_tests/`, `system_tests/` | COM8; JSON-lines device protocol |
| **Hardware** | Carrier PCB v4.2, USB-C charge, 8 MB flash option | `PCB_layout/`, hw docs in repo root / architecture | Qi dropped; beeper DNP |

### 1.2 Product flash map (FW ≥ 5.01)

| Region | Address (2 MB classic) | Scales with chip? |
|--------|------------------------|-------------------|
| FlashRing pre-roll 4000×20 B | `0x0000–0x13FFF` | **No** (product arm window) |
| Run slots N × ~244 KB | `0x14000` → config | Yes: 2 MB→8, 4 MB→16, 8 MB→32 |
| Config / Index / Self-test / spare | top 4 × 4 KB sectors | Yes (top-of-chip) |

Boot must emit: `{"ev":"flash_map","size_kb":…,"slots":…}` then `{"ev":"raw_store","ok":1,…}`.

### 1.3 BLE identity

| Item | Value |
|------|--------|
| **Local name** | Device config (default SGC family); ADV only in **IDLE / POST_RUN** |
| **Service UUID** | `53470000-0000-1000-8000-00805F9B34FB` |
| **UUID pattern** | `5347` + `XXXX` + `-0000-1000-8000-00805F9B34FB` |

| Char | UUID | Props | Role |
|------|------|-------|------|
| **ABC0** | `5347ABC0-0000-1000-8000-00805F9B34FB` | Write | UTC epoch time sync |
| **ABC1** | `5347ABC1-…` | R/W | Device name (≤20) |
| **ABC2** | `5347ABC2-…` | R/W | Arm side |
| **ABC3** | `5347ABC3-…` | R/W | Discipline |
| **ABC4** | `5347ABC4-…` | R/N | State (bits4–0) + sensor flags (7–5) |
| **ABC5** | `5347ABC5-…` | R/N | Battery % (6–0) + charging (7) |
| **ABC7** | `5347ABC7-…` | R/N | Flash used % |
| **ABC8** | `5347ABC8-…` | R/N | Run count u16 + oldest age u32 (6 B) |
| **ABC9** | `5347ABC9-…` | R | Run list JSON (512 B) |
| **ABCA** | `5347ABCA-…` | W | FT request (run id) |
| **ABCB** | `5347ABCB-…` | N | FT chunk (notify; FW paces ~20 B @ 25 ms) |
| **ABCC** | `5347ABCC-…` | R | FT CRC32 |
| **ABCD** | `5347ABCD-…` | R/N | FT status (2=done, 3=error, …) |
| **ABD0** | `5347ABD0-…` | R/N | Calibration |

**Sources of truth:**  
FW `src/ble/sgc_service.cpp` · App `lib/ble/sgc_service.dart`

### 1.4 Critical runtime invariants (do not regress)

1. **Init order:** BLE first → Flash → LDC → RawRunStore → Ring → BHY2.  
2. **No large stack/heap** after Cordio up (nRF52832) — index I/O streams ≤256 B pages (FW 5.02).  
3. **During BLE FT:** no BHY2.update / feed_sensors / ambient SPI (FW 4.97+).  
4. **Erases off descent path:** full-slot prepare at POST_RUN/boot, not ARM.  
5. **JSON-lines only** on serial (ADR-001).  
6. **LA units:** mm/s², |g|=9810 on phone ImpactDetector.  
7. **CRC:** payload-only (skip 16 B RunHeader) on app validate.

### 1.5 Related docs (do not duplicate wholesale)

| Doc | Role |
|-----|------|
| `unit_tests/TEST_CATALOG.md` | Automated keep/merge/drop + smoke/core/full |
| `unit_tests/README.md` | How to run harness |
| `system_tests/device.md` | Device system test semantics |
| `TOOLS.md` / `MEMORY.md` (workspace) | Operator cheatsheet + durable handoff |

---

## 2. Active Session Test Case

*Status: **OPEN** — BLE zombie / no ADV after app disconnect (blocks phone path)*

### Meta

| Field | Value |
|-------|--------|
| **Case ID** | `TC-2026-08-14-002` |
| **Title** | BLE stuck “connected” → no sleep, not scannable |
| **Objective** | After phone disconnect / app kill / BLE toggle, device must clear link, release hold_idle, re-ADV in IDLE so app can scan; serial `s`/`i` must recover without HW reset |
| **Baseline under test** | FW **5.03** (code ready), App **1.10**, Nicla COM8 |
| **Priority** | **P0** |
| **Opened** | 2026-08-14 |
| **Owner** | Lead Systems Coordinator |
| **Parent** | TC-2026-08-14-001 smoke PASS; BLE leg failed |
| **Code status** | **IMPLEMENTED** in workspace — await JP flash + bench |

### Hypothesis

V5.00 hold-idle + connect handlers are correct in intent, but recovery is incomplete:

1. `main` loop keeps `hold_idle = central_flag || BLE.connected() || ft` → if Cordio leaves a **zombie link**, IDLE never times out to SLEEP (matches JP: never slept).
2. `sgc_ble_update_state(IDLE|POST_RUN)`: if `BLE.connected()` → **only** `setLocalName`, **no** `restart_advertising` → phone scan empty while “logically IDLE”.
3. Serial `s`→SLEEP tries `disconnect` + clear flags; if disconnect is incomplete / events missed, `i`→IDLE still sees connected → still no ADV.
4. First smoke attempt needed **HW reset** and harness got `FW version None` until reboot — same wedged BLE/stack class of failure.
5. `main.cpp` POST_RUN still calls bare `BLE.advertise()` (known Cordio no-op class) — secondary inconsistency.

### Manual steps already done (JP)

1. Smoke after manual reset button → green (see §3).  
2. App scan for SGC: **not visible**.  
3. Closed app, phone BLE restart, device serial IDLE↔SLEEP↔IDLE: **still no SGC**, device **did not auto-sleep** (stuck hold / connected).

### Expected after fix (FW 5.03 target)

| Check | Expect |
|-------|--------|
| Kill app while connected | serial `ble_disc` within ~1–2 s (or force-recover path) |
| After disc in IDLE | `ble_adv` / scannable; hold_idle false; can IDLE→SLEEP in 120 s if no link |
| Serial `i` anytime | force recover + ADV if IDLE/POST_RUN discoverable |
| Serial `s` | drop link, ADV off, hold clear |
| App scan after recover | SGC visible without HW reset |
| Smoke | still green |

### Observed behavior

1. Smoke `run_20260814_0829`: U16–U19, U12–U13, S04 **ALL PASS** on FW 5.02 after **manual reset**.  
2. S04: **99.5 fps**, `store=raw`, `we=0`, ver=5.02.  
3. `raw_store` earlier boot fragment: `ok:1 slots=8 slot_kb=244 chip_kb=2048`.  
4. BLE: **not discoverable**; sleep inhibited → **zombie connected / hold_idle**.

### Verdict

| Field | Value |
|-------|--------|
| **Result** | `OPEN` — code in tree, **bench pending** |
| **Root cause** | Incomplete BLE link teardown + ADV restart while `BLE.connected()` sticky; hold_idle latches sleep |
| **Fix version** | **5.03** implemented (`sgc_ble_force_recover`) |
| **Follow-ups** | flash 5.03 → serial `i` → app scan → smoke; then core + FT |

---

## 2b. Closed / parked this session

### TC-2026-08-14-001 — FW 5.02 boot + smoke (layout/stack)

| Field | Value |
|-------|--------|
| **Result** | **PASS (smoke)** — core/BLE not claimed |
| **Evidence** | `run_20260814_0829`; boot ver 5.02; chip 2048 / 8 slots; S04 99.5 fps |
| **Note** | First harness attach failed until HW reset (see D-006). |

---

## 3. Component Logs

### 3.1 Hardware serial output

```text
# First smoke attempt (wedged — no FW version, then mid-boot after reset during U16):
WARNING: could not read FW version (got None)
U16 FAIL until device reboot mid-run:
← {"ev": "boot", "ver": "5.02", "rr": 4}
← {"ev": "led", "strip": 10, "timing_only": 0, "pin": 12}
← {"ev": "preroll_prep", "arm_cap": 3000, "total": 4000, "keep": 1000, "why": "boot"}
← {"ev": "init", "sub": "ble", "ok": 1}
← {"ev": "init", "sub": "raw_store"}
← {"ev": "raw_store", "ok": 1, "slots": 8, "slot_kb": 244, "chip_kb": 2048, "runs": 4}
← {"ev": "init", "sub": "raw_store_res", "ok": 1}
(KeyboardInterrupt — JP stopped; HW reset; re-ran smoke)

# Second smoke — clean. Status sample U13:
{"ev": "status", "st": "IDLE", ..., "runs": 4, "ver": "5.02", "tm": 1, ...}
# S04 end:
{"ev": "run_saved", "id": 0, "fr": 2119, "sz": 15298, "dur_ms": 21296, "fps10": 995, "ok": 1, "store": "raw", "we": 0, "runs": 1, "total": 1}
```

### 3.2 Flutter / logcat

```text
(no logcat paste — JP report: SGC never appeared in scan after app close +
 phone BLE restart + device IDLE/SLEEP/IDLE serial cycling)
```

### 3.3 PC harness summary

```text
run_20260814_0829  FW 5.02  Harness v2.26.0
U16 4/4  U17 7/7  U18 5/5  U19 5/5  PASS
U12 1/1  U13 9/9  PASS
S04 PASS 99.5 fps store=raw we=0
OVERALL smoke: ALL TESTS PASSED
Prerequisite: manual reset button before successful run
```

### 3.4 Attachments / paths

| Kind | Path |
|------|------|
| Harness results | PC `unit_tests/run_20260814_0829.md` |

---

## 4. Open defects / risks (coordinator board)

| ID | Sev | Symptom | Suspected area | Status |
|----|-----|---------|----------------|--------|
| D-001 | P0 | Boot crash after BLE on 5.01 index load/persist | `raw_run_store.cpp` 4 KB stack | **PASS on bench 5.02** (smoke) |
| D-002 | P1 | BLE FT hang if BHY2 SPI during transfer | shared SPI0 | Fixed 4.97; regression-watch |
| D-003 | P2 | S03 dump `bad_id` intermittent | host parse race | Mitigated SIM 2.50.0 retry |
| D-004 | P2 | U19 inject flake historically | serial/inject | **PASS** on 0829 |
| D-005 | P3 | No automated serial test for BLE hold-idle / re-ADV | BLE 5.00 | Manual only |
| **D-006** | **P0** | Zombie BLE connected: no auto-sleep, not scannable; HW reset needed for smoke | `sgc_service.cpp` / hold_idle / main ADV | **Code 5.03 ready — bench confirm** |

---

## 5. Coordinator task queue

| ID | Status | Notes |
|----|--------|-------|
| T-001 | **DONE** (code review OK) | `sgc_ble_force_recover` + FW 5.03 |
| T-002 | **DONE** (code review OK) | wired SLEEP/IDLE/serial `i`/desync/main POST_RUN |
| T-003 | **PENDING** | catalog baseline bump after JP confirms flash |

### 5.03 implementation review (coordinator)

**Files:** `config.h`, `ble/sgc_service.h`, `ble/sgc_service.cpp`, `main.cpp`

**Verified in tree:**
- `FW_VERSION "5.03"`
- `sgc_ble_force_recover`: FT abort → stop ADV → disconnect+≤3 poll → clear flags → ADV iff IDLE/POST_RUN → `ble_recover` JSON
- IDLE/POST_RUN: keep link only if `g_central_connected && BLE.connected()`; else force_recover
- SLEEP: `force_recover("sleep")` (ADV off)
- Serial `i`: `force_recover("serial_i")` even if already IDLE
- Main loop desync heal before hold refresh
- Bare `BLE.advertise()` removed from POST_RUN in main
- Not called from `on_ble_disconnected` (no recursive disconnect)

**Residual risk:** double ADV restart if disconnect handler fires during force_recover poll — should be idempotent.

**Not claimed green** until JP flashes and scans.

---

## 6. Case history index

| Case ID | Date | Title | Result | Archive |
|---------|------|-------|--------|---------|
| TC-2026-08-14-001 | 2026-08-14 | FW 5.02 boot + smoke | **PASS smoke** | §2b |
| TC-2026-08-14-002 | 2026-08-14 | BLE zombie / no ADV | **OPEN** | _(active §2)_ |

Snapshots: `test_ledger/history/`  
Optional deep-dive cases: `test_ledger/cases/TC-….md`

---

## 7. Quick commands (PC)

```powershell
# Flash
cd F:\Documents\Progetti\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Firmware_implementation
pio run -t upload -e nicla
pio device monitor -b 115200

# Smoke
cd ..\..\..\unit_tests   # adjust to unit_tests path on PC
$runId = "run_" + (Get-Date -Format "yyyyMMdd_HHmm")
foreach ($t in 'test_sensor_injection.py','test_flash.py','test_s04_bhy2_rate.py') {
  py sgc_test_harness.py --port COM8 $t --run-id $runId
}
```

Path note: JP PC canonical tree may be `F:\Documents\Progetti\ski-gate-chrono`; workspace mirror is under OpenClaw `ski_gate_chrono/`.
