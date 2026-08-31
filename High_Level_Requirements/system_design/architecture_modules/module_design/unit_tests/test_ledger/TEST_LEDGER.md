# OpenClaw Unified Test Ledger — Ski Gate Chrono (SGC)

**Owner role:** Lead Systems Coordinator (this chat / `ski_gate_chrono` session)  
**Test base folder:** `.../module_design/unit_tests/`  
**Ledger root:** `unit_tests/test_ledger/`  
**Last updated:** 2026-08-31 07:45 UTC  
**Current baselines:** FW **5.59** (Burst & Breathe + TX block forensics) · App **1.20** · HW **v4.2** · Port **COM8**  
**Last harness:** 5.27 partial — **5.37 smoke PASS** (run_20260824_1715, COM3)  
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
| **Device FW** | C++ / PlatformIO / Arduino_BHY2 / ArduinoBLE (Cordio) on **nRF52832** | `implementation/Firmware_implementation/` | Nicla Sense ME today; production = ANNA-B112 + SGC carrier |
| **Sensors** | BHI260AP (IMU/fusion), BMP390 (baro), **Piezo button** (arm/wake), LDC1612 (diag-only, lazy-init) | FW `src/sensors`, `src/state_machine` | SPI0 shared: BHI260 CS + MX25R CS — **no concurrent SPI** |
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

1. **Init order:** BLE first → Flash → RawRunStore → Ring → BHY2 → Piezo Button.  **LDC1612 NOT in boot path** (v5.34+) — lazy-init on `c`/`z` serial commands only.  
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

*Status: **OPEN** — FW **5.59** pushed (`d501062`), pending JP bench test on S22*

> **2026-08-31 — mechanism CONFIRMED in library source:** `HCI.sendAclPkt()`
> busy-blocks (`while (_pendingPkt >= _maxPkt) poll();`) when the nRF52
> controller TX queue is full, and `ATT.handleNotify()` **discards** its
> return value → `writeValue()` can freeze the FT loop for seconds with zero
> error signal. 244 B @ 30 ms outpaces the ~40–60 ms connection-event drain,
> so the device sits permanently at the congestion edge; when the S22
> controller/host suffocates and delays LL ACKs, the phone kills the link
> with `LINK_SUPERVISION_TIMEOUT` (0x08) after its supervision window.
> ACK-loop (5.50–5.56) and turbo push (5.58) both failed because they never
> addressed the queue-pressure trigger.
>
> **FW 5.59 = Burst & Breathe + forensics:**
> - 10 chunks @ 30 ms → 100 ms breathe gap (effective ~40 ms/chunk)
> - `writeValue()` block timing on every chunk: `ft_txblk` JSON > 50 ms,
>   adaptive extra breathe > 100 ms, clean `ft_abort("tx_blocked")` > 3000 ms
> - Supervision timeout hint 5 s → 10 s (recovery room for transient stalls)
> - Wire protocol unchanged — App 1.20 needs no update

### Meta

| Field | Value |
|-------|--------|
| **Case ID** | `TC-2026-08-26-001` |
| **Title** | FT blocking $\rightarrow$ S22 BLE Buffer Exhaustion $\rightarrow$ WDT Reboot |
| **Objective** | Complete batch download on S22 without `LINK_SUPERVISION_TIMEOUT` or reboots |
| **Baseline under test** | FW **5.50** · App **1.20** (ACK loop) · HW **v4.2** |
| **Priority** | **P0** |
| **Opened** | 2026-08-26 |
| **Owner** | Lead Systems Coordinator |
| **Parent** | TC-2026-08-25-001 (FT stall on S22) |

### Timeline (2026-08-24)

#### JP report — FW 5.32–5.33

```text
JP: FW 5.32 pushed. Button does not trigger ARMED.
    After booting, device enters ARMED without being asked.
    After `i` (SLEEP), P0.02 voltage = 0.4 V.
    After booting, P0.02 voltage = 1.8 V.
```

#### Root cause

