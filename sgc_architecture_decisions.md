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

## AD-010: Shadow BoschParser.cpp for Calibration Accuracy

**Date:** 2026-06-18
**Status:** Accepted

**Decision:** Copy `BoschParser.cpp/.h` from the Arduino_BHY2 library into
`Firmware_implementation/src/` and apply a one-line patch to capture BHI260AP
calibration accuracy from BHY2 meta-events.

**Rationale:**
- The BHI260AP reports calibration accuracy (0-3) via
  `BHY2_META_EVENT_SENSOR_STATUS` meta-events, NOT in the Rotation Vector
  data packet at byte offset 8.
- `SensorQuaternion::accuracy()` returns `data[8:9] * scaleFactor` which
  is raw sensor data (e.g. 785), not calibration level (0-3).
- The library's `parseMetaEvent()` receives the correct value but discards
  it (only prints when debug is enabled).
- Accessing `bhy2_dev*` to register our own callback requires private
  library internals.

**Implementation:**
- Copied `BoschParser.cpp` into `src/` (PlatformIO prefers local source).
- Added `volatile uint8_t g_bhy2_accuracy[256]` global array.
- In `parseMetaEvent()`, before the `if (_debug)` block:
  `if (meta_event_type == BHY2_META_EVENT_SENSOR_STATUS) g_bhy2_accuracy[byte1] = byte2;`
- SGC reads `g_bhy2_accuracy[34]` for sensor ID 34 (SENSOR_ID_RV).

**Trade-off:** If Arduino_BHY2 library is updated, the shadowed file must
be refreshed from the new version and the one-line patch re-applied.
Acceptable — library updates are rare and the patch is trivial.
