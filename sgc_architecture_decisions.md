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

**Flash (v4.77+):** `0x0000–0x13FFF` pre-roll **4000** slots (3000 ARM + 1000 drain headroom); RawRunStore from `0x14000`.

**Drain:** pop 2 + push 1 live while backlog remains; if pop empties ring, encode live direct (never pop1+push1 at r=1).

**Baseline tag:** `v4.78-best-preroll`. Suite: `system_tests/run_device_suite.py` (S04–S06).

**Tests:** S05 fill, S06 drain (`L` with tm=0), S04 live rate unchanged.

---

## AD-017: Pole-Mount as v1 Primary Form Factor

**Date:** 2026-08-11
**Status:** Accepted
**Supersedes:** Forearm guard as v1 primary (moved to v2/future)

**Decision:** The v1 production device mounts on the upper ski pole shaft, just below the handgrip. The electronics face the back of the pole (protected from direct gate impacts). Two devices per athlete (one per pole). Forearm guard form factor is deferred to v2 as a separate premium product variant.

**Rationale:**

Forearm guard development would require:
- Injection-molded polycarbonate shell (180–220 mm), multi-cavity tooling
- EVA foam padding, velcro straps — multiple SKUs per size (S/M/L)
- Epoxy/silicone potting automation
- 200g shock rating for direct gate impacts
- Cross-arm LDC1612 proximity arming with passive target discs
- Complex multi-step mechanical assembly

None of this is manageable by a solo developer. The pole mount eliminates all of it:
- Small enclosure (~25×25×70 mm), O-ring sealed — 3D-printable for prototypes
- Standard pole strap/clamp attachment — one SKU fits all pole diameters
- Pole shaft absorbs gate impacts — device on back side is shielded
- Simple reed switch arming (no LDC1612, no target disc)
- Single PCB, single assembly step

**What stays identical:**
- All firmware (sensors, state machine, storage, BLE) — 100% reusable
- Phone app (gate detection, course setup, cloud) — unchanged
- Sensor complement (BHI260AP, BMP390) — unchanged
- PCB (Nicla replica or companion carrier) — unchanged, just different enclosure

**What changes:**

| Aspect | Forearm Guard (→v2) | Pole Mount (v1) |
|--------|---------------------|-----------------|
| Enclosure | 180–220mm PC shell | Small tube, O-ring seal |
| Mounting | Velcro straps on forearm | Strap/clamp on pole shaft |
| Arming | LDC1612 cross-arm proximity | Magnetic reed switch (poles-together) |
| LDC1612 | Active (cross-arm target disc) | Removed from active BOM |
| Device count | 1 per arm = 2 per athlete | 1 per pole = 2 per athlete |
| Left/right | Identified by arm side | Identified by pole side (same logic) |
| Impact path | Direct forearm strike | Through pole shaft (shielded) |
| Shock rating | 200g | Pole-flex absorbed (lower) |
| SKUs | 6 (L/R × S/M/L) | 1 |
| Production COGS | ~€105–134 (P3) | ~€50–70 (est.) |
| Retail price | €249–349 | €149–199 (est.) |

**Arming — Reed Switch (AD-017.1):**

A normally-open magnetic reed switch sits inside the sealed device enclosure. A small neodymium magnet is embedded in each pole grip (or a silicone band around the grip). At the start gate, the athlete brings both pole grips together → each magnet triggers the opposite pole's reed switch → both devices arm simultaneously after 1000 ms continuous hold (same timing as F03).

Advantages over alternatives:
- **No mechanical penetration** — reed switch is glass-encapsulated, enclosure stays IP67
- **No moving parts** — rated for 10⁷+ cycles, no freeze/jam risk at −20°C
- **One gesture, two devices** — same "bring together" motion as original cross-arm design
- **Glove-friendly** — large activation zone, no small button to find
- **Cost:** ~€0.30 for reed switch + €0.50 for magnet

**Electronics placement:** Device mounts on the BACK of the pole (trailing side, away from gates). Gate impacts hit the front of the pole; the shaft flex absorbs energy and the device is shielded. The SK6812 LEDs face upward/outward for athlete visibility. The beeper faces inward (against the pole) for transmission through the shaft.

**Mounting:** Simple silicone-coated elastic strap with a quick-release buckle. Wraps around the pole shaft and the device body. No clamp mechanism needed for v1 — strap tension + silicone grip is sufficient for a 40–50g device.

