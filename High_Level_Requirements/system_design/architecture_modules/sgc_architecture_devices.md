# SGC — Architecture: Device Firmware (v1.3)

*2026-06-09 — v1.3: Architecture pivot — RFID removed from active BOM. Dual start detector (speed OR drop). rfid_reader marked as v2 stub. See sgc_architecture_decisions.md for pivot rationale.*

*2026-06-08 — v1.2: Renamed button-related identifiers to proximity-based: `button_held_ms` → `proximity_held_ms`, `on_button_press()` → `on_proximity_start()`, `on_button_release()` → `on_proximity_end()`. Updated comments to reflect cross-arm proximity arming.*

*2026-06-06 — Added traceability note: GateTimeEstimator runs on phone, see sgc_architecture_phone.md §7.*

*2026-06-05 — detailed module interfaces, state machines, data flow, SPI arbitration, memory maps. Complements SD §1–9 with implementation-level design.*

---

## 1. Module Dependency Graph

```
                     ┌─────────────┐
                     │  main.cpp   │  ← setup(), loop(), tick dispatch
                     └──────┬──────┘
                            │
         ┌──────────────────┼──────────────────┐
         │                  │                  │
    ┌────▼─────┐      ┌─────▼──────┐     ┌─────▼──────┐
    │ sensors/ │      │ state_     │     │ ble/       │
    │ bhi260ap │      │ machine    │     │ sgc_service│
    │ bmp390   │      │ start_det  │     │ file_xfer  │
    │ ldc1612  │      │ end_det    │     └─────┬──────┘
    └────┬─────┘      │ led        │           │
         │            │ beeper     │           │
         │            │ battery    │           │
         │            └─────┬──────┘           │
         │                  │                  │
    ┌────▼──────────────────▼───────────┐      │
    │         storage/                  │      │
    │  ring_buffer  ←→ bit_packer      │      │
    │  flash_manager                    │      │
    │  run_file      gate_event        │      │
    └───────────────────────────────────┘      │
                                               │
    ┌──────────────────────────────────────────┘
    │
    ▼
  ┌──────────────┐     ┌──────────────┐
  │ rfid_reader  │     │   sleep.cpp  │
  │ (⚠️ v2 only) │     │   (WFE)      │
  └──────────────┘     └──────────────┘
```

**Ownership rules:**
- `main.cpp` owns the state machine and all module instances
- Sensor modules are read-only from `main.cpp`'s perspective — they produce data, don't consume it
- `storage/` modules are called by `main.cpp` during ARMED/LOGGING
- `ble/` modules run in the idle loop, never during LOGGING SPI bursts
- `rfid_reader` is ⚠️ v2 only — called from `main.cpp` during LOGGING when RFID module is populated
- `sleep.cpp` is called by `main.cpp` on inactivity timeout
- **No circular dependencies.** `storage/` never calls `sensors/` or `ble/`

---

## 2. Ring Buffer (`ring_buffer.cpp`)

### Data Structures

```cpp
#define RING_SIZE 500

struct RawFrame {
    int16_t q_w, q_x, q_y, q_z;  // quaternion (Q30 fixed-point) — 8 bytes
    int16_t la_x, la_y, la_z;    // linear acceleration (mm/s²) — 6 bytes
    uint16_t baro_pa_div4;       // barometric pressure (Pa/4) — 2 bytes
    // Total: 16 bytes stored in ring buffer
    // Time delta (2B) and packet type/sequence counter are added by
    // bit_packer at Flash-write time, not stored in ring buffer.
    // SD §2 reference ("20 bytes/frame") includes this 4B encode overhead.
};

struct RingBuffer {
    RawFrame data[RING_SIZE];     // 500 × 16 bytes = 8000 bytes
    uint16_t head;                // write index (next push goes here)
    uint16_t count;               // number of valid frames (0..500)
};
```

