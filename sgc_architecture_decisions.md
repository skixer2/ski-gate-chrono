# SGC — Architecture Decisions Log

*2026-06-09 — Working session with JP. Documents the rationale for the v2 architecture pivot.*

---

## AD-001: Remove UHF RFID Reader from v1

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** The UHF RFID reader (Impinj E310, $30 at production) is removed from the active BOM. Unpopulated footprint retained on PCB for potential v2.

**Rationale:**
- RFID module was 42% of device BOM ($30/$71)
- Killing it drops device BOM from $71 to ~$40
- Two-arm kit retail drops from $426 to ~$243 → athlete saves $183
- RFID reader was also the dominant power consumer (80 mA active)
- Gate detection moves to pressure ΔP + IMU on the phone post-run

**Trade-off:** Loses unambiguous gate ID from passive pole tags. Accepted — pressure + IMU detection is the primary mechanism.

---

## AD-002: No BLE Beacons on Poles for v1

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** No per-gate electronics of any kind for v1. Zero course infrastructure cost.

**Rationale:**
- BLE beacons require batteries, weatherproofing, maintenance
- 60-gate course = $600 in beacons (even at $10 each)
- Sparse checkpoint beacons (3-4 per course) considered but rejected as premature optimization
- Course cost should be zero for adoption; $0 beats $40 every time

---

## AD-003: Dumb Logger Architecture

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** The device does NO gate detection, NO BLE scanning, NO math. It logs IMU + pressure at 100 Hz to Flash and sleeps. The phone does all gate detection post-run.

**Rationale:**
- Simplifies firmware (no RFID driver, no pressure matching, no gate state machine)
- Reduces power (no BLE scanning duty cycle, no RFID active draw)
- Gate detection algorithm iterates quickly on phone (app update vs. firmware OTA)
- Three operational tiers (Gold/Bronze) with same firmware — trainer chooses effort vs. precision

---

## AD-004: Gate Detection via Pressure ΔP + IMU

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** Trainer registers course pressure map (deltas from START) via phone. Post-run, phone matches athlete's pressure trace against stored ΔP deltas, cross-referenced with IMU turn kinematics.

**Key details:**
- Pressure deltas (ΔP_n = P_n − P_start), not absolute pressure — stable across hours/days
- ω zero-crossing = transition between gates, not at gate. Gate occurs during active rotation.
- Learned spatial percentage A (F26 Case B) for estimated gate placement — not ω midpoint
- Pressure interpolation for missed gates: find t where P(t) = P_start + ΔP_n
- Bananas: 2 gates possible between zero-crossings
- Flats: IMU turn counting fills gap when multiple gates share same altitude band

---

## AD-005: Two Operational Tiers (Silver Removed)

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** Gold (trainer loaded a map) and Bronze (no map). No automatic fallback to old data. Trainer explicitly chooses which map to use or opts out.

**Rationale:**
- Device cannot know if an old map is still valid
- Trainer must explicitly load a previous map → accepts drift risk
- Partial update supported: trainer can selectively Move/Delete/Add gates from an old map

---

## AD-006: Dual Start Detection

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** Two simultaneous triggers, whichever fires first:
- Speed mode: vertical descent > 1.5 m/s sustained for 200 ms
- Drop mode: cumulative vertical drop > 2.0 m from arming pressure P₀

No athlete toggle needed.

**Rationale:** Flat start huts are common in training. Speed-only would miss them. Drop-only would false-trigger on the walk to the start gate. Both together cover every geometry.

---

## AD-007: Course Setup — Two Modes, Dual View

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** 
- Mode A (New Course): sequential recording, no detection. Trainer walks in order, taps to record.
- Mode B (Update Existing): GPS + ΔP detection. Phone highlights nearest gate → trainer chooses Move/Delete/Add.
- Dual view: graphical map (GPS available) ↔ text list (always available).