**Forearm guard as v2:** The forearm guard is preserved in all documentation as a future product variant. It would reuse the same electronics module in a larger protective shell. The LDC1612 and cross-arm arming would be reintroduced for that variant. AD-017 does NOT delete forearm guard content — it re-frames it as "v2 / future development."

**Impact on existing documentation:**
- Hardware enclosure (§8) → rewritten for pole-mount, forearm guard moved to §8bis
- Production strategy → simplified; injection molding deferred to v2
- Requirements H04 (inductive through shell) → marked v2; new H14 (reed switch arming)
- Requirements H05 (thickness <16mm) → relaxed for pole mount
- Requirements H06 (weight ≤40g) → tightened to ≤50g
- Interface I06 (cross-arm) → marked v2; new I12 (reed switch → nRF52 GPIO)


## AD-018: Piezo-Button Era Documentation Sync (HLR v6.0)

**Date:** 2026-09-10 · **Status:** accepted (JP directives, documentation-only)

Arming, wake, and factory reset are exclusively the sealed piezo button on
P0.02 (single press = arm; 5 presses in 3 s = factory reset; GPIO sense =
wake from SLEEP and System Off). The LDC1612 is physically removed from the
board (2026-08-22) — inductive sensing abandoned (unmanageable on metallic
poles); the v5.9 reed switch was never built. Start detection = barometric
drop only (> 2.0 m from P₀ — descent-speed mode dropped). Sleep model =
immediate SLEEP + System Off after 1 h unconnected. Pre-roll = 10 s (1000
frames) in Flash, ARM cap 30 s, drain = pop-2 + push-1 (net −1/10 ms). No
beeper in v1 (DNP footprint). Charging = sealable USB-C (Qi dropped). BMM150
unused (H08 N/A). BLE MTU target ≥ 500 B payload (pull transport — see
PULL_TRANSFER_REDESIGN.md). F03a proposed: one-press-arms-both via
advertising flag (not implemented).

**Docs updated:** sgc_requirements.md v6.0, sgc_system_design.md,
integration_tests/hardware.md, acceptance_tests/device.md,
requirements_traceability.md, sgc_architecture_hardware.md v2.3,
sgc_context_gemini_01.md (marked frozen). Historical sections preserved.

## AD-019: ANNA-B402 Module + Direct-to-Custom-Board Production

**Date:** 2026-09-10 · **Status:** accepted (JP directives, documentation-only)

The custom production board uses the **u-blox ANNA-B402** (open CPU,
nRF52833). Decisive criterion (JP, 2026-09-10): the **B402 is open CPU** —
we run our own firmware on it, the exact same code as the B112-based Nicla
Sense ME line — while the **B412 is a u-connect module** (u-blox's closed
AT-command stack, cannot host our code) and is therefore rejected. The **companion
carrier PCB concept is dropped** — there is no Nicla-on-carrier production
phase. Hardware phases: Nicla Sense ME prototypes (P0) → standalone custom
PCB with ANNA-B402 (P1+). B112 pin-map tables in the hardware architecture
doc must be revalidated against the B402 datasheet during the PCB redesign.
Also confirmed: no IDLE state in the production state machine (removed
FW V5.19; SLEEP → ARMED directly on button press).

**Docs:** sgc_architecture_hardware.md v2.4, sgc_production_strategy.md
v1.6, sgc_bom.md note, sgc_system_design.md (IDLE note), HLR v6.0 (F13).

## AD-021: Stable Monotonic Run-Ids on the Wire, Deletes Leave Holes

**Date:** 2026-10-27 · **Status:** accepted (implemented FW 0.22.1, bench + phone verified — TC-2026-10-27-002)

**Decision:** Every wire id in the app-03 `pull_proto` protocol — the 0xABC9 listing `"id"`, the `D` directory `"run"` field, and the `s`/`H`/`d` arguments — is the device's **monotonic run_id** assigned at record time, never the `entries[]` position. Per-run delete (`d <id>`) is index-only removal with lazy sector reclaim: ids never shift, holes in the sequence are expected and permanent, and the command is idempotent (unknown id → `OK already gone`, same reply on repeat). Requests resolve via `store_run_pos()`; unknown ids take the not-found paths.