LDC1612 was **still being initialized** in `setup()`:
- `g_ldc.begin()` started conversions → DRDY fires every ~819 µs
- `g_ldc.enable_interrupt()` reconfigured P0.02 as LDC INTB input
- LDC INTB open-drain output physically wired to P0.02, actively pulling LOW

All three symptoms explained:
1. **Auto-arm on boot** — INTB LOW → LATCH bit set → `WAKE_LDC` → auto-arm
2. **0.4 V after `i`** — LDC running, DRDY pulls INTB LOW ~98 % of the time
3. **Button dead** — pin already driven LOW by LDC; pressing button changes nothing

#### Fix — FW 5.34 (commit `54f4b7d`)

1. Removed `g_ldc.begin()` + `g_ldc.enable_interrupt()` from `setup()`
2. LDC lazy-init: `g_ldc.begin()` called on-demand only for `c` (recal) / `z` (diag)
3. `g_ldc_inited` flag guards all other `g_ldc.*` calls
4. `WAKE_LDC` → `WAKE_BUTTON` rename
5. Removed `g_ldc.force_recalibrate()` on SLEEP transition
6. Comments updated: P0.02 = "piezo button" not "LDC INTB"

### Expected bench results (FW 5.34)

- P0.02 idle voltage: **3.3 V** (pull-up, no LDC loading)
- P0.02 pressed voltage: **~0 V** (button to GND)
- Boot → **SLEEP** (no auto-arm — LATCH not set, pin is HIGH)
- Button press in SLEEP → **ARMED**
- `i` → SLEEP → button press → **ARMED**
- System Off → button press → **wake** (GPIO SENSE_LOW, no DRDY race)
- `?` status: `ldc:false,ldc_raw:0` (LDC not initialized)

### 2026-08-31 — FW 5.59 Burst & Breathe (commit `d501062`)

**Stall history (this case):** chunk 0 (5.54 L-STREAM) → ~50 (5.57 open-loop,
20 B) → ~136 ≈ 33 KB (5.46 @ 50 ms **and** 5.58 @ 30 ms turbo). Faster big
chunks got furthest → per-packet queue pressure, not per-byte volume, is the
trigger. Library mechanism: see blockquote above (verified in
`.pio/libdeps/nicla/ArduinoBLE/src/utility/HCI.cpp:638` + `ATT.cpp:624`).

**FW 5.59 changes (`file_transfer.cpp`, `sgc_service.cpp`, `config.h`):**

| # | Change | Why |
|---|--------|-----|
| 1 | Burst gate: 10 × 30 ms, then 100 ms breathe (~40 ms/chunk) | Keeps `_pendingPkt` off the `_maxPkt` ceiling; Android drains GATT/HCI + does LL housekeeping in the gaps |
| 2 | `writeValue()` timed per chunk | First-ever visibility into the silent block (previously invisible — return discarded by lib) |
| 3 | `ft_txblk` JSON when block > 50 ms | Serial forensics: shows phone falling behind seconds before supervision timeout |
| 4 | Adaptive +100 ms hold when block > 100 ms | Self-throttle before the wedge cascades |
| 5 | `ft_abort("tx_blocked")` when block > 3000 ms | Clean FT_ERROR to app beats zombie 5 s timeout |
| 6 | Supervision hint 500 → 1000 (10 s) | Transient wedges < 10 s recover instead of dying |

**Bench test protocol (JP, S22 + serial monitor open):**

1. `git pull && pio run -t upload -e nicla` → expect `ver=5.59`
2. App 1.20 → Download Latest Run → watch **both** phone log and device serial
3. **PASS:** `ft_done` with CRC OK on phone; serial shows `ft_prog` … `ft_done`
4. **If it stalls again, the serial now tells the story:**
   - `ft_txblk` lines with rising `blk_ms` **before** the stall → phone ACK
     starvation confirmed → next lever: `FT_BREATHE_MS` 100→200 ms or
     `FT_BURST_COUNT` 10→5 (both one-line changes)
   - `ft_abort,"tx_blocked"` → device detected the dying link cleanly
     (expected to replace the zombie timeout)
   - **No** `ft_txblk` at all + stall → block hypothesis wrong → next:
     phone-side host drain (app) or resume-capable batch protocol
