# SGC — System Design (v2.1)

*2026-06-14 — v2.1: Battery budget cleanup — removed RFID/UWB hardware subsections (already in devices architecture §12). Power states table now v1-only with footnote for RFID. H02 confirmed met without RFID. RFIDReader interface condensed with v2-only banner.*
*2026-06-09 — v2.0: Architecture pivot — no RFID for v1 (unpopulated footprint). Dual start detection (speed OR drop). RFID removed from power budget. See sgc_architecture_decisions.md for pivot rationale.*
*2026-06-08 — v1.9: Cross-arm proximity arming — replaced "button press" with "cross-arm proximity" in state transition table and factory reset description. LED states updated to match 5× strip sequential animation (flowing point / chase patterns).*

*2026-06-06 — Coherence fixes (v1.x, partially superseded): GateTimeEstimator kinematics pipeline. F54/F55 cross-refs now marked v2 only.*

*2026-06-06 — Replaced MissedGateEstimator + GateClassifier with unified GateTimeEstimator: kinematics-driven pipeline (ω zeros, 0.5 Hz LPF, local-frame L/R, Case A/B time estimation).*

*2026-06-05 — coherence check fixes: corrected stale F41→DB schema cross-refs, added LED column to battery table header, EndDetector ±0.3 m/s threshold, FlashManager CRC validation + corrupt-run handling (R05), bonding-loss risk, phone module traceability tags (F15/F27/F49/F51/F59).*

---

## 1. Device State Machine

```
                         ┌──────────────────────────────────────┐
                         │           SLEEP                     │
                         │  nRF52: System ON sleep (WFE)       │
                         │         RTC running, RAM retained   │
                         │         ~3 µA                       │
                         │  LDC1612: 10 Hz poll (~50 µA)       │
                         │  BMP390: OFF                        │
                         │  BHI260AP: OFF                      │
                         │  Total sleep: ~53 µA                │
                         └──────────┬──────────────────────────┘
                                    │ LDC1612 INTB → GPIO interrupt (F13)
                                    ▼
                         ┌──────────────────────────────────────┐
                         │              IDLE                    │
                         │  nRF52: awake, BLE advertising      │
                         │  LDC1612: continuous monitor        │
                         │  Sensors: initialized, not logging  │
                         │  Cal: accuracy < 2 → slow blue blink│
                         │  Cal: accuracy ≥ 2 → solid blue     │
                         │  Timeout → SLEEP after 5 min        │
                         └──────────┬──────────────────────────┘
                                    │ 1000 ms proximity AND accuracy ≥ 2 (F03, F51, P05)
                                    ▼
                         ┌──────────────────────────────────────┐
                         │             ARMED                   │
                         │  Phase 1: fill (5s → 500 samples)  │
                         │  Phase 2: freshen (pop 1, push 1)  │
                         │  LED: green (F41)                   │
                         │  Beeper ON (F14)                    │
                         │  BMP390: monitoring vertical speed  │
                         │  30 s timeout → IDLE (R02)          │
                         └──────────┬──────────────────────────┘
                                    │ Barometric descent > 1.5 m/s (F04)
                                    ▼
                         ┌──────────────────────────────────────┐
                         │            LOGGING                  │
                         │  push 1 live → pop 2 (or 1) → Flash│
                         │  (same ring buffer, always)         │
                         │  UHF RFID: ⚠️ v2 only (unpopulated) │
                         │  LED: red (F41)                     │
                         │  LDC1612: masked (R03)              │
                         │  Beeper: OFF                        │
                         │  10 s flatline + stillness → POST    │
                         └──────────┬──────────────────────────┘
                                    │ Auto-terminate (F06)
                                    ▼
                         ┌──────────────────────────────────────┐
                         │           POST_RUN                  │
                         │  File closed, CRC32 written         │
                         │  BLE: advertise updated run count   │
                         │  Re-arm allowed after 2 s cooldown  │
                         │  5 min inactivity → SLEEP           │
                         └──────────────────────────────────────┘

    ┌─ LOW_BATTERY (R04) ──────────────────────────────────────┐
    │  Any state → VBAT < 3.3V → close file → SLEEP            │
    └──────────────────────────────────────────────────────────┘

    ┌─ CHARGING (H10) ─────────────────────────────────────────┐
    │  Qi power detected (any state):                          │
    │    → If LOGGING: continue logging (charging non-blocking)│
    │    → BLE advertising + GATT operations allowed           │
    │    → ARMED / LOGGING transitions allowed as normal       │
    │    → Charging status notified via GATT (...ABCF)         │
    │    → No separate CHARGING state — Qi is a side condition │
    └──────────────────────────────────────────────────────────┘

    ┌─ FACTORY RESET (F42) ────────────────────────────────────┐
    │  20 s continuous inductive hold (IDLE):                  │
    │    → LED flashes red 3×                                  │
    │    → Clear BLE bonding                                   │
    │    → Reset device name to default                        │
    │    → Erase all run data from Flash                       │
    │    → Restart                                             │
    └──────────────────────────────────────────────────────────┘
```

### State Transition Table