**Rationale:**
- New course has no stored data to detect against — detection is meaningless
- Update mode leverages GPS + ΔP for smart gate identification
- Map view gives spatial awareness; text list is the universal fallback

---

## AD-008: Unpopulated Footprints for v2

**Date:** 2026-06-09
**Status:** Accepted

**Decision:** Impinj E310 (UHF RFID) and Qorvo DW3000 (UWB) footprints retained on PCB, unpopulated. Zero v1 cost. Firmware probes SPI at boot to detect presence and enable/disable drivers dynamically.

**Rationale:** PCB redesign is expensive. These footprints are a free hedge. If v1 pressure-only detection proves insufficient, v2 can populate RFID without a board spin.

---

## File Structure (V-Model)

```
ski_gate_chrono/
├── sgc_architecture_decisions.md          ← This file
├── sgc_context_gemini_01.md               ← Original enriched plan
├── sgc_context_gemini_02.md               ← Working context (latest)
├── sgc_uwb_positioning_context.md         ← UWB analysis (future reference)
│
└── High_Level_Requirements/
    └── sgc_requirements.md                ← REQ-FUNC, REQ-PERF, REQ-HW, REQ-ROB, REQ-DB
    └── system_design/
        ├── sgc_system_design.md           ← State machine, ring-buffer drain, bit-packing, BLE GATT
        └── architecture_modules/
            ├── sgc_architecture_devices.md  ← Device firmware (sensors, storage, BLE, state machine)
            ├── sgc_architecture_phone.md    ← Phone software (BLE client, decompressor, gate detection, UI)
            ├── sgc_architecture_hardware.md ← Device hardware (block diagram, pin map, power, enclosure)
            ├── sgc_bom.md                   ← Bill of materials
            └── module_design/              ← (future: detailed per-module specs)
```

---

## AD-009: PlatformIO Build System — Nicla via nordicnrf52 + Mbed

**Date:** 2026-06-10
**Status:** Accepted

**Decision:** The firmware build uses PlatformIO with the `nordicnrf52` platform, `nicla_sense_me` board, and `arduino` framework (which maps to `framework-arduino-mbed`). Two build targets: `nicla` (prototyping on Nicla Sense ME) and `custom_pcb` (production nRF52832-QFAA via Adafruit nRF52 core).

**Rationale:**
- PlatformIO's `platform-arduino_mbed` does not exist as a separate repo — Nicla support is through `nordicnrf52` platform after updating to latest
- Nicla uses Mbed OS framework (ArduinoCore-mbed) which bundles BHI260AP+BMP390 drivers
- Custom PCB uses Adafruit nRF52 Arduino core for bare-metal control
- `ArduinoCore-mbed` repo lacks `platform.json` — unusable as a direct PlatformIO platform URL
- Custom board JSON (`sgc_nrf52832`) created as fallback but unused after platform update resolved Nicla board

**Build results (2026-06-10):**
- `nicla`: ~276 KB Flash (52%), ~46 KB RAM (72%) — clean, zero errors
- `custom_pcb`: builds via `adafruit_feather_nrf52832` board (Adafruit core)

---

## AD-010: SGCRingBuffer — Rename to Avoid Mbed Collision

**Date:** 2026-06-10
**Status:** Accepted

**Decision:** The firmware's `RingBuffer` class renamed to `SGCRingBuffer` in `ring_buffer.h/cpp` and all references (main.cpp, state_machine.cpp).

**Rationale:** The Mbed framework in `nordicnrf52` platform defines its own `RingBuffer` class in system headers. C++ name collision at link time.

---

## AD-011: Nicla Pin Separation via #ifdef

**Date:** 2026-06-10
**Status:** Accepted

**Decision:** `config.h` uses `#ifdef NICLA_SENSE_ME` / `#else` to separate Nicla pin assignments from custom PCB pin assignments. Nicla target does NOT redefine SPI pins (lets variant provide them); Qi detect moved from P0.07 (Nicla SPI MISO) to P0.10.