5. Note the negotiated MTU/CI if visible in logcat

**Next levers (ordered):** (a) tune breathe/burst constants; (b) CI request
32–48 → shorter min; (c) resume-capable batch download (CMD_START with
offset — protocol + app change); (d) vendor patched ArduinoBLE exposing
`_pendingPkt` so pacing keys off real queue depth.
- `c` / `z` commands: LDC lazy-inits, diagnostics work (but P0.02 recontaminated — reboot after)

### Verdict

| Field | Value |
|-------|--------|
| **Result** | **PASS** — smoke green (run_20260824_1715, COM3) |
| **Code review** | ✅ Coordinator — full pin audit, all Arduino API calls verified |
| **Build** | ✅ JP built + flashed 5.37 |
| **Bench** | ✅ Boot→SLEEP, button→ARMED, P0.02 voltage correct |
| **Smoke** | ✅ sensor_injection + flash + S04 ALL PASS |
| **Notes** | • Button press needed to wake from System Off (not SLEEP) — tests send `i` for SLEEP wake |
|  | • SLEEP_SYSTEM_OFF_MS reverted to 1h (30s caused System Off during test setup) |
|  | • SSH key not yet set up — results push failed, results local only |
| **Next** | S03 stream run; SSH key setup (`ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_sgc`) |

#### Iteration 2 — FW 5.34 (removing begin() was not enough)

```text
JP: 5.34 flashed. Device still starts in ARMED.
    P0.02 = 1.8V at boot, 0V after 'i'.
    Button still dead.
```

Root cause: LDC1612 chip **powers up in active mode by default** — DRDY fires
immediately and INTB open-drain pulls P0.02 LOW, even without `g_ldc.begin()`.
The chip's power-on default has CONFIG bit 0 = 1 (active). Removing `begin()`
from setup was necessary but not sufficient — the chip was still actively
driving INTB LOW.

#### Fix — FW 5.35 (commit `0a64276`)

Added `LDC1612::quiesce()`:
- `Wire.begin()` + `Wire.setClock(100kHz)`
- Ping I2C address 0x2B (skip if not present)
- Write `CONFIG = 0x0000` (sleep mode — bit 0 = 0 stops conversions)
- Read `DATA0_MSB` + `DATA0_LSB` to clear any pending DRDY
- INTB releases HIGH (pull-up, no more DRDY)

Called in `setup()` **before** `g_button.begin()` to free P0.02 for the piezo button.

Boot log should show: `{"ev":"init","sub":"ldc_quiesce","ok":true}`

#### Iteration 3 — FW 5.35 (LDC quiesced, still broken)

```text
JP: LDC1612 physically removed from board. 5.35 flashed.
    Device still starts in ARMED.
    P0.02 = 1.8V at boot, 0V after 'i'.
    Button still dead.
```

Root cause: **Arduino pin mapping bug** — Nicla Sense ME PinDescription array maps:

```text
Arduino pin 0  → P0.10 (GPIO3)
Arduino pin 1  → P0.09 (GPIO2/RX)
Arduino pin 2  → P0.20 (GPIO1/TX)    ← WRONG PIN!
Arduino pin 3  → P0.23 (SCL1)
...
Arduino pin 10 → P0.02 (A0)          ← CORRECT PIN
```

Piezo button code used `PIN = 2` for everything:
- `pinMode(2, INPUT_PULLUP)` → configured **P0.20** (TX), not P0.02
- `attachInterrupt(digitalPinToInterrupt(2), ...)` → interrupt on **P0.20**
- `NRF_GPIO->IN >> 2` → read **P0.02** (correct, but no pull-up, floating!)