| From | Trigger | To | Notes |
|---|---|---|---|
| SLEEP | LDC1612 INTB (cross-arm proximity) | IDLE | Wake to IDLE (F13). RTC preserved — no time lost |
| IDLE | 20 s continuous inductive hold | (factory reset) | LED flashes red 3× → clear bonding + Flash → restart (F42) |
| IDLE | 1000 ms continuous proximity **AND** BHI260AP accuracy ≥ 2 | ARMED | Ring buffer starts, LED=green chase, beeper on (F03, F14, F51, P05). If accuracy < 2: ignore (arming refused, no LED change) |
| IDLE | 5 min no arming, no BLE connection | SLEEP | F12 |
| ARMED | Barometric descent > 1.5 m/s for 200 ms **OR** cumulative vertical drop > 2.0 m from arming P₀ | LOGGING | Drain begins, LED=red chase (F04 dual-mode, F05, F41). Whichever fires first |
| ARMED | 30 s no descent | IDLE | R02 |
| LOGGING | 10 s flatline (±0.3 m/s) + IMU stillness | POST_RUN | Close file, write CRC32 (F06) |
| POST_RUN | 2 s cooldown elapsed | IDLE | Ready for next arming |
| POST_RUN | 5 min inactivity | SLEEP | F12 |
| SLEEP | LDC1612 INTB (cross-arm proximity) | IDLE | Wake to IDLE (F13). RTC preserved — no time lost |
| * | VBAT < 3.3V | LOW_BATTERY → SLEEP | Close file first (R04) |
| * | BLE disconnect during transfer | (resume on reconnect) | R06 |
| * | Qi power detected | (unchanged) | Charging status notified via GATT. No state change — Qi is a side condition. Arming/logging proceed normally while charging. |

---

## 2. Ring-Buffer Drain Algorithm (F05)

The ring buffer operates in three phases:

**Phase 1 — Filling (first 5.0 s after arming):** Push 1 sample per cycle until 500 samples accumulated. No pops.

**Phase 2 — Freshening (after full, waiting for start):** Pop 1 oldest (discard), push 1 new. Count stays at 500. Buffer always contains the most recent 5.0 s of data. The athlete may stand at the start gate up to 30 s (R02 timeout) — the buffer keeps refreshing.

**Phase 3 — Draining + logging (start confirmed):** The critical handoff from ARMED → LOGGING. **Pop first, push second** — always. Phase 3a (drain, cycles 1–500): pop 2 oldest, push 1 new. First 250 cycles flush the 500 pre-start frames; next 250 cycles flush the 250 live frames accumulated during the drain. Phase 3b (steady state, cycle 501+): pop 1 oldest, push 1 new. That's it — no mode switch, no separate buffer.

```
ARMED:  buffer = [h₁, h₂, h₃, ..., h₅₀₀]    (500 pre-start)

LOGGING cycle 1:  pop h₁, h₂ → Flash.  push l₁.   buffer = [h₃, h₄, ..., h₅₀₀, l₁]
LOGGING cycle 2:  pop h₃, h₄ → Flash.  push l₂.   buffer = [h₅, ..., h₅₀₀, l₁, l₂]
...
LOGGING cycle 250: pop h₄₉₉, h₅₀₀ → Flash.  push l₂₅₀.   buffer = [l₁, ..., l₂₅₀]
LOGGING cycle 251: pop l₁, l₂ → Flash.  push l₂₅₁.         buffer = [l₃, ..., l₂₅₁]
...
LOGGING cycle 500: pop l₄₉₇, l₄₉₈ → Flash.  push l₅₀₀.  buffer = [l₄₉₉, l₅₀₀]
LOGGING cycle 501: pop l₄₉₉ → Flash.  push l₅₀₁.          buffer = [l₅₀₀, l₅₀₁]
...
```

**Key insight:** Pre-start samples sit at the bottom of the queue, so they're popped first. Live samples enter at the top and work their way down. Flash naturally gets chronological order: all h's, then all l's — with zero effort. Buffer count stays ≤ 500 at all times (pop always precedes push).

**Pop-2 during drain (500 cycles = 5.0 s), pop-1 after:** First 250 cycles flush the 500 pre-start frames at 2× speed. Next 250 cycles flush the 250 live frames accumulated during the drain. After 500 cycles total, the buffer is near-empty and the code switches to pop-1 (steady state). Pop-2 duration = 5.0 s from start confirmation.

```
Cycle (every 10 ms, always pop-first):
┌─────────────────────────────────────────────┐
│ 1. Read 1 live sample from BHI260AP FIFO    │
│ 2. Pop N oldest from ring buffer            │
│    (N = 2 during drain, 1 after)            │
│ 3. Bit-pack popped sample(s) → page buffer  │
│ 4. If page buffer full → SPI Flash write    │
│ 5. Push live sample to ring buffer          │
│ 6. Update prev_frame from live sample       │
└─────────────────────────────────────────────┘
```

**Flash layout:** `h₁ h₂ … h₅₀₀ | l₁ l₂ l₃ …` — chronological, trivial to parse and recover.

**Buffered page writes:** ~9.25 bytes/frame compressed. 256-byte page buffer fills every ~27 frames. At 2 frames/cycle (most of the time) → page write every ~135 ms. Page write ≤ 3 ms, worst-case duty cycle ~2.2%.

**Timing budget per 10 ms cycle:**

| Step | Max time |
|---|---|
| I²C read from BHI260AP FIFO | ~200 µs |
| Ring buffer push × 1 | ~5 µs |
| Ring buffer pop × 2 (or × 1) | ~10 µs |
| Bit-packing × 2 (or × 1) | ~200 µs |
| Append to page buffer | ~10 µs |
| SPI Flash page write (amortized) | ~230 µs |
| **Total** | **~655 µs** |
| **Margin (100 Hz = 10 ms)** | **~9.3 ms** |