**Rationale:** The Nicla variant (`pins_arduino.h`) defines its own SPI pins (P0.07–P0.09) which conflict with custom PCB SPI pins (P0.13–P0.15). Redefinition causes compiler warnings and potential runtime misbehavior.

---

## AD-012: Nicla LED + Beeper as No-Ops

**Date:** 2026-06-10
**Status:** Accepted

**Decision:** On the Nicla target (`#ifdef NICLA_SENSE_ME`), LED class is a pure no-op (no SK6812 strip connected). `Adafruit NeoPixel` library excluded from Nicla `lib_deps` (incompatible with Mbed). Beeper uses `analogWrite()` instead of `tone()`/`noTone()` (AVR-specific, unavailable on nRF52).

**Rationale:** The Nicla has no SK6812 strip, no piezo transducer, no LDC1612 connected out of the box. These are external peripherals for the custom PCB. No-op stubs allow the full firmware to compile while only the sensor + BLE pipeline is tested on Nicla.

---

## AD-013: KiCad 8.x for PCB Layout

**Date:** 2026-06-10
**Status:** Accepted

**Decision:** PCB_layout uses KiCad 8.x (free, open-source). Custom symbol library (`sgc_custom.kicad_sym`) for SK6812-mini, LDC1612 coil, and piezo transducer. Standard KiCad libraries for all other components.

**PCB Spec:** 4-layer (Signal-GND-PWR-Signal), 22×55mm, 0.8mm FR4, ENIG finish, Tg≥150°C. Target prototype: 5 boards from JLCPCB (~$30).

**Files created:**
- `sgc_pcb.kicad_pro` — Project with 4-layer stackup, DRC rules, ERC config
- `sgc_custom.kicad_sym` — Custom symbols (SK6812-mini, LDC1612 coil, piezo)
- `sgc_pcb_netlist.md` — Complete per-pin wiring guide, 5 schematic sheets

---

## File Structure (Updated V-Model, 2026-06-10)

```
ski_gate_chrono/
├── sgc_architecture_decisions.md          ← AD-001 through AD-013
├── sgc_context_gemini_01.md               ← Original enriched plan
├── sgc_context_gemini_02.md               ← Working context (latest)
├── sgc_uwb_positioning_context.md         ← UWB analysis (future reference)
│
└── High_Level_Requirements/
    ├── sgc_requirements.md
    ├── acceptance_tests/                  ← V-cycle rising arm (empty)
    └── system_design/
        ├── sgc_system_design.md
        ├── system_tests/                  ← V-cycle rising arm (empty)
        └── architecture_modules/
            ├── sgc_architecture_devices.md
            ├── sgc_architecture_phone.md
            ├── sgc_architecture_hardware.md
            ├── sgc_bom.md
            ├── integration_tests/         ← V-cycle rising arm (empty)
            └── module_design/
                ├── unit_tests/            ← V-cycle rising arm (empty)
                └── implementation/
                    ├── Firmware_implementation/   ← ✅ 41 files, 2,754 lines
                    │   ├── platformio.ini
                    │   ├── include/config.h
                    │   └── src/ (sensors/ storage/ state_machine/ ble/ led/ beeper/ ...)
                    ├── PCB_layout/                ← ✅ sgc_pcb_layout.md + KiCad project
                    │   └── sgc_pcb/
                    │       ├── sgc_pcb.kicad_pro
                    │       ├── sgc_custom.kicad_sym
                    │       └── sgc_pcb_netlist.md
                    └── Phone_app_prototype/       ← (empty, pending)

---

## AD-010: BHI260AP Calibration Accuracy on Nicla

**Date:** 2026-06-18
**Status:** Resolved — quaternion magnitude check adopted.

**Problem:** The BHI260AP reports calibration accuracy (0-3) via
`BHY2_META_EVENT_SENSOR_STATUS` meta-events. The Arduino_BHY2 library
receives them but discards the value. `SensorQuaternion::accuracy()` returns
scaled sensor data at byte offset 8, not accuracy.

**Attempts:**
1. **Shadow BoschParser.cpp** — compilation errors (include path conflicts)
2. **`#define private public` + callback table** — required disabling
   `BHY2_CFG_DELEGATE_FIFO_PARSE_CB_INFO_MGMT` in platformio.ini. Callback
   fired correctly for sensor 31 (LACC → 3) but sensor 34 (RV) never emits
   SENSOR_STATUS on the Nicla's BHY2 firmware.

