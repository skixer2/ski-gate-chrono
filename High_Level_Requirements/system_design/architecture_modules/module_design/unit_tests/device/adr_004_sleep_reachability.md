# ADR-004: Sleep Reachability, IDLE Removal & System Off Strategy

**Date:** 2026-08-19
**Status:** Proposed (revised — IDLE state removed)
**Depends on:** ADR-001 (Always-JSON)

## Context

### Problem

After a ski run completes, the athlete may need up to 30+ minutes to
return to their phone (gondola ride, queue, hike back). The device
currently transitions:

```
LOGGING → POST_RUN (10 s) → IDLE (2 min) → SLEEP (BLE off, dark)
```

Once in SLEEP, the device is **unreachable via BLE** — advertising is
stopped and the link is torn down (`sgc_ble_force_recover("sleep")`).
The phone cannot auto-download runs. The athlete must physically tap
the LDC1612 proximity sensor to wake the device back to IDLE, then wait
for the phone to connect. This violates the core UX requirement:

> **No action from the athlete other than looking at his run results.**

### Current state machine (FW 5.10)

States: `SLEEP`, `IDLE`, `ARMED`, `LOGGING`, `POST_RUN`

- `SLEEP_TIMEOUT_MS = 120000` (2 min in IDLE → SLEEP)
- On SLEEP entry: `sgc_ble_force_recover("sleep")` → BLE stops
  advertising entirely, link torn down
- SLEEP is **System On** (not `sd_power_system_off()`) — main loop
  keeps running, WDT active, `__WFE`/`__WFI` between iterations
- Only wake path: LDC1612 INTB (P0.02) proximity interrupt →
  SLEEP → IDLE. No accelerometer-based wake exists.
- `m_hold_idle` prevents IDLE→SLEEP when phone connected or FT active

### Constraints

1. **ARMED/LOGGING must keep BLE off** — SPI0 bus is saturated (BHI260AP
   sensor + MX25R flash share SPI0). BLE polling steals cycles and risks
   bus race. No change to ADV policy in these states.
2. **BHI260AP + MX25R share SPI0** — no concurrent radio + flash access
   during LOGGING. This is a hardware constraint, not policy.
3. **Device must remain reachable for 30+ min after run end** with no
   user interaction.
4. **Battery budget** — 400 mAh cell, 10 runs/day target. Sleep
   advertising overhead must be <1% of daily energy.
5. **All nRF52833 GPIO pins (P0.00–P0.31) are wake-capable from System
   Off** via PIN_CNF.SENSE + DETECT signal. Verified for P0.14
   (BHI260AP INT) and P0.02 (LDC1612 INTB).

## Decision

### 1. Remove IDLE state

The IDLE state served no purpose distinct from SLEEP once SLEEP
advertises. **IDLE is eliminated.** The state machine becomes:

```
SLEEP ⇄ ARMED → LOGGING → POST_RUN → SLEEP
```

Transitions:
- `POST_RUN` → (10 s cooldown) → `SLEEP` (direct, no IDLE)
- `SLEEP` → (LDC proximity tap) → `ARMED`
- `SLEEP` → (phone connects) → download → `SYSTEM_OFF`
- `SLEEP` → (1 h, no phone) → `SYSTEM_OFF`
- `SYSTEM_OFF` → (LDC tap or BHI260 motion) → `SLEEP` (boot)

`SLEEP_TIMEOUT_MS` and the IDLE→SLEEP timeout are removed. POST_RUN
goes directly to SLEEP after the 10 s cooldown.

The `m_hold_idle` flag becomes `m_hold_sleep` — prevents SLEEP→SYSTEM_OFF
while phone is connected or FT is active.

### 2. SLEEP = low-rate advertising + sensors warm

SLEEP is the device's primary "waiting" state:

| Property | SLEEP |
|---|---|
| BLE advertising | Yes, **2 s interval** (~10 µA avg) |
| LDC1612 proximity | Active (tap → ARMED) |
| BHY260AP sensors | Active (monitoring, ready for start detector) |
| Main loop | Running, WFE/WFI between iterations |
| WDT | Active |
| `state` char on BLE | Reports SLEEP |