**Rationale:**
- Position-based addressing made a mid-transfer delete shift what the phone was about to download — a structural race. Stable ids remove the hazard entirely (the `E 4` transfer-busy guard remains as defense in depth).
- Erasing on the delete critical path is unnecessary: sectors are reclaimed lazily by the existing budget/runway logic (F62).
- Idempotency makes the phone's offline delete backlog safe to replay (any-time application cannot double-delete or hit the wrong run — backlog entries are also mac-stamped, AD-025).

**Evidence:** bench listing 0..18, 20..38 with hole at 19, zero shift after `d 19`; repeat `d 19` → `OK already gone`; `s` streams by run_id. Ledger TC-2026-10-27-002.

## AD-022: Naming Convention — Device Letters + letter#counter + Display-Only Day Numbering

**Date:** 2026-10-27 · **Status:** accepted (JP design; reqs v6.5 §Naming Convention; implemented App 1.69/1.72)

**Decision:** Two numbers, two jobs. (1) **Device letters:** every newly paired device gets a permanent letter (A, B, C…, strictly increasing per NEW device, rollover past Z); a re-paired known device keeps its letter; side detection is NOT identity — the assigned letter is. (2) **Run default name** = `<letter>#<device_counter>` (e.g. `A#123`), computed from the firmware's per-device run id, never stored; a custom F45 name overrides locally, and the effective name is uploaded in the cloud `name` field so local and cloud display the identical string by construction. (3) **Device run list** shows one day at a time (day pager) with per-day numbering 1..N assigned **at display time only** — never stored, never uploaded, renumbered on delete, R-before-L tiebreak. (4) **Merged default name** = `<A#x> + <B#y>` from the sources' defaults. (5) **Cloud ids are never displayed**; cloud list orders newest-first.

**Rationale:**
- Side detection is unreliable in slalom; a burned-in letter is a stable identity the athlete can read on the enclosure.
- The device run-id is already unique and monotonic (AD-021) — reusing it in the name gives every run a human-speakable handle (`A#32`) at zero storage cost.
- Display-only per-day numbering keeps storage clean (renumbering on delete would otherwise churn flash/cloud rows) while giving the athlete a stable mental model per day.
- Carrying the effective name in the upload payload kills the local/cloud title mismatch class of bugs.

**Spec of record:** `sgc_requirements.md` v6.5 §Naming Convention; implementation detail `remote_db_architecture.md` §13.

## AD-023: Synch-as-State — Per-Run Cloud State, Desync as Sole Cloud-Exit, No Implicit Resurrection

**Date:** 2026-10-27 · **Status:** accepted (JP redesign 13:45; implemented App 1.72.0)

**Decision:** "Synch to cloud" is a first-class **per-run state** (checked = the run IS on the cloud), not a delete option. Rules:
1. **Desync (ON→OFF) is the only cloud-removal path:** cloud `DELETE`, the durable queue entry moves to DESYNCED (dead — the backfill worker can never re-upload), pending rename-outbox entries are dropped; phone + device copies are untouched.
2. **Re-sync (OFF→ON) is an explicit narrow upload:** same `client_run_uuid` → the server creates a **NEW row** (the old id is never resurrected); only that run is uploaded, bypassing the global `push_to_cloud` gate (F20 must not veto a direct per-run command).
3. **Delete never touches the cloud:** phone + device legs only (AD-025). Unsynced run → strong total-loss double-confirm; synced run → honest "cloud copy is kept" scoping.
4. **Cloud Runs screen:** every row carries a Synch switch; **orphan rows** (no local copy behind the `client_run_uuid`) can never re-sync — they get a delete icon + definitive warning.
5. **No implicit resurrection anywhere:** deleting a cloud-only run leaves the local DONE queue entry silent; the merged backfill skips shrunk entries; auto-retries never bypass the push gate.

**Rationale:**
- App 1.71's delete-with-cloud-checkbox conflated two orthogonal decisions ("remove from phone/device" vs "remove from cloud"), produced dishonest confirmations, and stranded cloud rows. A state switch models reality: cloud membership is a boolean the user can flip both ways.
- Making desync the sole cloud-exit gives exactly one code path that removes cloud data — auditable, and compatible with GDPR intent (explicit, confirmed, irreversible).
- New-row-on-resync (no id resurrection) keeps lineage and audit semantics honest: a re-uploaded run is a new cloud object, even if it carries the same client uuid.

**Spec:** `remote_db_architecture.md` §14.3.

## AD-024: Upload-Everything Policy — Singles and Merged as Distinct Types with Lineage