P0.02 was floating (1.8V at boot, 0V noise after `i`).
P0.20 (TX) toggled during serial comms → spurious ISR fires → `m_pressed = true` → auto-arm.

The LDC1612 code had the **same bug** (`INTB_PIN = 2`), but it used direct
register access for wake (`PIN_CNF[2]`), so it partially worked.

#### Fix — FW 5.36 (commit `343102c`)

Split into two constants:
- `ARDUINO_PIN = 10` (A0 = P0.02) for `pinMode` / `attachInterrupt`
- `NRF_PIN = 2` (P0.02 physical) for `NRF_GPIO->IN` / `PIN_CNF` register access

Also fixed LDC1612 `INTB_PIN` (was 2, now 10).

#### Iteration 4 — FW 5.36 (LDC removed, still broken)

```text
JP: LDC1612 physically removed from board. 5.36 flashed.
    Device still starts in ARMED. P0.02 = 1.8V at boot, 0V after 'i'.
    Button still dead.
    "Are you telling me that we spent a week debugging the LDC behaviour,
     and the problem was a pin definition? Now check all the PIN definitions!"
```

Root cause: **Same pin mapping bug in 3 more files.** Full audit of every
`pinMode` / `digitalWrite` / `digitalRead` / `attachInterrupt` call:

| File | Code | Got | Should be | Impact |
|------|------|-----|-----------|--------|
| `spi_flash.cpp` | `pinMode(26)` | out of range | `16` (CS FLASH) | DP never worked |
| `spi_flash.cpp` | `pinMode(3)` | P0.23 (SCL1!) | `9` (SCK P0.11) | toggled I²C SCL |
| `spi_flash.cpp` | `pinMode(4)` | P0.22 (SDA1!) | `8` (MOSI P0.27) | toggled I²C SDA |
| `main.cpp` `w` | `pinMode(22)` | out of range | `4` (SDA1) | silent no-op |
| `main.cpp` `w` | `pinMode(23)` | out of range | `3` (SCL1) | silent no-op |
| `main.cpp` `?` | `digitalRead(10)` | P0.02 (button!) | no Qi hw | wrong `qi` field |

#### Fix — FW 5.37 (commit `11e2839`)

Full pin audit + fix all remaining Arduino API pin mapping bugs:
- `spi_flash.cpp`: 26→16, 3→9, 4→8 (both enter + release DP)
- `main.cpp w`: 22→4, 23→3 (SDA1/SCL1 toggle)
- `main.cpp qi`: `digitalRead(10)` → `false` (no Qi hardware)
- Verified correct (no change): `PIN_CNF[2]`, `PIN_CNF[14]`, `PIN_CNF[19]`,
  `NRF_P0->OUTCLR`, `NRF_GPIO->IN >> 2` — all direct register access, correct

### Pin map reference (Nicla Sense ME)

```
Arduino  0 → P0.10 (GPIO3)          Arduino 10 → P0.02 (A0 / button)
Arduino  1 → P0.09 (GPIO2/RX)       Arduino 11 → P0.30 (A1)
Arduino  2 → P0.20 (GPIO1/TX)       Arduino 12 → P0.19 (ESLOV/LED)
Arduino  3 → P0.23 (SCL1)           Arduino 13 → P0.18 (Reset BHI260)
Arduino  4 → P0.22 (SDA1)           Arduino 14 → P0.14 (BHI260 INT)
Arduino  5 → P0.24 (GPIO0)          Arduino 15 → P0.25 (BQ25120 CD)
Arduino  6 → P0.29 (CS BHI260)      Arduino 16 → P0.26 (CS FLASH)
Arduino  7 → P0.28 (MISO)           Arduino 17 → P0.31 (CS BHI260 alt)
Arduino  8 → P0.27 (MOSI)
Arduino  9 → P0.11 (SCK)
```

## 2b. Closed / parked this session

### TC-2026-08-14-002 — BLE zombie / no ADV (5.03)