Power: ~10 µA avg at 2 s ADV interval. 30 min ≈ 0.005 mAh — negligible
vs. one ski run (~0.04–0.08 mAh for 30 s at 5–10 mA).

### 3. System Off after 1 h with no phone

After 1 h in SLEEP with no phone connection, enter **System Off**
(`sd_power_system_off()`). This is the "athlete went to lunch" path.

```
SLEEP ──(1 h, no phone)──→ SYSTEM_OFF
     ──(phone connects)──→ download ALL → SYSTEM_OFF
```

System Off current: <0.5 µA (radio + CPU off, only GPIO SENSE active).

`SLEEP_SYSTEM_OFF_MS = 3600000` (1 h).

### 4. Wake from System Off → SLEEP, then conditionally auto-ARM

Two wake sources, both via GPIO SENSE, both wake to **SLEEP** for init.
After init completes, behavior diverges based on wake source:

**a) LDC1612 INTB (P0.02) — deliberate tap / push button**
- Athlete taps device → LDC INTB fires → nRF52 wakes (full reset)
- Boot → SLEEP (sensors warm up, BLE init, pre-roll prep, ADV starts)
- **After init complete → auto-transition to ARMED**
- The LDC tap is a deliberate athlete action ("I'm at the gate, ready
  to ski"). No second tap needed.

**b) BHI260AP INT (P0.14) — accelerometer motion (accidental)**
- BHI260AP any-motion / significant-motion virtual sensor configured
  before System Off. BHI260AP stays powered (VCC doesn't drop) and
  monitors independently.
- On motion above threshold → INT pulse on P0.14 → nRF52 wakes (full reset)
- Boot → SLEEP (same warm-up path as LDC wake)
- **After init complete → stays SLEEP** (waits for deliberate LDC tap
  to ARM). The accelerometer wake could be accidental (backpack, jacket,
  gondola) — requires confirmation via LDC tap.

**Wake source detection:** `NRF_GPIO->LATCH` register read at boot
identifies which pin triggered the wake (P0.02 bit = LDC, P0.14 bit =
BHI260). Stored as `g_wake_source` for the init sequence to act on.

**Why SLEEP first, not direct to ARMED:** the device needs:
- BHI260AP boot (~50 ms)
- LDC1612 recalibration (baseline)
- BLE stack init + advertising start
- Pre-roll preparation

These complete in <1 s. After init, if `g_wake_source == LDC`, the
device auto-transitions SLEEP → ARMED. If `g_wake_source == BHI260`,
it stays in SLEEP until a deliberate LDC tap.

The athlete's natural flows:
- **At the gate:** tap device → wakes to SLEEP → auto-ARMED → ski → RUN
- **Accidental motion:** device wakes to SLEEP → stays SLEEP → athlete
  taps when ready → ARMED → ski → RUN

### 5. Accelerometer wake threshold — user-settable

The BHI260AP motion threshold must be **configurable by the athlete**.
Kids and adults have different motion profiles.

**BLE characteristic (production):**
- New `wake_threshold` char (uint8, 0–255)
- App exposes a slider: "Wake sensitivity" (Low / Medium / High, or numeric)
- Device stores in flash config sector (survives power cycle)
- Default: medium (BHI260AP significant-motion default)

**Serial command (bench/test):**
- `w <value>` — set wake threshold immediately
- `w` alone — read current value
- Stored in config alongside other calibration values

**BHI260AP mapping:** The exact register/parameter depends on the
virtual sensor type (any-motion vs significant-motion). T6 in the task
list covers BHY2 library investigation. If the library doesn't expose
threshold configuration, direct BHI260AP register writes.

### 6. Download all pending runs

Current FT protocol downloads one run at a time. New behavior:
phone app loops over all runs not yet transferred:

1. Phone reads `?` status → `total_runs`, `runs` (untransferred count)
2. For each untransferred run: FT download → CRC verify → `dl_confirmed`
3. Device marks run as downloaded (flag, don't erase — slot reused
   on next `prepare_next_run` cycle)
4. After all runs confirmed → phone sends `dl_all_confirmed`
5. Device → System Off

## State machine (final)

```
SYSTEM_OFF
  │
  ├──(LDC tap)──────────→ SLEEP ──(init done)──→ ARMED → LOGGING → POST_RUN (10s) → SLEEP
  │                         │                                                      ↑
  │                         ├──(phone connects)── download ALL → SYSTEM_OFF        │
  │                         │                                                      │
  │                         ├──(1 h, no phone)──→ SYSTEM_OFF                       │
  │                         │                                                      │
  │                         └──(LDC tap)──→ ARMED ─────────────────────────────────┘
  │
  └──(BHI260 motion)────→ SLEEP ──(init done, stays SLEEP)──→ (waits for LDC tap)
```

## Power budget summary

| State | Current | 30 min cost | 1 h cost |
|---|---|---|---|
| SLEEP (ADV 2 s) | ~10 µA | 0.005 mAh | 0.01 mAh |
| SYSTEM_OFF | <0.5 µA | ~0 mAh | ~0 mAh |
| LOGGING (30 s run) | ~5–10 mA | 0.04–0.08 mAh | — |

Daily estimate (10 runs, 30 min SLEEP between runs, System Off
overnight): ~0.8 mAh runs + ~0.05 mAh SLEEP ADV ≈ 0.85 mAh/day.
400 mAh cell → **~470 days standby** (ignoring self-discharge).

## Hardware verification

- **P0.14 (BHI260AP INT):** wake-capable ✓ (all P0.00–P0.31 support SENSE)
- **P0.02 (LDC1612 INTB):** wake-capable ✓ (already used for SLEEP wake)
- **BHI260AP power in System Off:** VCC stays on (nRF52 System Off
  doesn't cut peripheral power). BHI260AP runs independently with its
  own firmware — can generate INT from wake-up virtual sensors.
- **SPI bus in System Off:** SPI controller (nRF52) is off. BHI260AP
  cannot be polled but doesn't need to be — it operates autonomously
  and signals via INT pin.

## S03 integrity bug fixes (bundled)

Two S03 stream integrity issues are addressed alongside this ADR:

### S03-A: Stream escape regression (FW 5.04 → fixed 5.05)

**Root cause:** `handle_serial_stream_escape()` was added in 5.04 to peek
UART during stream mode and treat bytes `i` / `s` / `p` as commands.
During S03, the PC sends raw 16-byte frames whose payload bytes are
often `0x69/0x73/0x70` (inside quaternion/lin_acc fields). Stealing even
one byte desyncs the 16-byte pull parser → garbage baro → 0% integrity.

**Fix (already applied in 5.05):** removed the single-byte stream
escape entirely. Stream mode exits only via end detector, ARM_TIMEOUT,
POST_RUN, or state transitions. No `handle_serial()` while streaming.

**Status:** ✅ Fixed. No code change needed. Documented here for
completeness.

### S03-B: D-003 — S03 dump `bad_id` intermittent (open)

**Symptom:** S03 data integrity check requests `h <id> raw` (hex dump).
Intermittently the device returns `hex_err: bad_id` — the run ID doesn't
match what the host expects. This is a host-side parse race: the `h`
command args arrive but are parsed before the full line is assembled.

**Current mitigation:** SIM 2.50.0 retries the `h` command on `bad_id`.
This masks the symptom but doesn't fix the root cause.

**Root cause:** The `h` command arg parser in firmware uses a short wait
(50 ms historically, 500 ms since 4.79) for the hex dump args. Under
heavy serial load (post-stream, POST_RUN → IDLE transitions), the args
may arrive after the wait window closes → `no_args` / `bad_id`.

**Fix (bundled with this ADR):**
- Make `h` command arg parsing **line-oriented** (wait for `\n`, not a
  fixed timeout) — same pattern as the 4.91/4.92 `handle_serial` fix
  for U19 inject flake
- Increase robustness: if args missing, emit a JSON error immediately
  and let the host retry (no silent failure)
- Add S03 `bad_id` regression test to the test harness

**Task:** T19 in the implementation list below.

1. **BHI260AP wake-up sensor configuration** — need to verify that
   Arduino_BHY2 supports configuring any-motion / significant-motion
   virtual sensors that stay active when the host MCU is off. If the
   BHY2 library tears down sensors on `end()`, we may need to bypass it
   and configure the BHI260AP directly via register writes before
   System Off.
2. **False wakes from backpack motion** — threshold must be tuned per
   user. The settable threshold (BLE + serial) mitigates this. If
   false wakes are still excessive even at high threshold, fall back
   to LDC-only wake and drop the accelerometer path.
3. **iOS background scan reliability** — `CBCentralManager` state
   restoration with known service UUID should relaunch the app when
   the peripheral appears. Worth a phone-side spike to de-risk.
4. **`dl_confirmed` protocol** — device marks run as downloaded (flag),
   does not erase. Slot gets erased on next `prepare_next_run` cycle.
5. **IDLE removal impact** — current firmware has IDLE in many code
   paths (state checks, BLE policy, LDC arming). Need careful audit of
   all `DeviceState::IDLE` references and remap to SLEEP.

## Implementation tasks

| # | Task | Files | Depends |
|---|------|-------|---------|
| T1 | Remove IDLE state — remap all IDLE references to SLEEP | state_machine.h/cpp, main.cpp, sgc_service.cpp, all references | — |
| T2 | POST_RUN → SLEEP direct (remove IDLE transition) | state_machine.cpp, config.h | T1 |
| T3 | SLEEP: slow ADV (2 s interval) instead of force_recover | sgc_service.cpp | T1 |
| T4 | Add `SLEEP_SYSTEM_OFF_MS = 3600000` + timeout in `check_timeouts()` | state_machine.cpp, config.h | T2 |
| T5 | `m_hold_idle` → `m_hold_sleep` (prevent SLEEP→OFF while connected) | state_machine.h/cpp | T1 |
| T6 | System Off entry: `sd_power_system_off()` + GPIO SENSE on P0.02 + P0.14 | main.cpp | T4 |
| T7 | Boot: read `NRF_GPIO->LATCH` + `RESETREAS` to identify wake source (LDC vs BHI260); store as `g_wake_source` | main.cpp | T6 |
| T8 | Boot init sequence: after sensors + BLE + pre-roll ready, if `g_wake_source == LDC` → auto-transition SLEEP → ARMED. If BHI260 → stay SLEEP. | main.cpp, state_machine | T7 |
| T9 | BHI260AP: configure any-motion virtual sensor before System Off | bhy2 init, config | T6 |
| T10 | Verify BHY2 library supports wake-up sensor config (or direct reg path) | investigation | T9 |
| T11 | BLE: `wake_threshold` characteristic (uint8, flash-persisted) | sgc_service.cpp, config sector | T1 |
| T12 | Serial: `w <value>` command to read/set wake threshold | main.cpp | T11 |
| T13 | BLE: `dl_all_confirmed` command → device → System Off | sgc_service.cpp | T3 |
| T14 | Phone app: download loop for all pending runs | app (Flutter) | T13 |
| T15 | Phone app: `dl_all_confirmed` → device → System Off | app + firmware | T13, T14 |
| T16 | Phone app: background BLE scan (Android fg service + iOS state restoration) | app | T3 |
| T17 | Phone app: wake threshold slider → BLE char write | app | T11 |
| T18 | Integration test: full flow (LDC wake → auto-arm → run → sleep → phone download → off; BHI260 wake → sleep → LDC tap → arm → run) | test harness | T1–T17 |
| T19 | Fix D-003: `h` command arg parsing → line-oriented (wait for `\n`, not timeout); add S03 `bad_id` regression test | main.cpp, test harness | T1 |