**Memory:** 8000 bytes + 4 bytes overhead = ~8 KB (fits in nRF52832's 64 KB RAM with ~8× margin).

### Interface Contract

```cpp
class RingBuffer {
public:
    void push(const RawFrame& frame);
    // Pre:  count < RING_SIZE (caller guarantees space before calling)
    // Post: frame written at data[head], head = (head+1) % RING_SIZE, count++
    //       There is NO silent discard. The caller (main loop) always
    //       pops before pushing during LOGGING, so count never exceeds 500.
    //       A debug assertion fires if push is called when is_full().

    RawFrame pop();
    // Pre:  count > 0
    // Post: returns oldest frame (at index (head - count + RING_SIZE) % RING_SIZE)
    //       count--. If count == 0, behavior is undefined (caller must guard)

    bool is_full() const;
    // Returns count >= RING_SIZE

    bool is_empty() const;
    // Returns count == 0

    uint16_t available() const;
    // Returns count

    void reset();
    // Post: head = 0, count = 0. Called on new arming (IDLE → ARMED)
};
```

### Push/Pop Visualization

```
Initial state (empty):   head=0, count=0
After push(A):           [A, _, _, _, _]  head=1, count=1
After push(B):           [A, B, _, _, _]  head=2, count=2
After push(C):           [A, B, C, _, _]  head=3, count=3
After pop():   → A       [_, B, C, _, _]  head=3, count=2  (oldest at index 3-2=1)
After pop():   → B       [_, _, C, _, _]  head=3, count=1  (oldest at index 3-1=2)
After push(D):           [_, _, C, D, _]  head=4, count=2
After push(E):           [_, _, C, D, E]  head=0, count=3  (wrapped)
After push(F):           [F, _, C, D, E]  head=1, count=4
After pop():   → C       [F, _, _, D, E]  head=1, count=3  (oldest at index (1-3+5)%5=3)
```

**Key invariant:** oldest frame is at `data[(head - count + RING_SIZE) % RING_SIZE]`.

### Integration with Drain Algorithm (SD §2)

```
PHASE 1 — ARMED, filling buffer (first 5.0 s after arming):
  Every cycle: push(live_sample). count grows 0 → 500.

PHASE 2 — ARMED, buffer full, waiting for start confirmation:
  Every cycle: pop(1 oldest) → discard. push(live_sample).
  Count stays at 500. Buffer always contains the most recent 5.0 s.
  The athlete may stand at the start gate up to 30 s (R02 timeout).

PHASE 3a — LOGGING drain (cycles 1–500, 5.0 s from start confirmation):
  Every cycle: pop(2 oldest) → Flash. push(live_sample).
  First 250 cycles: flush all Phase 1+2 pre-start data (500 frames).
  Next 250 cycles: flush the 250 live frames accumulated during the drain.
  Count: 500 → 498 → 499 → 497 → ... → ~0 after 500 cycles.

PHASE 3b — LOGGING steady state (cycle 501+, until run ends):
  Every cycle: pop(1 oldest) → Flash. push(live_sample).
  Count oscillates 0–1. Frames go directly to Flash with ≤10 ms pipeline delay.
```

**Critical invariant: POP ALWAYS PRECEDES PUSH when buffer is full.**
This prevents overflow. The ring buffer itself has no silent discard —
it trusts the caller to maintain this invariant (debug assertion if push
called when is_full()). During Phase 1 (filling), only push is called and
count < 500, so the invariant doesn't apply.

**Pop-2 duration:** 500 cycles = 5.0 s total from start confirmation. After that,
all pre-start and drain-accumulated data is fully flushed to Flash. The buffer
runs near-empty for the remainder of the run — each frame hits Flash within one
cycle (10 ms), minimizing data loss risk on power failure.

---

## 3. Bit Packer (`bit_packer.cpp`)

### Encoding State Machine

```
                    ┌──────────────────────────┐
                    │  encode(current, *prev)   │
                    └────────────┬─────────────┘
                                 │
                    Compute deltas: Δ = current - prev
                                 │
                    All |Δ| ≤ 7 ?
                     ╱          ╲
                   YES           NO
                    │             │
                    ▼             ▼
              TYPE 1           All |Δ| ≤ 127 ?
            (coasting)         ╱           ╲
          4-bit deltas        YES           NO
          payload: 3.5 B       │             │
                               ▼             ▼
                         TYPE 2          TYPE 3
                        (turning)       (impact)
                      8-bit deltas     full 16-bit
                      payload: 7 B   payload: 14 B
```

### Encoding Function

```cpp
enum PacketType { TYPE1 = 0, TYPE2 = 1, TYPE3 = 2 };

CompressedFrame encode(const RawFrame& current, const RawFrame& prev, uint16_t seq) {
    int16_t d_w  = current.q_w  - prev.q_w;
    int16_t d_x  = current.q_x  - prev.q_x;
    int16_t d_y  = current.q_y  - prev.q_y;
    int16_t d_z  = current.q_z  - prev.q_z;
    int16_t d_lx = current.la_x - prev.la_x;
    int16_t d_ly = current.la_y - prev.la_y;
    int16_t d_lz = current.la_z - prev.la_z;

    // Forced anchor every 100 frames (SD §3): limit error propagation to 1 s max
    if ((seq % 100) == 0) {
        return pack_type3(current.q_w, current.q_x, current.q_y, current.q_z,
                           current.la_x, current.la_y, current.la_z);
    }

    if (all_abs_leq(d_w, d_x, d_y, d_z, d_lx, d_ly, d_lz, 7)) {
        return pack_type1(d_w, d_x, d_y, d_z, d_lx, d_ly, d_lz);
        // 3.5 bytes payload → frame total = 8 bytes stored
    }
    if (all_abs_leq(d_w, d_x, d_y, d_z, d_lx, d_ly, d_lz, 127)) {
        return pack_type2(d_w, d_x, d_y, d_z, d_lx, d_ly, d_lz);
        // 7 bytes payload → frame total = 11 bytes
    }
    return pack_type3(current.q_w, current.q_x, current.q_y, current.q_z,
                       current.la_x, current.la_y, current.la_z);
    // 14 bytes payload → frame total = 18 bytes
}
```

### Byte-Level Packing Detail

**Type 1 (Coasting):** 7 deltas × 4 bits = 28 bits = 3.5 bytes.

```
Byte layout (anchor 4B + payload 4B = 8B total, 4-bit alignment):
  [delta_hi][delta_lo][baro_hi][baro_lo] [qw:4|qx:4][qy:4|qz:4][lx:4|ly:4][lz:4|pad:4]
  Bit positions in anchor: delta_hi[15:14]=TT, [13:10]=seq, [9:0]=delta_ms
```

**Type 1 (Coasting):** 7 deltas × 4 bits = 28 bits = 3.5 bytes of useful data, stored in 4 bytes with 4-bit padding. SD §3 now shows "8 bytes stored (7.5 useful)" — consistent.

```
Anchor (4 bytes):
  Byte 0-1: Time delta (uint16 LE)
  Byte 2-3: Barometric pressure (uint16 LE, Pa/4)

Payload (3.5 bytes = 28 bits, packed in 4 bytes with 4-bit pad):
  Byte 4: [qw_delta:4 | qx_delta:4]
  Byte 5: [qy_delta:4 | qz_delta:4]
  Byte 6: [la_x_delta:4 | la_y_delta:4]
  Byte 7: [la_z_delta:4 | unused:4]

Frame total: 8 bytes stored.
```

**Type 2 (Turning):** 7 deltas × 8 bits = 56 bits = 7 bytes.

```
Payload (7 bytes):
  Byte 4:  q_w_delta (int8)
  Byte 5:  q_x_delta (int8)
  Byte 6:  q_y_delta (int8)
  Byte 7:  q_z_delta (int8)
  Byte 8:  la_x_delta (int8)
  Byte 9:  la_y_delta (int8)
  Byte 10: la_z_delta (int8)

Frame total: 11 bytes stored.
```

**Type 3 (Impact/Anchor):** Full absolute 16-bit values.

```
Payload (14 bytes):
  Byte 4-5:   q_w (int16 LE)
  Byte 6-7:   q_x (int16 LE)
  Byte 8-9:   q_y (int16 LE)
  Byte 10-11: q_z (int16 LE)
  Byte 12-13: la_x (int16 LE)
  Byte 14-15: la_y (int16 LE)
  Byte 16-17: la_z (int16 LE)

Frame total: 18 bytes stored.
```

### Page Buffer Assembly

```cpp
#define PAGE_SIZE 256

class PageBuffer {
    uint8_t buf[PAGE_SIZE];
    uint16_t cursor;  // 0..255

public:
    void append(const CompressedFrame& cf) {
        // cf.len is 8, 11, or 18
        memcpy(&buf[cursor], cf.data, cf.len);
        cursor += cf.len;
    }

    bool is_full() const {
        return cursor >= PAGE_SIZE - 18;  // next worst-case frame won't fit
    }

    void flush_to_flash();
    void reset() { cursor = 0; }
};
```

**Page write timing:** At ~10.7 bytes average frame size (Type 1 dominates):
- 256 / 10.7 ≈ 23 frames per page → page write every ~230 ms
- SPI Flash page write ≤ 3 ms → 1.3% duty cycle

---

## 4. Flash Manager (`flash_manager.cpp`)

### Flash Geometry

```
SPI Flash: MX25R1635F (2 MB, Nicla stock U7)
  - 512 sectors × 4 KB (erase unit)
  - 8192 pages × 256 bytes (program unit)

Layout:
  ┌──────────────────────────────────────────────────────────────┐
  │ Sector 0–509: Run data (circular buffer, 2 MB − 8 KB)        │
  │ Sector 510–511: Index sector (8 KB, reserved)                │
  └──────────────────────────────────────────────────────────────┘
```

### Index Sector (last 4 KB)

Stores a persistent array of run metadata at known Flash offset. Updated on `close_run()`.

```
Index sector layout:
  Byte 0-3:    Magic number (0x53474300 = "SGC\0")
  Byte 4-5:    Number of runs (uint16)
  Byte 6-7:    read_head page index (uint16)   → page address of oldest valid run
  Byte 8-9:    write_head page index (uint16)  → page address of next free space
  Byte 10-13:  Write counter (uint32)          → incremented on each close_run(), used for wear leveling
  
  Run entry array (from byte 16, 32 bytes each, up to 128 entries):
    Byte 0-1:   run_id (uint16, incrementing)
    Byte 2-5:   page_start (uint32)            → page address of run file start
    Byte 6-9:   page_end (uint32)              → page address of run file end
    Byte 10-13: timestamp (uint32, unixtime)
    Byte 14:    arm_side (uint8, 0=left, 1=right)
    Byte 15:    format_version (uint8)
    Byte 16-19: compressed_size (uint32)
    Byte 20-23: frame_count (uint32)
    Byte 24-31: reserved (zero)
```

**Power-loss safety:** Index sector is erased + rewritten atomically on `close_run()`. If power fails mid-write, the magic number check on boot detects corruption and `recover_index()` rebuilds by scanning all run file headers. The scan is O(n) in number of runs. Worst case: 2 MB / ~43 KB per training run (40s masters/kids) ≈ 46 runs. Full scan < 100 ms. Typical: 2 MB / ~108 KB (2-min DH run) ≈ 19 runs, scan < 50 ms.

### Creating a Run

```cpp
int create_run(ArmSide side) {
    // 1. Estimate run size conservatively: ~108 KB per 2-min DH run (worst case).
    //    Typical training runs (40s GS) are ~43 KB — but we budget for max.
    uint32_t estimated_pages = 432;  // 108 KB / 256 bytes
    
    // 2. Check if write_head + estimate overlaps read_head
    //    (circular wrap included)
    if (would_overlap(write_head, estimated_pages, read_head)) {
        // Delete oldest run: advance read_head past it
        RunEntry* oldest = get_run_at(read_head);
        read_head = oldest->page_end;
        remove_run_entry(oldest->run_id);
    }
    
    // 3. Open file at write_head
    // 4. Write 16-byte run file header
    // 5. Return run_id
}
```

### Sector Erase Strategy

Runs don't align to sector boundaries. Strategy:
- New run starts at `write_head` (next free page)
- `close_run()` marks the last page. Gaps between runs are absorbed into the circular buffer — when `write_head` wraps around, unused pages at the end of a sector are simply skipped
- Sectors are erased **only when** a new run needs to write into a previously-used sector. Erase is triggered by the first write to a page in that sector after `write_head` wraps
- Erase time: ~45 ms typ, ~400 ms max (MX25R1635F). Performed before the first page write in a sector during `create_run()` — before LOGGING starts, so no real-time impact

---

## 5. Run File (`run_file.cpp`)

### On-Disk Format

```
┌─────────────────────────────────────────────────────────────────┐
│ Run File (sequential pages in SPI Flash)                        │
│                                                                 │
│ Page 0: [16-byte Header] [240 bytes compressed frames start]    │
│ Page 1: [256 bytes compressed frames cont.]                     │
│ ...                                                             │
│ Page N: [last frames...] [pad bytes...] [4-byte CRC32]          │
│   CRC32 of all compressed frames from Page 0 byte 16 through    │
│   last frame byte. Written by close_run().                      │
└─────────────────────────────────────────────────────────────────┘
```

### CRC32-End Detection

On readback, the phone scans from the end of the known-compressed-data range backward to find `CRC32_MAGIC_HI` and `CRC32_MAGIC_LO` markers. The CRC32 itself is not distinguishable from data — the marker bytes (`0xC3 0x32`) precede the CRC32:

```
[compressed frames ...] [0xC3] [0x32] [CRC32_LE:4B]
```

CRC32 of the file is computed excluding these 6 trailer bytes. If the computed CRC matches, the file is valid (R05). If not, the run is marked corrupt and skipped.

---

## 6. SPI Bus Arbitration (`spi_bus.cpp`)

### Hardware

The Nicla Sense ME provides two SPI buses:

1. **BHI SPI (P0.03 SCK / P0.04 MOSI / P0.05 MISO):** Managed by Arduino_BHY2.
   Connects nRF52832 to BHI260AP (CS P0.31) and MX25R1635F Flash U7 (CS P0.26).

2. **SGC SPI (P0.11 SCK / P0.27 MOSI / P0.28 MISO):** Arduino `SPI` object.
   Used for RFID (v2) and UWB (v2). Nicla's external header SPI, routed
   internally on custom PCB.

```
BHI SPI (BHY2-managed, P0.03 SCK / P0.04 MOSI / P0.05 MISO):
  ├── CS P0.26 → Flash U7 (MX25R1635F, 2 MB)
  └── CS P0.31 → BHI260AP (sensor hub)

SGC SPI (Arduino SPI, P0.11 SCK / P0.27 MOSI / P0.28 MISO):
  ├── CS P0.20 → RFID (v2, unpopulated)
  └── CS P0.29 → UWB  (v2, unpopulated)
```

No external SPI flash needed — the Nicla's onboard 2 MB Flash U7 is sufficient.

### Arbitration Protocol

Single-threaded (no RTOS, no DMA contention). The main loop is the only SPI user:

```cpp
class SPIBus {
    static SPIBus instance;
    bool locked;
    
public:
    void acquire(SPIDevice dev) {
        // Spin-wait if locked. Only one device at a time.
        // No timeout — worst-case is Flash page write at 3 ms.
        while (locked) { /* yield */ }
        locked = true;
        digitalWrite(cs_pin(dev), LOW);
    }
    
    void release(SPIDevice dev) {
        digitalWrite(cs_pin(dev), HIGH);
        locked = false;
    }
    
    void transfer(uint8_t* tx, uint8_t* rx, uint16_t len) {
        nrf_spim_transfer(tx, rx, len);  // blocking
    }
};
```

### Transaction Budget

Per 10 ms cycle, SPI transactions are sequenced:

```
Cycle start (t=0):
  1. RFID poll (if due):             acquire → transfer ~20B → release  (~200 µs)
  2. Flash page write (if needed):   acquire → write 256B → release    (~3000 µs, rare)
  3. Sensor I²C (non-SPI, parallel): read BHI260AP FIFO                (~200 µs)
  4. Bit-packing (CPU, no bus):      ~200 µs

  Total worst-case: ~3600 µs. Margin: ~6400 µs (64% idle).
```

**Rule:** RFID poll and Flash page write never overlap in the same cycle. If a Flash page is full during an RFID cycle, the Flash write is deferred to the next idle cycle.

---

## 7. State Machine (`state_machine.cpp`)

### State Enum

```cpp
// BLE-exposed values: 0=idle, 1=armed, 2=logging, 3=post_run
// SLEEP (0xFF) NOT BLE-exposed — radio off during sleep
enum class DeviceState : uint8_t {
    SLEEP     = 0xFF,  // Not BLE-exposed (BLE off during sleep)
    IDLE      = 0,
    // SETUP  = 4 — v2 only (requires RFID, F57)
    ARMED     = 1,
    LOGGING   = 2,
    POST_RUN  = 3
};
```

### Transition Implementation

```cpp
class StateMachine {
    State current;
    uint32_t state_entered_ms;  // RTC timestamp
    uint32_t proximity_held_ms;    // for 20s factory reset detection
    bool file_open;
    int current_run_id;
    
public:
    void tick();  // Called every main loop iteration (~1 kHz idle, 100 Hz in LOGGING)
    
    // Event sources (called from ISR or main loop):
    void on_proximity_start();
    void on_proximity_end();
    void on_baro_descent(float m_s);
    void on_flatline_stillness();
    void on_ble_command(uint8_t cmd);
    void on_ble_disconnect();
    void on_battery_low();
    void on_qi_power(bool present);
};
```

### Entry/Exit Hooks

```cpp
void enter_state(State s) {
    switch (s) {
    case SLEEP:
        led.off();
        beeper.off();
        // rfid_reader.disable(); — ⚠️ v2 only
        bmp390.sleep();
        bhi260ap.sleep();
        ldc1612.low_power_mode();  // 10 Hz poll, INTB enabled
        ble.stop_advertising();
        // ... WFE entry in main loop (not here — see sleep.cpp)
        break;
        
    case IDLE:
        ld1612.continuous_mode();
        bmp390.wake();
        bhi260ap.wake();
        led.set_blue(blink_or_solid);  // depends on calibration accuracy
        ble.start_advertising();
        break;
    // SETUP case removed — ⚠️ v2 only (requires RFID, F57)
        
    case ARMED:
        ring_buffer.reset();
        beeper.on();
        led.set_green();
        start_detector.reset();
        break;
        
    case LOGGING:
        beeper.off();
        led.set_red();
        ldc1612.mask_interrupt();  // ignore inductive trigger during run (R03)
        // rfid_reader.set_rate(...) + enable() — ⚠️ v2 only
        file_open = true;
        current_run_id = flash_manager.create_run(arm_side);
        break;
        
    case POST_RUN:
        // rfid_reader.disable(); — ⚠️ v2 only
        flash_manager.close_run(current_run_id);
        file_open = false;
        led.set_blue_slow_blink();  // back to advertising
        ble.start_advertising();
        ble.update_run_count(flash_manager.get_run_list().count);
        break;
    }
    state_entered_ms = rtc_now_ms();
}
```

### Timer Management

```cpp
void StateMachine::check_timeouts() {
    uint32_t elapsed = rtc_now_ms() - state_entered_ms;
    
    switch (current) {
    case IDLE:
    // case SETUP: — ⚠️ v2 only
    case POST_RUN:
        if (elapsed > 300000 && !ble.is_connected()) {  // 5 min (F12)
            enter_state(SLEEP);
        }
        break;
    case ARMED:
        if (elapsed > 30000) {  // 30 s (R02)
            enter_state(IDLE);
        }
        break;
    case POST_RUN:
        if (elapsed > 2000) {  // 2 s cooldown
            allow_rearm = true;
        }
        break;
    }
}
```

### Proximity Hold Detection (Factory Reset, F42)

```cpp
void StateMachine::on_proximity_start() {
    proximity_held_ms = rtc_now_ms();
    if (current == SLEEP) {
        enter_state(IDLE);  // F13
    }
}

void StateMachine::on_proximity_end() {
    uint32_t held_duration = rtc_now_ms() - proximity_held_ms;
    if (current == IDLE && held_duration >= 20000) {
        // Factory reset
        led.flash_red(3);
        ble.clear_bonding();
        flash_manager.erase_all();
        device_name = DEFAULT_NAME;
        NVIC_SystemReset();  // full reboot
    }
}
```

---

## 8. Start Detector (`start_detector.cpp`) — Dual Mode (v2)

Detects run start via **two simultaneous triggers**, whichever fires first:
- **Speed mode:** vertical descent > 1.5 m/s sustained for 200 ms
- **Drop mode:** cumulative vertical drop > 2.0 m from arming pressure P₀

P₀ is captured at arming. Both conditions monitored simultaneously — no athlete toggle needed.

### Algorithm

```cpp
class StartDetector {
    static const int SPEED_WINDOW = 2;      // 200 ms at 10 Hz feed rate (BARO_FEED_HZ=10)
    static const float SPEED_THRESHOLD = 1.5; // m/s
    static const float DROP_THRESHOLD = 2.0;  // meters from P₀
    
    float p0;            // pressure at arming (Pa)
    float history[SPEED_WINDOW];
    int idx;
    bool speed_triggered;
    bool drop_triggered;
    
public:
    void reset() {
        idx = 0;
        speed_triggered = false;
        drop_triggered = false;
        memset(history, 0, sizeof(history));
    }
    
    void set_p0(float pa) { p0 = pa; }  // called at arming
    
    void feed_pressure(float pa) {
        float alt = pressure_to_altitude(pa);
        float alt0 = pressure_to_altitude(p0);
        history[idx] = alt;
        idx = (idx + 1) % SPEED_WINDOW;
        
        // Speed mode: slope over 200 ms window (2 samples at 10 Hz)
        // Both samples must indicate descent > threshold
        if (idx == 0) {
            float oldest = history[0];  // oldest sample, written 2 cycles ago
            float newest = history[SPEED_WINDOW - 1];  // newest sample
            float speed = (oldest - newest) / (SPEED_WINDOW * 0.1);  // m/s, positive = descending
            if (speed > SPEED_THRESHOLD) {
                speed_triggered = true;
            }
        }
        
        // Drop mode: cumulative descent from arming P₀
        float drop = alt0 - alt;  // positive = descending
        if (drop > DROP_THRESHOLD) {
            drop_triggered = true;
        }
    }
    
    bool descent_detected() const { return speed_triggered || drop_triggered; }
};
```

**Pressure-to-altitude lookup:** Pre-computed at compile time for 600–1100 hPa range at −20 to +10°C. Resolution: ~2.5 cm per LSB (Pa/4). Stored as a 2000-entry int16 table → 4 KB Flash (acceptable for nRF52832's 512 KB internal Flash).

---

## 9. End Detector (`end_detector.cpp`)

### Algorithm

```cpp
class EndDetector {
    static const int FLAT_WINDOW = 100;   // 10 s at 100 ms feed rate
    static const float FLAT_THRESHOLD = 0.3;  // m/s
    static const float STILL_THRESHOLD = 0.05; // g (≈ 0.5 m/s²)
    
    bool flatline_detected();
    bool stillness_detected();
    
public:
    void feed(float pressure, const RawFrame& frame) {
        // Pressure → altitude → vertical speed (same as start detector)
        // IMU linear acceleration magnitude:
        float accel_mag = sqrt(frame.la_x² + frame.la_y² + frame.la_z²) / 1000.0; // g
        
        // Count consecutive samples where:
        //   |vertical_speed| < FLAT_THRESHOLD
        //   AND |accel_mag - 1.0| < STILL_THRESHOLD  (1g = stationary)
        // When count ≥ FLAT_WINDOW: run_ended = true
    }
    
    bool run_ended() const;
};
```

---

## 10. BLE SGC Service (`sgc_service.cpp`)

### Characteristic Initialization

```cpp
// Nordic SDK / ArduinoBLE pseudo-code
void init_sgc_service() {
    BLEService sgc_service("53470000-0000-1000-8000-00805F9B34FB");
    
    // Writable characteristics
    BLECharCharacteristic time_char("5347ABC0-...", BLERead | BLEWrite);
    BLECharCharacteristic name_char("5347ABC1-...", BLERead | BLEWrite);
    BLECharCharacteristic arm_char("5347ABC2-...", BLERead | BLEWrite);
    BLECharCharacteristic disc_char("5347ABC3-...", BLERead | BLEWrite);
    
    // Readable/notifiable characteristics
    BLECharCharacteristic state_char("5347ABC4-...", BLERead | BLENotify);
    BLECharCharacteristic bat_char("5347ABC5-...", BLERead);
    BLECharCharacteristic chg_char("5347ABCF-...", BLERead | BLENotify);
    BLECharCharacteristic count_char("5347ABC6-...", BLERead | BLENotify);
    BLECharCharacteristic flash_char("5347ABC7-...", BLERead | BLENotify);
    BLECharCharacteristic age_char("5347ABC8-...", BLERead);
    BLECharCharacteristic list_char("5347ABC9-...", BLERead);
    
    // File transfer
    BLECharCharacteristic ft_req_char("5347ABCA-...", BLEWrite);
    BLECharCharacteristic ft_chunk_char("5347ABCB-...", BLENotify);
    BLECharCharacteristic ft_crc_char("5347ABCC-...", BLERead);
    BLECharCharacteristic ft_status_char("5347ABCD-...", BLERead | BLENotify);
    
    // Sensor status
    BLECharCharacteristic sensor_char("5347ABCE-...", BLERead);
    
    // Calibration
    BLECharCharacteristic cal_char("5347ABD0-...", BLERead | BLENotify);
    
    // Write handlers
    time_char.setWriteCallback(on_time_write);    // F37
    name_char.setWriteCallback(on_name_write);    // F11
    arm_char.setWriteCallback(on_arm_write);      // F11
    disc_char.setWriteCallback(on_disc_write);    // F11
    
    // Read handlers
    bat_char.setReadCallback(on_bat_read);
    // ... etc.
    
    sgc_service.addCharacteristic(time_char);
    // ... etc.
    
    BLE.addService(sgc_service);
}
```

### Write Callbacks

```cpp
void on_time_write(BLEDevice central, BLECharacteristic chr) {
    uint32_t unix_time;
    chr.readValue(&unix_time, 4);
    rtc_set(unix_time);
    // Time sync complete — device can now operate standalone (F37)
}

void on_disc_write(BLEDevice central, BLECharacteristic chr) {
    uint8_t val;
    chr.readValue(&val, 1);
    discipline = (val >> 4) & 0x0F;  // 0=SL, 1=GS, 2=SG, 3=DH
    mount_type = val & 0x0F;         // 0=arm, 1=pole (reserved)
    // rfid_reader.set_rate(discipline_to_rate(discipline)); — ⚠️ v2 only
}
```

### Notifications (push from device → phone)

```cpp
// Called from main loop or event handlers
void notify_battery(uint8_t pct) {
    bat_char.writeValue(pct);
}

void notify_calibration(uint8_t accuracy) {
    cal_char.writeValue(accuracy);
    // LED update handled by led.cpp reading this value
}

void notify_charging(bool is_charging) {
    chg_char.writeValue(is_charging ? 1 : 0);
}
```

---

## 11. BLE File Transfer (`file_transfer.cpp`)

### State Machine

```
                         ┌─────────────────┐
                         │      IDLE       │
                         │  status = 0     │
                         └────────┬────────┘
                                  │ Phone writes run_id to ...ABCA
                                  ▼
                         ┌─────────────────┐
                         │   TRANSFERRING  │
                         │  status = 1     │
                         │  chunk_idx = 0  │
                         │  Read run file  │
                         │  from Flash     │
                         └────────┬────────┘
                                  │ Send chunks via ...ABCB (notify)
                                  │ 244 bytes each (MTU − 3)
                                  │ Phone acks by reading ...ABCC
                                  ▼
                         ┌─────────────────┐
                         │   COMPLETE      │
                         │  status = 2     │
                         │  Send CRC32     │
                         │  via ...ABCC    │
                         └────────┬────────┘
                                  │ Phone verifies CRC
                                  │ Phone writes 0 to ...ABCA (next run)
                                  ▼
                                IDLE

  Error path:
  ┌──────────────┐
  │    ERROR     │  status = 3
  │  chunk lost  │  Phone writes 0 to ...ABCA → retry
  │  CRC fail    │  or disconnect → reset
  └──────────────┘
```

### Protocol

```cpp
void on_ft_request(BLEDevice central, BLECharacteristic chr) {
    uint16_t run_id;
    chr.readValue(&run_id, 2);
    
    if (run_id == 0) {
        // Reset / abort transfer
        transfer_reset();
        return;
    }
    
    RunFile run = flash_manager.read_run(run_id);
    transfer_chunks(run.data, run.size, run.crc32);
}

void transfer_chunks(uint8_t* data, uint32_t size, uint32_t crc32) {
    ft_status_char.writeValue(1);  // transferring
    uint32_t offset = 0;
    uint16_t chunk_idx = 0;
    
    while (offset < size) {
        uint16_t chunk_len = min(244, size - offset);
        ft_chunk_char.writeValue(&data[offset], chunk_len);
        offset += chunk_len;
        chunk_idx++;
        // BLE stack buffers notifies — no need for per-chunk ACK
        // Flow control: BLE connection interval (7.5 ms min) limits rate
    }
    
    // Send CRC32 for verification
    ft_crc_char.writeValue((uint8_t*)&crc32, 4);
    ft_status_char.writeValue(2);  // complete
}
```

**Transfer time:** 108 KB run (2-min DH, worst case) at 20 KB/s = ~5.4 seconds. Typical 40s training run (~43 KB) transfers in ~2.2 seconds.

---

## 12. RFID Reader (`rfid_reader.cpp`)

### Hardware

**Module:** Impinj E310-based embedded module (SILION SIM3600, Chafon CF-E311, or equivalent).
- Interface: SPI (4-wire) to nRF52832
- Power: 3.3V, ~80 mA active TX, µA in standby
- Enable: GPIO-driven (P0.20) — powered only during LOGGING
- Antenna: Integrated on module (ceramic chip or PCB trace)
- Read range: ≥ 1 m at 0 dBm TX power (EPC Gen2 passive tags)
- Protocol: EPCglobal Class 1 Gen 2 / ISO 18000-6C
- Form factor: ~25×25 mm SMD, soldered as daughterboard on custom PCB

⚠️ **Why module, not bare IC:** ST25RU3993 is NRND/EOL. Impinj E310 bare IC requires Impinj partnership + custom RF matching network design (impedance matching, balun, filter) — high risk for v1. The module approach is pre-certified, lower risk, and available in small quantities (~$40-80).

### Inventory Round State Machine

```cpp
class RFIDReader {
    enum { IDLE, SENDING, LISTENING, DONE } state;
    uint32_t round_start_us;
    
public:
    GateEvent poll() {
        switch (state) {
        case IDLE:
            // Wait for discipline timer
            break;
        case SENDING:
            // SPI → EPC Gen2 Select + Query command
            // Select: target gate tag population (optional filtering)
            // Query: start inventory round, Q=0 (single tag expected)
            state = LISTENING;
            round_start_us = micros();
            break;
        case LISTENING:
            // SPI → read RN16 + EPC from reader FIFO
            // If tag found + RSSI > best_rssi: update best
            // Timeout after 15 ms (P09)
            if (micros() - round_start_us > 15000) {
                state = DONE;
            }
            break;
        case DONE:
            if (best_tag_found) {
                return GateEvent{best_epc, best_rssi, rtc_now_ms()};
            }
            state = IDLE;
            return GateEvent::null();
        }
    }
};
```

### RSSI-Based Nearest Tag Selection (F54)

```cpp
void on_tag_read(uint32_t epc, int8_t rssi) {
    if (rssi > best_rssi) {
        best_rssi = rssi;
        best_epc = epc;
    }
    tag_count++;
}

// At end of round:
GateEvent result = (tag_count > 0) ? GateEvent{best_epc, best_rssi, now} : null;
```

---

## 13. Sensor Acquisition (v2.0 — Arduino_BHY2)

All Nicla on-board sensors (BHI260AP, BMP390, BMM150, BME688) are accessed via
the **Arduino_BHY2 library**. This library handles SPI communication, firmware
upload, sensor configuration, and data fusion internally.

### BHY2 Initialization

```cpp
#include <Arduino_BHY2.h>

SensorQuaternion rotation(SENSOR_ID_RV);     // Rotation Vector (quaternion)
SensorXYZ linAccel(SENSOR_ID_LACC);           // Linear Acceleration
Sensor baro(SENSOR_ID_BARO);                  // Barometric pressure (BMP390)
Sensor temp(SENSOR_ID_TEMP);                  // Temperature

void setup() {
    BHY2.begin();  // Initialises SPI + BHI260AP firmware
    rotation.begin();
    linAccel.begin();
    baro.begin();
}

void loop() {
    BHY2.update();  // Must be called continuously
    
    if (rotation.dataAvailable()) {
        float qw = rotation.w();
        float qx = rotation.x();
        float qy = rotation.y();
        float qz = rotation.z();
        rotation.clearDataAvailFlag();
    }
    if (linAccel.dataAvailable()) {
        float la_x = linAccel.x();
        float la_y = linAccel.y();
        float la_z = linAccel.z();
        linAccel.clearDataAvailFlag();
    }
    if (baro.dataAvailable()) {
        float pressure_pa = baro.value();
        baro.clearDataAvailFlag();
    }
}
```

### Calibration

The BHI260AP self-calibrates through the BHY2 library. Accuracy levels 0–3:
- 0: Unreliable — do not arm
- 1: Low — do not arm
- 2: Medium — minimum for arming (F51)
- 3: High — full accuracy

Figure-8 calibration motion for ~10 seconds reaches accuracy ≥ 2.
Calibration matrix stored in BHI260AP flash (U2, 2 MB).

⚠️ **Accuracy access note (2026-06-18):** The BHI260AP reports calibration
accuracy via `BHY2_META_EVENT_SENSOR_STATUS` meta-events, not in the
quaternion data packet. The Arduino_BHY2 library's `SensorQuaternion::accuracy()`
returns the wrong byte (sensor data at offset 8, not the accuracy enum).
The SGC firmware works around this by shadowing `BoschParser.cpp` from the
library into `src/`, where the meta-event handler stores the real accuracy
in `g_bhy2_accuracy[34]` (34 = SENSOR_ID_RV). See
`Firmware_implementation/src/BoschParser.cpp` for the one-line patch.

### From I²C to BHY2 — Migration Notes

The v1 firmware used direct I²C reads from BHI260AP (0x28) and BMP390 (0x77).
This does NOT work on the Nicla because BHI260AP is on SPI (P0.03–P0.05),
not I²C. The correct approach is Arduino_BHY2:

| v1 (direct I²C) | v2 (Arduino_BHY2) |
|---|---|
| `Wire.beginTransmission(0x28)` | `BHY2.begin()` |
| `Wire.requestFrom(0x28, 14)` | `BHY2.update(); rotation.dataAvailable();` |
| `Wire.requestFrom(0x77, 3)` | `baro.dataAvailable(); baro.value();` |
| `PIN_FIFO_IRQ = p2` | BHY2 handles FIFO internally |

### LDC1612 (SGC Addition — I²C)

The LDC1612 is the only sensor NOT managed by BHY2. It connects to I2C1
(P0.22/P0.23) at address 0x2A with interrupt on P0.02.

```cpp
void init() {
    // I²C address: 0x2A on Wire (P0.22/P0.23)
    // Channel 0 only
    // RCOUNT: ~10 Hz in low-power mode (SLEEP), continuous in IDLE/ARMED
    // INTB pin: P0.02 — asserts when proximity threshold crossed
}

---

## 14. Battery (`battery.cpp`)

Battery management uses the Nicla's built-in BQ25120 charger + BHI260AP fuel gauge.
No custom ADC pin needed.

```cpp
void init() {
    // Battery voltage and charging status via BHY2 / Nicla_System
    // No external ADC — BQ25120 + BHI260AP handle everything
}

uint8_t read_percent() {
    // Read battery level via Nicla_System or BHY2 sensor
    // Returns 0–100%
}

bool is_qi_present() {
    // GPIO P0.10: pulled LOW when Qi receiver outputs 5V
    // HIGH = no Qi, LOW = Qi active
    pinMode(PIN_QI_DETECT, INPUT_PULLUP);
    return (digitalRead(PIN_QI_DETECT) == LOW);
}
```

---

## 15. LED (`led.cpp`)

```cpp
class LED {
    // 5× SK6812-mini strip on single NZR wire (PIN_LED_STRIP = P0.19)
    // Sequential animation: flowing lit point that travels along the strip
    // See sgc_architecture_hardware.md §4 for animation patterns per state
    
public:
    void off();                  // All HIGH
    void set_blue(bool blink);   // Fast blink (uncalibrated) or solid (calibrated)
    void set_green();            // Armed
    void set_red();              // Logging
    // void set_white();         // SETUP mode — ⚠️ v2 only (RFID, F57)
    void set_yellow();           // Low battery / error
    void flash_red(int count);   // Factory reset indicator
    
    // Non-blocking PWM update in main loop:
    void tick();                 // Called every 50 ms, handles blink patterns
};
```

---

## 16. Beeper (`beeper.cpp`)

```cpp
void init() {
    // GPIO P0.09, PWM at 4 kHz (audible)
    // Drives surface transducer bonded to enclosure inner wall
    // IP67: no sound port needed — vibrations conduct through polycarbonate
}

void on() {
    pwm_start(PIN_BEEPER, 4000, 50);  // 50% duty
}

void off() {
    pwm_stop(PIN_BEEPER);
}
```

---

## 17. Sleep (`sleep.cpp`)

```cpp
void enter_sleep() {
    // Pre-conditions: LDC1612 in low-power mode with INTB enabled
    // BHI260AP, BMP390, RFID, BLE all OFF
    // nRF52 peripherals: UART off, SPI off, TWI off, PWM off
    // RTC: running (32 kHz LFCLK)
    
    // Configure wake sources:
    // - LDC1612 INTB → GPIO PORT event → wake from WFE
    
    __WFE();  // Wait For Event — CPU sleeps, RAM retained, RTC runs
    // Execution resumes here after wake event
    
    // Post-wake: peripherals need re-enabling (handled by state machine enter_state(IDLE))
}
```

**Note:** System ON sleep (WFE), NOT System OFF. RAM and RTC preserved. Wake latency < 100 µs.

---

## 18. `config.h` (v2.0 — Nicla Replica)

See `include/config.h` for the authoritative pin map.

Key changes from v1.3:
- Nicla stock pins documented and locked
- BHI260AP/BMP390 via Arduino_BHY2 (not direct I²C)
- Battery via BQ25120/BHY2 (no VBAT_ADC)
- Flash = Nicla's MX25R1635F U7 (no external W25Q16)
- All GPIOs verified against ANNA-B112 datasheet Table 7
- SGC v1 pins: P0.02 (LDC_INTB), P0.09 (BEEPER), P0.10 (QI_DETECT), P0.19 (LED_STRIP)
- SGC v2 pins: P0.20 (RFID_CS), P0.24 (RFID_EN), P0.29 (UWB_CS), P0.30 (UWB_PWR)

---

## 19. `main.cpp` — Main Loop Pseudocode

```cpp
StateMachine sm;
RingBuffer ring;
PageBuffer page;
BitPacker packer;
FlashManager flash;
RFIDReader rfid;
LED led;
Beeper beeper;
Battery bat;
SensorFusion sensors;
uint32_t last_baro_feed_ms = 0;  // for start/end detectors (10 Hz)
RawFrame prev_frame;

void setup() {
    config_init();
    sensors.init();     // BHI260AP (100 Hz), BMP390 (100 Hz), LDC1612
    flash.init();
    rfid.init();
    ble.init();
    sm.enter_state(SLEEP);
}

void loop() {
    // === Always-active checks ===
    bat.tick();
    if (bat.is_low() && sm.current() != SLEEP) {
        if (sm.current() == LOGGING) {
            flash.close_run(sm.current_run_id());
        }
        sm.enter_state(SLEEP);
        return;
    }
    
    // === State-specific logic ===
    switch (sm.current()) {
    
    case SLEEP:
        // Wait for proximity wake (WFE in sleep.cpp)
        // WFE already entered; if we're here, we just woke up
        sm.on_proximity_start();  // transitions to IDLE
        break;
        
    case IDLE:
        ble.poll();
        if (ldc1612.proximity() && sensors.accuracy() >= 2) {
            if (ldc1612.held_duration() >= ARM_HOLD_MS) {
                sm.enter_state(ARMED);
            }
        }
        sm.check_timeouts();
        break;
        
    // case SETUP: — ⚠️ v2 only (requires RFID, F57)
    //     rfid.poll_1hz();
    //     if (rfid.tag_detected()) { ble.notify_setup_tag(rfid.last_tag()); }
    //     ble.poll();
    //     sm.check_timeouts();
    //     break;
    
    case ARMED:
        if (sensors.fifo_ready()) {
            RawFrame f = sensors.read_frame();
            f.baro_pa_div4 = sensors.read_pressure() / 4;  // BMP390 at 100 Hz → stored in every frame
            
            // Phase 1: filling → just push
            // Phase 2: full → pop 1 oldest (discard), push 1 new (freshen)
            if (ring.is_full()) {
                ring.pop();
            }
            ring.push(f);
            prev_frame = f;
        }
        
        // Start detector: fed at 10 Hz (every 10th cycle's baro value)
        if (millis() - last_baro_feed_ms >= 100) {
            start_detector.feed_pressure(prev_frame.baro_pa_div4 * 4.0);
            last_baro_feed_ms = millis();
        }
        if (start_detector.descent_detected()) {
            sm.enter_state(LOGGING);
        }
        sm.check_timeouts();
        break;
        
    case LOGGING:
        if (sensors.fifo_ready()) {
            RawFrame f = sensors.read_frame();
            f.baro_pa_div4 = sensors.read_pressure() / 4;  // BMP390 at 100 Hz
            
            // POP FIRST (always), then push
            // Phase 3a (drain, 500 cycles = 5.0 s): pop 2 → Flash. Push 1.
            // Phase 3b (steady, cycle 501+):       pop 1 → Flash. Push 1.
            static int drain_count = 0;
            int pop_n = (drain_count < 500) ? 2 : 1;
            drain_count++;
            
            for (int i = 0; i < pop_n; i++) {
                if (!ring.is_empty()) {
                    RawFrame old = ring.pop();
                    CompressedFrame cf = packer.encode(old, prev_frame);
                    prev_frame = old;  // chain: h₂ = h₁ + delta₂, h₃ = h₂ + delta₃, ...
                    page.append(cf);
                    if (page.is_full()) page.flush_to_flash();
                }
            }
            
            ring.push(f);
            // prev_frame already updated inside the pop loop to last popped frame.
        }
        
        // Barometric end detection (fed at 10 Hz from stored frame pressure)
        if (millis() - last_baro_feed_ms >= 100) {
            end_detector.feed(prev_frame.baro_pa_div4 * 4.0, prev_frame);
            last_baro_feed_ms = millis();
        }
        
        // RFID inventory (at discipline rate)
        rfid.tick();
        GateEvent ge = rfid.poll();
        if (ge.valid()) {
            flash.write_gate_event(ge);
        }
        
        if (end_detector.run_ended()) {
            sm.enter_state(POST_RUN);
        }
        break;
        
    case POST_RUN:
        ble.poll();
        sm.check_timeouts();
        break;
    }
    
    // === Always-active peripherals ===
    led.tick();
    beeper.tick();
}
```

---

*Next: per-module unit test specifications, integration test scenarios, hardware-in-the-loop validation plan.*