| Field | Value |
|-------|--------|
| **Result** | **PASS (5.03)** — `sgc_ble_force_recover` fixed zombie link + ADV restart |
| **Fix** | `sgc_ble_force_recover` in `ble/sgc_service.cpp` |
| **Closed** | 2026-08-14 |

### TC-2026-08-14-001 — FW 5.02 boot + smoke

| Field | Value |
|-------|--------|
| **Result** | **PASS (smoke)** — core/BLE not claimed |
| **Evidence** | `run_20260814_0829`; boot ver 5.02; chip 2048 / 8 slots; S04 99.5 fps |
| **Note** | First harness attach failed until HW reset (see D-006). |

---

## 3. Component Logs

### 3.1 Hardware serial output (5.03 BLE OK)

```text
{"ev":"flash_map","size_kb":2048,"slots":8,"slot_kb":244,"cfg":2080768,"idx":2084864,"preroll_end":81920}
{"ev":"raw_store","ok":1,"slots":8,"slot_kb":244,"chip_kb":2048,"runs":1}
{"ev":"st","from":"SLEEP","to":"IDLE"}
{"ev":"ble_adv","why":"state"}
{"ev":"ble_recover","why":"state","st":"IDLE"}   # boot double-fire — cosmetic
{"ev":"ready","st":"IDLE","runs":1,"ver":"5.03","used_pct":12}
{"ev":"ble_conn","addr":"43:80:d1:f0:fb:e9"}
{"ev":"ble_adv","why":"disconnect"}
{"ev":"ble_disc","st":"IDLE"}
… multiple app reconnect cycles …
{"ev":"timeout","from":"IDLE","to":"SLEEP"}
{"ev":"ble_recover","why":"sleep","st":"SLEEP"}
{"ev":"st","from":"SLEEP","to":"IDLE"}
{"ev":"ble_recover","why":"serial_i","st":"IDLE"}
```

### 3.2 Flutter / logcat

```text
JP: after hard reset, SGC found; close/reopen app many times — OK.
(First attempt without phone BLE/app restart was a false start.)
```

### 3.3 PC harness summary

```text
run_20260814_0900  FW 5.03  Harness v2.26.0
test_sensor_injection  ✅ 21/21
test_flash             ❌ 9/10  (U12 PASS; U13 step2 got cmd B instead of status)
test_s04_bhy2_rate     ✅ 1/1   S04 99.7 fps store=raw we=0 ver=5.03
```

### 3.4 Attachments / paths

| Kind | Path |
|------|------|
| Smoke 0900 | `unit_tests/tmp_test_results/run_20260814_0900*` |
| Prior 5.02 | `run_20260814_0829` (PC; may not be in workspace) |

---

## 4. Open defects / risks (coordinator board)

| ID | Sev | Symptom | Suspected area | Status |
|----|-----|---------|----------------|--------|
| **D-008** | **P0→CODE MITIGATED** | `ble_radio` restart returns ok=0 after heavy traffic | Heavy 244 B FT; BLE.end() can't drain Cordio pending HCI | **T-008c: NVIC_SystemReset fallback (5.16)** — self-recovery confirmed. Zombie path fixed by T-008b. Active-traffic path reboots. |
| **D-009** | **P0→CLOSED** | FT stall after flash (warm reset) — Cordio statics survive | nRF52 warm reset | **T-008a warm_deinit (5.15) CONFIRMED** — BLE ok:1 on every warm boot |
| D-001 | P0 | Boot crash after BLE on 5.01 index load/persist | `raw_run_store.cpp` 4 KB stack | **PASS on bench 5.02** (smoke) |
| D-002 | P1 | BLE FT hang if BHY2 SPI during transfer | shared SPI0 | Fixed 4.97; regression-watch |
| D-TIME | P2 | Run ts_utc stuck at 1970 | ABC0 no-op / create_run | **PASS bench** — after phone connect, S03 run time correct (4.99 path) |
| D-003 | P2 | S03 dump `bad_id` intermittent | host parse race | Mitigated SIM 2.50.0 retry |
| D-004 | P2 | U19 inject flake historically | serial/inject | **PASS** on 0829 |
| D-005 | P3 | No automated serial test for BLE hold-idle / re-ADV | BLE 5.00 | Manual only |
| D-006 | P0 | Zombie BLE connected: no auto-sleep, not scannable | `sgc_ble_force_recover` | **CLOSED PASS on 5.03** |
| D-007 | P2 | U13 step "Record initial runs" got `cmd B p=0` instead of `status` | harness serial RX race | **WATCH** — flake on 0900; **PASS 10/10** on 0911 |