**Date:** 2026-10-26 · **Status:** accepted (JP directive "Go!!" 19:06; reqs v6.4; deployed)

**Decision:** The phone uploads **all** runs — every single AND every merged run — to the cloud. Merged runs are a distinct `run_type = "merged"` (vs `"single"`) carrying `source_run_ids` lineage pointing at the cloud ids of their source singles (may be empty if the sources were never uploaded or were deleted).

**Rationale:**
- Merges are derived data: keeping the singles server-side allows future re-derivation with better algorithms without re-fetching from devices.
- Single-run analyses (cross-arm technique comparison) need singles anyway; a merged-only cloud would lose information.
- Distinct typing beats one blended blob: the API can order/filter by type, and the lineage list makes provenance queryable instead of guessable.

**Schema:** `runs.run_type ∈ {single, merged}` (default single), `runs.source_run_ids` — reqs v6.3/v6.4 REQ-DB; API behavior `remote_db_architecture.md` §4/§15.

## AD-025: Phone-Initiated Device Deletion with Offline Backlog (mac-stamped, applied pre-listing)

**Date:** 2026-10-27 · **Status:** accepted (implemented App 1.70–1.72 + FW 0.22.0/0.22.1)

**Decision:** The device leg of the delete triad is initiated by the phone via the BLE console `d <id>`. When the device is not connected, the delete lands in a durable `device_delete_backlog.json`, stamped with the **target device MAC**, and is applied on **every connect before any listing/download batch**. Backlog application is idempotent (AD-021) so replays are harmless.

**Rationale:**
- Delete must work offline: an athlete deletes in the evening, connects at the slope tomorrow. Late application after a listing/download would re-download the very run the user asked to erase — hence strictly pre-listing ordering.
- The mac stamp means a recycled run id on a *different* device can never consume the wrong backlog entry.
- Phone-initiated (device executes) keeps the UI single-surface: one Delete action fans out to the legs it can reach now, queues what it cannot.

**Legs:** cloud first (`DELETE /v1/runs/{id}`; 404/410 = already gone = success) → phone local removal → device live-or-backlog. Full table: `remote_db_architecture.md` §14.1.

## AD-026: One-Time Name Reconciliation with Custom-Name Protection

**Date:** 2026-10-27 · **Status:** accepted (implemented `sgc-api/scripts/reconcile_names.py`, 42 tests green)

**Decision:** A one-time, idempotent server-side backfill stamps machine-generated names (`A#x` / `A#x + A#y`, AD-022) onto cloud rows uploaded before the phone started sending names. It touches **only** rows whose `name` IS NULL or already matches the machine-generated pattern; a **custom user name is never matched, hence never overwritten**. Merged rows are recomposed from each source's `run_number` resolved via `source_run_ids`; empty/incomplete lineage leaves the row untouched and logged. Ships in the `sgc-api` image with `--dry-run` support.

**Rationale:**
- Legacy rows predate App 1.69's name-in-payload; without a backfill the cloud list shows a mix of named and nameless rows forever.
- Server-side (not a phone migration) means it runs once against the live DB regardless of which phone versions ever connected.
- The narrow match pattern makes the operation safe-by-construction on user data: F45 custom names are exactly the rows the regex cannot match.

## AD-027: Cloud Retention — 1-Year Nightly Purge + Nightly WAL-Safe Backups (30 d)

**Date:** 2026-10-26/27 · **Status:** accepted (JP directive; reqs v6.3; deployed as nightly ops job)

**Decision:** Cloud run data (runs + timestamps + barometric_data) older than **1 year** is purged by a **nightly server-side job** (`app.routers.retention`); phone-local copies (including raw 100 Hz data) are unaffected. Independently, the SQLite database is backed up nightly (WAL-safe copy) with **30-day retention**. Activation-window semantics for expired accounts: writes blocked, reads + deletion always allowed.

**Rationale:**
- The cloud stores small derived data (F23) whose value decays — a season-old trace has little training utility; the purge caps storage growth and GDPR exposure deterministically.
- WAL mode makes a plain file copy racy; the nightly backup uses the WAL-safe checkpoint/copy path so a backup never tears a transaction.
- Retention and backup cadence are deliberately decoupled: purge governs the live DB, the 30-day backup window governs recoverability.

**Spec:** `sgc_requirements.md` v6.3 REQ-DB §Data Retention; `remote_db_architecture.md` §5.