**Resolution:** Use a **quaternion magnitude check** (0.8 < |q| < 1.2) as
sensor-ready gate. A valid rotation quaternion always has magnitude 1.0;
a dead/uninitialized sensor produces zeros. This is library-independent,
works immediately, and catches the only real failure mode.

**Infrastructure kept:** `bhy2_cal_hook.cpp` captures meta-events for
diagnostics (`Ev:N` in `?` status). If the BHY2 firmware ever reports
RV calibration, the arming gate is a one-line re-enable.

---

## AD-014: Circular Flash Storage — FlashManager Implementation

**Date:** 2026-06-25
**Status:** Accepted

**Decision:** Replace the inline forward-only `g_next_run_addr` scheme with a proper
FlashManager class implementing the circular buffer specified in
sgc_architecture_devices.md §4. Flash layout revised to match the original design.

**Rationale:**
- `g_next_run_addr` was growing unbounded past 2 MB flash → HardFault at RAM boundary
- Requirement F08 (auto-overwrite oldest run) was unfulfilled
- No CRC32 per run meant no data integrity (R05)
- Design documents specified read_head/write_head indexing with wrap-around

**Implementation details:**
- FlashManager class: `storage/flash_manager.h/.cpp` — 430+ lines
- Flash layout: sectors 0-3 = ring buffer, 4-509 = circular run data, 510-511 = index
- Index sector holds magic, run_count, read_head, write_head, write_counter, and 255 run entries (32B each)
- create_run() checks overlap vs read_head, auto-deletes oldest runs when full
- close_run() writes CRC32 trailer (magic 0xC3 0x32 + CRC32_LE) and advances write_head
- Power-loss recovery: scans all valid RunHeaders to rebuild index on boot
- RunHeader aligned: added frame_count (uint16), kept data_size as uint32 (Phase 7b overflow fix)

**Coherence fixes applied in the same phase:**
- C4: RunHeader field alignment (+frame_count)
- C5: Ring buffer drain interleaving (pop-2/push-1 for 500 cycles)
- C6/C7: StartDetector thresholds + cumulative-drop-from-P₀ algorithm
- C8: EndDetector v4.0 — replaced flatline+IMU with 5 s elevation delta (0.5 Hz, 10-sample ring, altitude-adaptive PA_PER_M)
- C9: Quaternion mapping bug (x→w, y→x, z→y, w→z → fixed)
- C10: State machine enum values aligned to design
- C11: StartDetector at 10 Hz via separate timer

**Cordio heap fix (2026-06-25 22:00):**
Three interacting causes prevented BLE from initializing:
1. `CORDIO_ZERO_COPY_HCI=1` in Nicla's mbed_config.h causes `init_wsf()` to replace the system heap
   with the Cordio buffer pool (~4.9 KB). Fix: `#ifndef` guard + `-DCORDIO_ZERO_COPY_HCI=0`.
2. `BHY2.begin()` defaulted to `NICLA_BLE_AND_I2C`, starting its own BLE/I2C/DFU handlers
   that consumed nearly all remaining heap. Fix: `BHY2.begin(NICLA_STANDALONE)`.
3. BLE was initialized after BHY2, so the heap was already exhausted. Fix: BLE.begin() before BHY2.begin().

Diagnosed via heap probes (malloc 64/256/4096) at each init step, revealing <256 bytes free after BHY2.

⚠️ The framework patch (`~/.platformio/packages/framework-arduino-mbed/variants/NICLA/mbed_config.h`
line 52) must be re-applied after PlatformIO package updates.