---

## 5. Coordinator task queue

| ID | Status | Notes |
|----|--------|-------|
| T-001 | **DONE** | `sgc_ble_force_recover` + FW 5.03 |
| T-002 | **DONE** | wired SLEEP/IDLE/serial `i`/desync/main POST_RUN |
| T-003 | **PENDING** | catalog baseline 5.03 + harness 2.27 note |
| T-004 | **DONE** | harness → `tmp_test_results/` auto staging (v2.27) |
| T-005 | **OPEN** | harden U13/`?` expect against stray `cmd B` (D-007) |
| T-006 | **DONE** | PC→VPS `push_test_results.ps1/.sh` via SSH **key** (no password); DeepSeek + review/push |
| **T-008** | **DONE** | FW 5.15 T-008a+b + FW 5.16 T-008c — confirmed on bench |
| **T-008a** | **DONE ✅** | warm_deinit on every warm boot → BLE ok:1 (rr:2, rr:4, rr:6) |
| **T-008b** | **DONE ✅** | zombie: ok:1 retry:0. Active traffic: ok:0 → T-008c catches. |
| **T-008c** | **DONE ✅** | NVIC_SystemReset fallback — `reboot:1` → clean recovery → FT OK |
| **T-009** | **DONE** | 5.16 validated: S03 OK, FT OK after self-recovery. Full smoke not run. |

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
| TC-2026-08-14-002 | 2026-08-14 | BLE zombie / no ADV | **PASS 5.03** | §2b (closed) |
| TC-2026-08-15-001 | 2026-08-15 | FT download hang + `ble_radio` regression | **PASS (5.16)** | history |
| TC-2026-08-22-001 | 2026-08-22 | LDC stale-status race + System Off wake + boot diagnostics | **PASS (5.27)** | history |
| TC-2026-08-24-001 | 2026-08-24 | LDC1612 still in boot path — button dead + auto-arm | **PENDING JP BENCH (5.34)** | §2 (active) |
| run_20260814_0900 | 2026-08-14 | Smoke 5.03 | inject+S04 PASS; flash **9/10** U13 flake | `tmp_test_results/` |
| run_20260814_0911 | 2026-08-14 | Smoke 5.03 | **ALL PASS** 21+10+1 | `tmp_test_results/` (SSH push OK) |
| 2026-08-18 FT | 2026-08-18 | FW 5.14 FT warm-reset stall + hard-reboot success | FT ✅ after POR; `ble_radio` ❌ | §2 + §3 |

Snapshots: `test_ledger/history/`  
Optional deep-dive cases: `test_ledger/cases/TC-….md`

---

## 7. Quick commands (PC)

```powershell
# Flash
cd F:\Documents\Progetti\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Firmware_implementation
pio run -t upload -e nicla
pio device monitor -b 115200

# Smoke (run + auto-push results to the coordinator)
cd ..\..\..\unit_tests   # adjust to unit_tests path on PC
.\run_smoke.ps1          # -SkipPush to skip push, -Clean to prune old runs first
# Core tier: .\run_core.ps1
```

Path note: JP PC canonical tree may be `F:\Documents\Progetti\ski-gate-chrono`; workspace mirror is under OpenClaw `ski_gate_chrono/`.
## L-STREAM (v5.54) Architecture
Consolidated FT characteristics into single Stream characteristic to eliminate S22 GATT stall.