## AD-028: Cloud Deployment — Caddy + sgc-api Container on the Hostinger VPS, Bootstrap Indirection (I07)

**Date:** 2026-10-27 · **Status:** accepted (LIVE: sgc-api.vytsolutions.com)

**Decision:** The cloud backend runs as the **`sgc-api` container** (FastAPI + SQLAlchemy + SQLite WAL) on the existing Hostinger VPS, fronted by **Caddy** for automatic Let's Encrypt TLS — HTTPS-only per I07, HTTP → redirect. The DB port is never published; the API listens on the internal proxy network. The app resolves the API base via **bootstrap indirection**: a static JSON at the hardcoded bootstrap address (`{"api_base": …, "min_app_version": …}`) so the API can move host/domain without an app release. Nightly ops inside the same host: retention purge (AD-027) + WAL-safe backups.

**Rationale:**
- The VPS already exists; one more container with no published DB port is the lowest-ops footprint that still gives TLS, isolation, and `docker compose` lifecycle.
- Caddy beats nginx+certbot for a single-service host: zero-config renewal, HTTP/3 for free.
- SQLite WAL is sufficient for the P1 single-writer profile; the ORM keeps the PostgreSQL flip (P2) a config + migration run, not a rewrite.
- Bootstrap indirection decouples the hardest-coded constant in the system (the bootstrap URL in the app) from everything that may move.

**Deployed state:** `remote_db_architecture.md` §3; runbook `implementation/sgc_api/README.md`.

## AD-029: Fuel Gauge on v2 — MAX17048 @ 0x36 on the Power Bus (i2c0/P0.15–16)

**Date:** 2026-10-28 · **Status:** accepted (JP directive 07:40 UTC; documentation-only)

**Decision:** The v2 custom PCB adds a **MAX17048G+T10** (ADI/Maxim 1-cell ModelGauge,
TDFN-8, no sense resistor, ~3 µA hibernate) on the **power-management bus — Zephyr
`&i2c0`, P0.15/P0.16** (Arduino "Wire1"), fixed address **0x36**. The BQ25120A's
voltage-band gauge is **demoted to fallback**. /ALRT is wired to a spare GPIO via a
10 kΩ pull-up for firmware freedom; its use is v2-optional (low-battery interrupt),
behavior TBD.

**Rationale:**
- The BQ encoding resolves only **60–100% in 2% bands** — bench: the device reported a
  **constant 92% across all states**, information-free in our operating range (the
  <60% floor already needs the 0.23.2 sentinel hack in `ble_config.c`).
- The MAX17048 gives true 1% SoC over the full 0–100 range from a single register
  read (RepSOC, 0x04); industry-standard part that pairs naturally with the BQ25120A
  (same bus, trivial driver, hibernate ≈ 3 µA keeps the SLEEP budget intact).
- Bus choice = minimal code impact: all power peripherals on one bus — no sensor-path,
  no new bus init, no acquisition-path change. Address 0x36 conflicts with nothing:
  i2c0 = 0x36 gauge / 0x53 IS31FL3194 LED / 0x6A BQ25120A charger; i2c1 (sensors,
  P0.22–23) = 0x2A LDC1612 (DNP) + 0x52 RV-3028 RTC (DNP), BHY2 hub on SPI.
- **App-side unchanged:** the ABC5 battery characteristic keeps its byte semantics
  (bit7 charging from BQ FAULTS, bits6–0 = %) — it now simply carries real 1% data.

**Firmware note:** `ble_config.c` only — `batt_work_handler` reads RepSOC @ 0x36 on
`i2c0` as the preferred source; when the gauge NAKs (P0 Nicla prototypes have none),
the existing BQ25120A VBMON chain runs unchanged as fallback — one binary serves both
platforms.

**Hardware detail (datasheet-checked):** on the single-cell MAX17048 the *sense* input
is **VDD** (pin 3 → VBAT + 0.1 µF); pin 2 "CELL" is not internally connected (it is
the 2-cell MAX17049's second-cell input) and ties to GND. Schematic notes:
`sgc_architecture_hardware.md` §7.

**Docs:** sgc_architecture_hardware.md v2.5 (§2 maps + §7 new subsection),
sgc_bom.md v4.3 (lines #10/#11; device totals $62.60/$44.19/$27.07),
sgc_production_strategy.md v1.7 (cost + margin deltas).