**Date:** 2026-06-21  
**Status:** Accepted  
**Supersedes:** Phase 8 dual-path (`#ifdef TEST_MODE` / `#else`)

**Decision:** JSON-lines is the only serial output format in every build.
No `#ifdef` on output code. Test commands (`T`,`B`,`Q`,`L`,`Z`) and sensor
injection are also always compiled in — no `-DTEST_MODE` flag exists.

**Rationale:**
- Production enclosure is IP67 sealed — USB physically inaccessible
- Test commands are harmless without serial access; test mode starts OFF
- Bench test = worst-case timing (115200 baud TX blocks loop); production runs *faster* (UART FIFO drains instantly with nothing connected)
- Single code path = identical binary = what you test is what ships
- Removing `#ifdef` eliminates the "production path is never tested" problem

**Full ADR:** `unit_tests/adr_001_always_json.md`

---

## AD-015: Opt-A Raw Run Store (no LittleFS payloads)

**Date:** 2026-08-06 (spike/production), amended 2026-08-07 (v4.64 prep timing)  
**Status:** Accepted — **current production storage**  
**Supersedes for run payloads:** AD-014 FlashManager circular layout; LittleFS Phase 12 path (ADR-003 original)

**Decision:** Store compressed run payloads in **pre-erased raw SPI slots** (`RawRunStore`), not LittleFS and not FlashManager-only. Keep on-disk run format BLE-compatible: `[RunHeader 16B][frames…][0xC3 0x32 CRC32_LE]`.

**Rationale:**
- S04 measured LittleFS append on LOGGING at ~42–47 fps vs design target 100 Hz
- v4.62 raw pre-erased spike: **99.4 fps**, `we=0`
- Large RAM pre-roll rejected (Cordio OOM, v4.60)
- Full drop of structured storage rejected earlier for list/recovery; Opt A keeps index + multi-slot overwrite without LFS COW on the hot path

**Implementation (v4.63/v4.64):**
- 8 slots × ~249 KB from `0x6000`–`0x1FBFFF`; index sector `0x1FD000` (magic `RRS1`)
- FlashRing remains ARMED pre-roll only (`0x0000`–`0x5FFF`)
- **`prepare_next_run()`:** full-slot erase during **POST_RUN** (10 s cooldown) and **boot** — not at ARM (v4.64, JP)
- LOGGING: `program()` only when prepared; `run_saved.store == "raw"`
- LittleFS / FlashManager / spike writer: `.disabled`, not linked
- BLE FT + run_count use `g_runs`

**Tests:** S04 `test_bhy2_rate.py` — fps ≥ 90, store=raw. Multi-run and S03 stream smoke still to confirm on device.

**Full ADR:** `unit_tests/device/adr_003_littlefs_storage.md`

---

## AD-016: Linear ARMED pre-roll (prepare on enter IDLE)

**Date:** 2026-08-07  
**Status:** Accepted (v4.75)  
**Supersedes for pre-roll geometry:** circular 2-half / 3-region FlashRing

**Decision:** ARMED pre-roll is a **forward-only** flash buffer of **3000** frames (~30 s @ 100 Hz = `ARM_TIMEOUT`). Erase the entire buffer in `prepare_preroll()` when entering **IDLE** (from ARMED timeout/cancel or POST_RUN) and at boot. ARMED fill is **SPI program only** (no mid-fill erase). At LOGGING start, keep only the newest **1000** frames (~10 s) for encode/BLE/phone.

**Rationale:**
- Circular designs either limited live history to one half (5 s) or required erase-on-wrap during fill (FPS dips)
- Full ARM window can be recorded without wrap; product only needs ~10 s before start
- Same Opt-A idea as run slots: pay erase when athlete is stopped

**Flash:** `0x0000–0xEFFF` pre-roll; RawRunStore from `0xF000`.

**Tests:** S05 fill, S06 drain (`L` with tm=0), S04 live rate unchanged.