**Ring buffer implementation:**
- 500 slots × 16 bytes = 8 KB (fits in nRF52832's 64 KB RAM). See sgc_architecture_devices.md §2 for RawFrame struct — the 4B encoding overhead is added at Flash-write time, not stored in RAM.
- Single buffer serves as both pre-start cache and live FIFO
- ARMING: samples pushed to top until `count` = 500
- LOGGING: every cycle, pop N oldest from bottom (N=2 during drain, N=1 after) → Flash, then push 1 to top
- `prev_frame` (20 bytes) kept alongside for delta encoding reference

---

## 3. Adaptive Bit-Packing Format (F07)

### Run File Header

Each run file begins with a 16-byte header before the compressed frame stream:

```
Byte 0:    Format version (uint8) — currently 1. Incremented when bit-packing
           format changes. Phone uses this for backward compatibility (DB schema: runs.format_version).
Byte 1:    Arm side (uint8) — 0=left, 1=right
Byte 2–5:  Run start UTC timestamp (uint32, unixtime) — from phone-synced RTC
Byte 6–7:  Barometric temperature at run start (int16, tenths of °C)
Byte 8–9:  Compressed data size in bytes (uint16, excludes header)
Byte 10–11:Uncompressed frame count (uint16, number of frames)
Byte 12–15: Reserved (uint32, zero-filled). CRC32 of compressed data is stored
           at end of run file (see §5 Run File on-disk format).
```

### Frame Structure

Each frame stores a **time delta** from the previous frame (not an absolute timestamp). The run start time is written once in the run file header. Packet type and sequence counter are embedded in the delta field — no separate header byte.

```
Bytes 0–1: Time delta (uint16, milliseconds)
           Bits 15-14: Packet Type (TT): 00=Type1, 01=Type2, 10=Type3, 11=reserved
           Bits 13-10: Sequence counter (CCCC): 0–15, wraps
           Bits 9-0:   Delta in ms (0–1023 ms)
           Typical: 10 = 10 ms (100 Hz cadence)
           If delta exceeds 1023 ms (device paused/resumed, rare):
           frame forced to Type 3 with full 16-bit delta in payload.

Bytes 2–3: Barometric pressure (uint16, Pa/4) — ALWAYS uncompressed
           → resolution ~2.5 cm altitude
           → 4 bytes anchor total (delta + baro, header embedded in delta)

Barometric temperature is NOT stored per-frame. It is sampled once at run start and written to the run file header (queryable via BLE GATT). Temperature changes slowly — per-frame storage would waste ~12 KB per 2-min run for negligible value.

Bytes 8+:  Payload (variable, see below)
```

### Payload by Packet Type

#### Type 1 — Coasting (4-bit deltas)
**Condition:** All 7 axes' deltas fit in ±7 (4-bit signed, range -8..+7)

| Field | Bits | Notes |
|---|---|---|
| q_w delta | 4 | int4 |
| q_x delta | 4 | int4 |
| q_y delta | 4 | int4 |
| q_z delta | 4 | int4 |
| la_x delta | 4 | int4 |
| la_y delta | 4 | int4 |
| la_z delta | 4 | int4 |
| **Total payload** | **28 bits = 3.5 bytes** | |
| **Frame total** | **8 bytes stored** *(7.5 useful)* | 4 (anchor) + 3.5 (padded to 4) |

#### Type 2 — Turning/Chatter (8-bit deltas)
**Condition:** All 7 axes' deltas fit in ±127 (int8)

| Field | Bits | Notes |
|---|---|---|
| q_w delta | 8 | int8 |
| q_x delta | 8 | int8 |
| q_y delta | 8 | int8 |
| q_z delta | 8 | int8 |
| la_x delta | 8 | int8 |
| la_y delta | 8 | int8 |
| la_z delta | 8 | int8 |
| **Total payload** | **56 bits = 7 bytes** | |
| **Frame total** | **11 bytes** | 4 (anchor) + 7 |

#### Type 3 — Impact / Periodic Anchor (full 16-bit)
**Condition:** Any delta overflows int8, or every 100th frame (forced anchor)

| Field | Bits | Notes |
|---|---|---|
| q_w | 16 | int16, full value (not a delta) |
| q_x | 16 | int16 |
| q_y | 16 | int16 |
| q_z | 16 | int16 |
| la_x | 16 | int16 |
| la_y | 16 | int16 |
| la_z | 16 | int16 |
| **Total payload** | **112 bits = 14 bytes** | |
| **Frame total** | **18 bytes** | 4 (anchor) + 14 |

### Expected Compression

| Packet type | % of frames | Bytes each | Weighted |
|---|---|---|---|
| Type 1 (coasting) | ~60% | 7.5 | 4.50 |
| Type 2 (turning) | ~35% | 11.0 | 3.85 |
| Type 3 (impact/anchor) | ~5% | 18.0 | 0.90 |
| **Average** | | | **9.25 bytes/frame** |

vs. raw 20 bytes/frame → **53.8% compression**. 2-min DH run (worst case) → ~108 KB. Typical training run (40s GS) → ~43 KB. 2 MB Flash → 19 (DH) to 46 (training) runs.

### Delta Encoding Rules

- **Type 1 / Type 2:** Each value = `previous_frame_value + delta`. Decoder reconstructs absolute values by accumulating deltas from the last Type 3 anchor.
- **Type 3 (forced anchor every 100 frames = 1 Hz):** Full 16-bit absolute values. Resets the delta accumulator. Limits error propagation to 1 second max.
- **First frame of every run:** Always Type 3.

---

## 4. BLE GATT Service Definition

### Connection Handshake

On every BLE connection, the phone pushes UTC time to the device **before any other GATT operation**. The device's RTC survives sleep (System ON with RAM retention), but the 32 kHz crystal drifts at ±20 ppm (~1.7 s/day). Syncing on each connection eliminates cumulative drift.

**BLE parameters:**
- PHY: LE 2M preferred, fall back to LE 1M (P08)
- ATT MTU: negotiated to ≥ 247 bytes on connect (enables 244-byte file chunks) (F38)
- Security: LE Secure Connections, Just Works pairing. Bond on first connection, encrypt all subsequent sessions (F39)

**Pairing & factory reset (F42):**
- First connection: phone initiates bonding → bond stored in nRF52 RAM (persists across System ON sleep)
- Factory reset: athlete holds cross-arm proximity for 20 continuous seconds → LED flashes red 3× → device clears bonding info, resets name to default, erases all Flash run data, restarts

```
Phone                           Device
  │                                │
  │──── Connect ──────────────────>│
  │<─── Security request ──────────│
  │──── Pair (Just Works) ────────>│
  │<─── Bonded, encryption ON ─────│
  │                                │
  │──── Write Current Time ───────>│  (UTC unixtime, uint32)
  │<─── Ack ──────────────────────│
  │                                │
  │  (now normal operations)       │
```

**RTC drift budget:**
```
32 kHz crystal, ±20 ppm → ±1.73 s/day worst case
1 week without phone: ±12 s drift
Syncing on every connection → drift reset to 0 each time
```

### Primary Service

**SGC Service UUID:** `53470000-0000-1000-8000-00805F9B34FB`

All SGC characteristics derive from this base UUID via 16-bit aliases. A characteristic with alias `0xABCD` has full UUID `5347ABCD-0000-1000-8000-00805F9B34FB` (the 16-bit alias replaces bytes 2–3 of the base UUID per Bluetooth Core Spec v5, Vol 3, Part F, §3.2.1).

### Characteristics

| Handle | UUID | Name | Type | Size | Description |
|---|---|---|---|---|---|
| — | `...ABC0` | Current Time | W | 4 B uint32 | Phone writes UTC unixtime on every connect, before any other operation (F37) |
| — | `...ABC1` | Device Name | R/W | ≤20 B UTF-8 | User-settable name (F11) |
| — | `...ABC2` | Arm Side | R/W | 1 B uint8 | 0=left, 1=right (F11) |
| — | `...ABC3` | Mount Type / Discipline | R/W | 1 B uint8 | Bits 3-0: mount (0=arm, 1=pole). Bits 7-4: discipline (0=SL, 1=GS, 2=SG, 3=DH). Discipline drives RFID polling rate (F53) |
| — | `...ABC4` | Device State | R/N | 1 B uint8 | 0=idle, 1=armed, 2=logging, 3=post_run, 4=setup. SLEEP has no BLE value (BLE is off during sleep) |
| — | `...ABC5` | Battery Level | R | 1 B uint8 | 0–100% |
| — | `...ABCF` | Charging Status | R/N | 1 B uint8 | 0=not charging, 1=charging, 2=charged (Qi) |
| — | `...ABC6` | Run Count | R/N | 2 B uint16 | Total runs stored (F09) |
| — | `...ABC7` | Flash Used % | R/N | 1 B uint8 | 0–100 (F09) |
| — | `...ABC8` | Oldest Run Age | R | 4 B uint32 | RTC timestamp of oldest run (F09) |
| — | `...ABC9` | Run List | R | variable | JSON array of run metadata (F09) |
| — | `...ABCA` | File Transfer Request | W | 2 B uint16 | Run ID to download (F10) |
| — | `...ABCB` | File Transfer Chunk | N | ≤244 B | Chunk of compressed run file (F10) |
| — | `...ABCC` | File Transfer CRC | R | 4 B uint32 | CRC32 of transferred file (F10) |
| — | `...ABCD` | File Transfer Status | R/N | 1 B uint8 | 0=idle, 1=transferring, 2=complete, 3=error |
| — | `...ABCE` | Sensor Status | R | 1 B uint8 | Bitfield: BHI, BMP, LDC, Flash OK flags (R07) |
| — | `...ABD0` | Calibration Accuracy | R/N | 1 B uint8 | BHI260AP fusion accuracy: 0=unreliable, 1=low, 2=medium, 3=high. Notifies on change (F41) |
| — (DFU Service) | `00001530-1212-EFDE-1523-785FEABCD123` | Nordic DFU Control Point | W/N | variable | Standard Nordic BLE DFU service for OTA firmware update (F40). Phone pushes signed firmware image; device validates, flashes, and reboots. Corrupted image → rejected, previous version retained |
| — (DFU Service) | `...` | DFU Packet | W (no resp) | ≤20 B | Firmware data packets |

### File Transfer Protocol

```
Phone                           Device
  │                                │
  │──── Read Run List ────────────>│  (discover available runs)
  │<─── JSON: [{id,ts,size,L/R}]──│
  │                                │
  │──── Write File Req (run_id) ──>│  (request specific run)
  │<─── Notify: Status=transferring│
  │                                │
  │<─── Notify: Chunk 1 ──────────│  (244 B chunks)
  │<─── Notify: Chunk 2 ──────────│
  │       ...                      │
  │<─── Notify: Chunk N (last) ────│
  │<─── Notify: Status=complete ───│
  │                                │
  │──── Read CRC ─────────────────>│
  │<─── CRC32 ────────────────────│
  │                                │
  │  Phone verifies CRC.           │
  │  Mismatch → re-request. (R05)  │
  │  Disconnect mid-transfer →     │
  │    resume from last chunk (R06)│
```

---

## 5. Phone Application Architecture (Flutter)

### Module Decomposition

```
lib/
├── main.dart                    # App entry, routing
├── models/
│   ├── run.dart                 # Run metadata + decompressed data
│   ├── gate_timestamp.dart      # Single gate time (real or guessed)
│   ├── barometric_point.dart    # Altitude + speed at 10 Hz
│   ├── device_config.dart       # BLE device parameters, calibration accuracy display (🔴/🟢 indicator per F51), arming prevention when accuracy < 2
│   └── user_profile.dart        # Athlete name, club, group, etc.
├── ble/
│   ├── ble_manager.dart         # BLE scan, connect, disconnect
│   ├── sgc_service.dart         # GATT characteristic read/write/notify. Arm pairing: associates left/right devices by RTC timestamp proximity ±3 s (F15)
│   └── file_transfer.dart       # Chunked download with CRC + resume
├── processing/
│   ├── decompressor.dart        # Reverse adaptive bit-packing (F17)
│   ├── impact_detector.dart     # Linear accel spike → gate impact (F18)
│   ├── gate_time_estimator.dart # Kinematics-driven: estimates times for MISSED gates only (no impact/RFID). Hardware impact/RFID timestamps take precedence. ω zeros → local-frame L/R → Case A/B (F26). Replaces MissedGateEstimator + GateClassifier.
│   ├── banana_detector.dart     # Same-side consecutive gates (F28)
│   ├── cross_correlator.dart    # Quaternion dot-product alignment (F16)
│   └── barometric_speed.dart    # Pressure → altitude → vertical speed (F30)
├── ui/
│   ├── run_viewer.dart          # Altitude + speed graphs + gate table (F19, F29)
│   ├── gate_table.dart          # Left/right aligned timestamp table with gate numbers (F44). Estimated (guessed) times displayed with trailing * suffix (F27)
│   ├── graph_widget.dart        # Reusable graph with gate markers (F31, F33)
│   ├── run_comparison.dart      # Side-by-side run comparison, cloud + local runs (F22, F32, F34, F48)
│   ├── run_naming_screen.dart   # User-editable run labels (F45)
│   ├── run_export.dart          # CSV + JSON export with metadata (F47)
│   ├── device_config_screen.dart # BLE device parameter editor (F21)
│   ├── profile_screen.dart      # Athlete profile editor (F20)
│   ├── group_admin_screen.dart  # Group member management + visibility (F35, F36)
│   ├── trainer_dashboard.dart   # Group athlete list + run browser + deletion (F25, F46)
├── setup/
│   └── course_setup.dart         # Pressure + GPS course mapping (v2). Mode A (New Course: sequential recording) / Mode B (Update Existing: GPS+ΔP detection, Move/Delete/Add). Dual view (map ↔ list).
├── cloud/
│   ├── api_client.dart          # HTTPS REST client
│   ├── bootstrap.dart           # Retrieve cloud endpoint from hardcoded URL (F24)
│   ├── sync_manager.dart        # Push timestamps + barometric_data to cloud; offline queue persists across app restarts (F23, F43)
│   └── gdpr_deletion.dart       # Permanent cloud data deletion with explicit warning (F50)
└── storage/
    ├── local_db.dart            # SQLite: run metadata, decompressed data cache
    └── settings.dart            # SharedPreferences: profile, device config
```

### Data Flow

Left and right arms are processed in parallel until merged. Barometric speed (F30) runs on each arm's decompressed frames; the final combined trace is the average of both arms.

```
BLE Download                Decompression              Processing (per arm, then merged)
─────────────              ──────────────             ─────────────────────────────────
Compressed file ──────► Decompressor ──────► 100 Hz frames in memory (each arm)
  (from device)           (F17)                  │
                    ┌────────────────────────────┤
                    │ L arm               R arm  │
                    ▼                      ▼     │
              Impact (F18)           Impact (F18) │
              Barometric (F30)       Barometric (F30)
                    │                      │     │
                    └──────────┬───────────┘     │
                               ▼                 │
                     Cross-correlator (F16)       │
                               │                 │
                     Merge: align + interleave    │
                               │                 │
                     Banana detector (F28)        │
                               │                 │
                     Gate time estimator (F26)    │
                      ω zeros → local frame       │
                      → L/R → Case A/B times      │
                               │                 │
                               ▼                 │
                        Gate timestamps +         │
                        Barometric data (10 Hz)   │
                               │                 │
                    ┌──────────┴──────────┐       │
                    ▼                     ▼       │
               Local SQLite          Cloud (F23)  │
               (full cache)         (ts + baro)   │
```

---

## 6. Cloud API

> ⚠️ **Reference design.** The API contract below defines the interface between the phone app and the cloud backend. Endpoints marked with ⚠️ are placeholders to be finalized during cloud development. The phone app codes against this contract — the cloud backend can be built independently by a different team/stack as long as it implements the same request/response shapes. Detailed endpoints (courses, per-run deletion, user profiles, club management) will be added when the cloud development phase begins. The device and phone app do not depend on a live cloud server.

### Bootstrap (F24)

```
GET https://bootstrap.sgc.example.com/config
→ 302 → https://api-04.sgc.example.com/v1
```

Phone caches the resolved endpoint for 24 h, retries bootstrap on failure.

### Endpoints

| Method | Path | Auth | Description |
|---|---|---|---|
| POST | `/v1/auth/login` | — | Email + password → JWT |
| POST | `/v1/auth/refresh` | Refresh token | → New JWT |
| GET | `/v1/athletes/{id}/runs` | JWT | List runs for athlete (subject to visibility) |
| GET | `/v1/runs/{id}/timestamps` | JWT | Gate timestamps for a run |
| GET | `/v1/runs/{id}/barometric` | JWT | Barometric data (altitude + speed) for a run |
| POST | `/v1/runs` | JWT | Upload new run (timestamps + barometric_data) |
| GET | `/v1/groups/{id}/members` | JWT | List group members |
| PATCH | `/v1/groups/{id}` | JWT (trainer) | Update group default_visibility |
| PATCH | `/v1/runs/{id}` | JWT (trainer) | Override run visibility |
| POST | `/v1/groups/{id}/members` | JWT (trainer) | Add member to group |
| DELETE | `/v1/groups/{id}/members/{uid}` | JWT (trainer) | Remove member |

### Visibility Enforcement (Server-Side)

```
Can user U see run R?
  1. If R.visibility = denied → U must be trainer of R.athlete's group → yes, else no
  2. If R.visibility = athlete_only → U must be:
     a. R.athlete itself, OR
     b. Linked via users2athletes (parent/friend/trainer), OR
     c. Trainer of a group containing R.athlete
     → yes, else no
  3. If R.visibility = full (or null with group default = full) → U must be:
     a. Any of the above, OR
     b. Member of the same group as R.athlete
     → yes, else no
  4. If R.visibility = null → use group.default_visibility, same logic as above
```

### Concurrency Control

All write operations use optimistic locking via `runs.lock_version`:
```
1. Phone reads run record (including lock_version = N)
2. Phone modifies and PUTs with lock_version = N
3. Server: UPDATE ... WHERE id = X AND lock_version = N, SET lock_version = N+1
4. If rows_affected = 0 → conflict (another client wrote first)
   → Phone re-reads the record and retries, or notifies user of conflict
```

For simultaneous uploads by two coaches: last-write-wins with version check. The losing client sees a conflict error and must re-read. This is safe because both uploads contain the same sensor data.

### GDPR Deletion Endpoint

```
DELETE /v1/athletes/{id}  →  permanently deletes all athlete data
  Requires: JWT (athlete or trainer/master of athlete)
  Response: { status: "deleted", runs_deleted: N, timestamps_deleted: M, ... }
  Pre-condition: phone must display explicit warning listing all data to be deleted
  This action is irreversible. No soft-delete. No recovery.
```

---

## 7. Nicla Firmware Module Decomposition

```
src/
├── main.cpp                     # setup() + loop(), state machine dispatch
├── config.h                     # Pin assignments, timing constants, BLE UUIDs
├── sensors/
│   ├── bhi260ap.cpp             # BHI260AP init, FIFO read, fusion config
│   ├── bmp390.cpp               # BMP390 init, 100 Hz pressure reads
│   └── ldc1612.cpp              # LDC1612 init, proximity detection, INTB config
├── storage/
│   ├── ring_buffer.cpp          # 500-sample circular RAM buffer
│   ├── bit_packer.cpp           # Adaptive 3-type delta encoder
│   ├── flash_manager.cpp        # SPI Flash: circular run storage, CRC32
│   └── run_file.cpp             # Per-run file header, metadata, close
├── ble/
│   ├── sgc_service.cpp          # GATT service + all characteristics
│   └── file_transfer.cpp        # Chunked file transfer with CRC
├── state_machine.cpp            # State transitions, timeout timers
├── start_detector.cpp           # Barometric descent detection
├── end_detector.cpp             # Flatline + stillness detection
├── beeper.cpp                   # GPIO PWM → surface transducer (IP67)
├── led.cpp                      # SK6812-mini strip: off=sleep, blue slow flowing=uncalibrated, blue chase=calibrated, green chase=armed, red chase=logging, yellow blink=low battery/error; factory reset = flash red 3× (F41, F42)
├── battery.cpp                  # VBAT monitoring, Qi charging detection, low-battery shutdown
├── rfid_reader.cpp              # ⚠️ v2 only — UHF RFID: SPI control, inventory rounds, RSSI-based nearest-tag (F52–F56). Not compiled in v1.
├── uwb_tag.cpp                   # ⚠️ [NOT v1 — stub only, never built, never linked] DW3000 UWB: GPIO power-gate, SPI init, TDoA blink scheduler. Reserved for potential v2+ when/if UWB snow-cannon infrastructure exists
└── sleep.cpp                    # System ON WFE sleep entry, GPIO interrupt wake. 20s hold detection for factory reset (F42)
```

### Key Module Interfaces

```
RingBuffer
  ├── void push(Frame)           # Write one sample
  ├── Frame pop()                # Read oldest (for Flash drain)
  ├── bool is_full()             # 500 samples reached
  └── void reset()               # Clear on new arming

FlashManager
  ├── int create_run(ArmSide)    # Open new file at write_head, return run_id
  ├── void write_frame(Frame)    # Append compressed frame
  ├── void close_run()           # Write CRC32, finalize, advance write_head
  ├── RunList get_run_list()     # Scan Flash for run headers (read_head → write_head). Validates CRC32 per run; marks corrupt runs as skipped (R05)
  ├── void read_run(int id, *buf)# Read full run file into buffer, verify CRC32 on readback (R05)
  ├── int used_percent()         # Flash fill percentage
  └── int oldest_run_age()       # RTC timestamp of run at read_head

  Circular buffer uses two positional indices:
  ┌──────────────────────────────────────────────────────────┐
  │ 2 MB SPI Flash                                           │
  │ ┌─────────┬──────────┬─────────┬──────────┬─────────┐    │
  │ │  Run A  │  Run B   │  Run C  │  Run D   │  (free) │    │
  │ └─────────┴──────────┴─────────┴──────────┴─────────┘    │
  │     ↑                                     ↑              │
  │  read_head                            write_head          │
  │  (oldest run,                            (next write       │
  │   overwritten                              position)       │
  │    when full)                                              │
  └──────────────────────────────────────────────────────────┘

  - read_head:  points to the oldest run. When Flash is full and a new
                run is created, the run at read_head is overwritten,
                and read_head advances to the next run.
  - write_head: points to the next free position. Advances after
                each close_run().
  - Both wrap around at Flash end (2 MB).
  - Fullness = distance(write_head, read_head) in Flash.
  - get_run_list() walks from read_head to write_head (wrapping if needed).
  - Automatic overwrite: create_run() checks if (write_head + estimated
    run size) would overlap read_head → delete oldest run → advance read_head.
  - Indices stored in a reserved Flash sector (last 4 KB) — survives power loss.

BitPacker
  ├── CompressedFrame encode(Frame current, Frame* prev)
  │                             # Delta-encode, select packet type
  └── Frame decode(CompressedFrame cf, Frame* prev)
                                # Reverse for BLE transfer verification

StartDetector
  ├── void feed_pressure(float pa) # Called every 100 ms
  └── bool descent_detected()      # >1.5 m/s over window

EndDetector
  ├── void feed(float pressure, Quaternion q)
  └── bool run_ended()             # 10s flatline (±0.3 m/s) + IMU stillness (F06)

UWBBlinker  ⚠️ [NOT v1 — interface definition only, never implemented]
  ├── void init()                # Power-gate ON, SPI init, configure DW3000 TDoA mode
  ├── void enable()              # Start blink scheduler (50-100 Hz, discipline-dependent)
  ├── void disable()             # Stop scheduler, power-gate OFF
  ├── void set_blink_rate(uint8_t hz) # 50, 100 Hz per ski discipline
  ├── void attach_imu(uint8_t* data)  # Pack next IMU sample into blink payload (19-31 bytes per UWB context doc)
  └── bool is_present()          # Hardware detected on SPI bus (always returns false in v1: DW3000 not populated)

  → See sgc_uwb_positioning_context.md §4 for TDoA architecture + IMU piggyback.

RFIDReader  ⚠️ v2 ONLY — not compiled in v1 firmware. Included for reference only.
  ├── void init()                # Power up, configure EPC Gen2, tune antenna
  ├── void enable()              # Enable RF field, start inventory rounds
  ├── void disable()             # Disable RF field, power down
  ├── GateEvent poll()           # Run one inventory round, return nearest tag (tag ID + RSSI + timestamp) or null. RSSI-based selection (F54). Gate events logged to Flash with sensor frames (F55).
  └── bool is_present()          # Hardware detected on SPI bus

  Inventory rate set by discipline (F11, F53): SL=5 Hz, GS=10 Hz, SG/DH=20 Hz.
  Single-tag inventory ≤ 15 ms (P09). Duty cycle worst case (DH): 15 ms / 50 ms = 30% → ~24 mA avg. See sgc_architecture_devices.md §12 for full RFID hardware details.
```

### SPI Bus Sharing

The nRF52832 has a single SPI master peripheral (SPIM). Three devices share it via dedicated CSn lines:

| Device | CSn GPIO | Active | Notes |
|---|---|---|---|
| SPI Flash (2 MB) | P0.17 | Always (read/write/erase) | SPI mode 0, up to 8 MHz |
| UHF RFID Reader | P0.18 | LOGGING only | SPI mode 0, power-gated separately |
| DW3000 UWB | P0.19 | NOT POPULATED — reserved footprint | SPI mode 0, power-gated (GPIO on VDD_UWB, default OFF). CSn pad present, never asserted in v1 firmware |

**Arbitration:** SPI bus is single-threaded in the main loop. No DMA contention. Flash writes are buffered (256-byte page → SPI burst every ~135 ms). RFID inventory and (in a hypothetical v2) UWB blink are mutually exclusive within a 10 ms cycle — both complete in < 200 µs each, leaving ≥ 9 ms margin. In v1, only Flash + RFID use the bus. DW3000 CSn is never asserted.

### Timing Architecture

```
mbed::Ticker (100 Hz = 10 ms period, highest priority)
  │
  ├── ISR: Read BHI260AP FIFO watermark
  │         → Copy samples to temporary queue
  │         → Set flag for main loop
  │
  └── (returns in < 200 µs)

main loop (polling)
  │
  ├── if BHI flag set:
  │     ├── ARMED:  push to ring buffer
  │     └── LOGGING: 2× drain + push (see §2)
  │
  ├── every 100 ms:
  │     ├── Read BMP390 pressure
  │     ├── ARMED:  feed StartDetector
  │     └── LOGGING: feed EndDetector
  │
  ├── every 50 ms:
  │     └── Read LDC1612, check 1000 ms continuous threshold
  │
  └── idle:
        └── Process BLE events (ArduinoBLE.poll())

  ├── every 50/100/200 ms (LOGGING only, per discipline: DH/GS/SL):
  │     └── RFIDReader.poll() → if new gate tag detected, emit GateEvent → Flash
  │
  └── ⚠️ [NOT v1] every 10/20 ms (LOGGING, if DW3000 populated in v2):
        └── UWBBlinker.fire() → transmit blink + piggybacked IMU data → TDoA anchors
```

**DW3000 SPI usage (v2 projection, NOT implemented in v1):** If populated, UWB blinks at 50-100 Hz per discipline. Each blink: MCU writes 19-31 byte payload to DW3000 TX buffer (~50 µs SPI), DW3000 auto-transmits (~150 µs). Total SPI utilization: < 2% of cycle. Zero impact on sensor acquisition or Flash write budget.

---

## 8. Battery Budget

### Power States

| State | nRF52 | LDC1612 | BMP390 | BHI260AP | Beeper | BLE TX | LED | Total |
|---|---|---|---|---|---|---|---|
| **SLEEP** (System ON WFE) | ~3 µA | ~50 µA (10 Hz poll) | OFF | OFF | OFF | OFF | OFF | **~53 µA** |
| **IDLE** (BLE advertising) | ~5 mA | ~100 µA (continuous) | ~3 µA (1 Hz) | ~3 mA (idle) | OFF | ~5 mA avg | ~1 mA (LED) | **~14 mA** |
| **ARMED** (ring buffer filling) | ~5 mA | ~100 µA | ~200 µA (100 Hz) | ~5 mA (100 Hz fusion) | ~5 mA | ~5 mA avg | ~1 mA | **~21 mA** |
| **LOGGING** (Flash writes active) | ~8 mA | ~100 µA | ~200 µA | ~5 mA | OFF | OFF | ~1 mA | **~19 mA** (incl. SPI Flash) |
| **POST_RUN** (BLE advertising) | ~5 mA | ~100 µA | ~3 µA | ~3 mA | OFF | ~5 mA avg | ~1 mA | **~14 mA** |

> ⚠️ v2 only: LOGGING + RFID (Flash + UHF RFID at 20% duty cycle) = ~43 mA. RFID hardware is unpopulated in v1. See sgc_architecture_devices.md §12 for full RFID power details.

### Shelf Life (Sleep)

```
Battery: Renata ICP622540PMT, 600 mAh, 3.7V Li-Po
Sleep current: ~53 µA
Shelf life = 600 mAh / 0.053 mA = 11,320 hours ≈ 472 days ≈ 15.7 months

Li-Po self-discharge: ~5%/month
After 6 months: ~70% remaining (self-discharge dominates over circuit draw)
After 12 months: battery effectively depleted by self-discharge
```

**Practical shelf life: ~6-8 months** before recharging is needed. The RTC keeps running as long as the battery has charge — self-discharge, not circuit draw, is the limiting factor. Covers a full ski season (November–April) with margin. No physical disconnect required (enclosure is potted for impact resistance).

### Active Use

```
Reference session: 3 hours, 10 runs (FIS training day)
  - 20 min IDLE/ARMED transitions (~13-20 mA): ~5 mAh
  - 20 min LOGGING (10 runs × 2 min at 19 mA): ~6 mAh
  - 20 min POST_RUN/BLE transfer (~13 mA): ~4 mAh
  - 120 min IDLE between runs (~13 mA): ~26 mAh
  Total per session: ~41 mAh

600 mAh / 41 mAh per session ≈ 14.6 sessions
At −10°C: derate 25% → ~11 sessions
```

### Qi Wireless Charging

Qi receiver coil + rectifier outputs 5V → feeds Nicla's BQ25100 battery charger via VBAT pin. Coil is a thin (~0.8 mm) flexible PCB coil placed on the opposite side of the PCB from the BMM150 magnetometer to minimize magnetic interference (H08). Standard Qi pad (5W) charges the 600 mAh battery from 0% to 100% in ~1.5 hours.

### Flash Wear Leveling

```
2 MB SPI Flash, ~108 KB/run (DH worst case) → ~19 runs per full cycle. Typical 40s training runs (~43 KB) → ~46 runs per cycle.
Typical endurance: 100,000 program/erase cycles per sector
Expected annual usage: ~1,000 runs/year (heavy training + race schedule)
Cycles per year: 1,000 / 19 ≈ 53 full overwrites/year
Years to wear-out: 100,000 / 53 ≈ 1,887 years
```

Flash wear is not a concern for the expected product lifetime. The circular buffer's oldest-run-overwrite policy (F08) naturally distributes writes.

---

## 9. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| SPI Flash write latency > 10 ms → dropped samples | High | Bench-test Flash chip; if marginal, buffer 2–3 samples before SPI write burst |
| BMP390 noise → false start detection | Medium | Low-pass filter pressure (5-sample moving average) before velocity calculation |
| BHI260AP FIFO overflow (FIFO not read fast enough) | High | Watermark at 10 samples → flush every 100 ms. If watermark interrupt missed, BHI260AP drops oldest → flag error |
| LDC1612 false arm from metal ski pole nearby | Low | 1000 ms hold requirement (R01) filters brief proximity; coil tuned for short-range (~3 mm) |
| BLE throughput < 20 KB/s on older phones | Medium | 2-min run = ~108 KB → ~5.4 s at 20 KB/s, well within acceptable range |
| Cold battery derating worse than 20% | Medium | H02 targets 8 h at −10°C with 20% derating; field-test early prototypes in real cold |
| Quaternion cross-correlation fails on very short runs | Low | Single-arm fallback (R08); runs < 10 s skipped for cross-correlation |
| UHF RFID interferes with BMM150 magnetometer | Medium | 900 MHz carrier outside BMM150 sensitivity band. Physical separation > 10 mm + ground plane isolation. Verify with calibration accuracy check |
| UHF RFID module integration on custom PCB (module size, SPI, antenna clearance) | Medium | Impinj E310 module ~25×25 mm SMD. Requires SPI + GPIO enable + 3.3V. Antenna clearance > 10 mm from BMM150. Prototype first with AliExpress TY921 breakout on Nicla breadboard to validate read range and RSSI behavior before committing to module selection for custom PCB |
| Impinj E310 module long-term availability | Low | E310 is actively supported (firmware updates Nov 2025). SILION and Chafon modules widely available. If E310 ever goes EOL, pin-compatible upgrade path exists to Impinj E510/E710/E910. ST ST25RU3993 rejected (confirmed NRND/EOL — research validated June 2026) |
| Gate tag occlusion by snow/ice | Low | UHF penetrates non-conductive materials well. Wet snow may add 1–3 dB attenuation at 900 MHz — negligible at < 1 m range |
| DW3000 footprint unused → wasted PCB area (v1) | Low | DW3000 + antenna keepout occupies ~12×12 mm. Acceptable trade-off for future-proofing. ⚠️ v1 firmware must explicitly NOT touch CSn P0.19 or VDD_UWB GPIO — these pins are reserved and floating/unconnected when DW3000 is unpopulated. If board space becomes critical during layout, footprint can be shrunk to DW3000 QFN only (no antenna) — antenna added in v2 PCB revision |
| DW3000 UWB (v2) interferes with UHF RFID (v2 coexistence) | Low | UWB at 6.5/8 GHz vs. RFID at 900 MHz — 7-9× frequency separation. No intermodulation products in either band. Antenna keepout zones > 10 mm apart. Coexistence verified by BMM150 calibration check (H12) |
| DW3000 UWB (v2) antenna detuning in proximity to athlete's body | Medium | UWB antenna detuning near human tissue is well-characterized. Mitigated by antenna placement on PCB edge facing away from arm, with ground plane isolation. Field-test RSSI and ranging accuracy on-body vs. free-space during v2 prototyping |
| DW3000 UWB (v2) SPI bus contention with Flash + RFID at high UWB rate (100 Hz) | Low | Each UWB blink: 50 µs SPI + 150 µs TX. At 100 Hz = 20 ms total SPI per second = 0.2% utilization. All three devices operate in separate 10 ms cycle windows. Even at worst case, < 500 µs combined SPI per cycle vs. 10 ms budget |
| Total battery drain → BLE bonding lost (no persistent bond storage) | Low | BLE bond stored in nRF52 RAM (retained across System ON sleep). If VBAT drops below LDO cutoff (~2.5V) during prolonged storage, RAM is lost and bonding must be re-paired. Mitigation: phone app detects missing bond + auto-re-pairs on next connection (Just Works, no user intervention). Acceptable — total drain after > 6 months shelf life is expected (self-discharge dominates) |

---

*Next: detailed module_design — ring buffer implementation, bit-packer state machine, BLE service code skeleton.*
