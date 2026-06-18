# SGC — Context Summary (2026-06-09)

## Architecture Pivot — v2 Decisions

### 2026-06-09 Working Session

**Major decisions:**

1. **No UHF RFID reader on athlete device.** The $30 RFID module was 42% of BOM. Removed from active BOM — unpopulated footprint only on PCB for potential v2.

2. **No BLE beacons on poles.** No per-gate electronics for v1. No course infrastructure cost.

3. **No UWB.** Unpopulated footprint only — hedge for v2 if snow cannon anchors materialize.

4. **Device is a dumb logger.** IMU + barometer → Flash at 100 Hz. No gate detection on-device. No BLE scanning. Phone does ALL math post-run.

5. **Gate detection via pressure ΔP + IMU.** Trainer registers course pressure map (deltas from START) with phone. Post-run, phone matches pressure trace against stored deltas, cross-referenced with IMU turn kinematics.

6. **Two operational tiers (Silver removed):**
   - Gold: trainer loaded a course map (today or updated old) → pressure ΔP + IMU → ±50–100ms
   - Bronze: no map → IMU turn counting + impact detection only
   - Trainer explicitly chooses which map to use. No automatic fallback to old data.

7. **Course setup — two modes:**
   - Mode A (New Course): sequential recording, no detection. Trainer walks in order, taps to record each gate.
   - Mode B (Update Existing): GPS + ΔP detection. Phone highlights nearest gate. Three actions: Move (record new position), Delete (remove), Add (insert after).
   - Dual view: graphical map (GPS available) ↔ text list (always available).
   - Partial update: trainer can update only changed gates from an old map.

8. **Dual start detection:** Vertical speed (>1.5 m/s for 200ms) OR vertical drop (>2.0m from arming P₀). Whichever fires first. No toggle needed.

9. **Corrected kinematics:** ω zero-crossing = transition between gates, NOT at gate. Gate occurs during active rotation (|ω| > threshold). Between two zero-crossings: 1 or 2 gates (banana). Learned spatial percentage A (F26 Case B) for estimated gate placement — not ω midpoint.

10. **Pressure interpolation for missed gates:** Find t where P(t) = P_start + ΔP_n via linear interpolation.

11. **Course map format:** START at index 0 with ΔP=0. Relative GPS vectors (ΔGPS from previous gate). All deltas from START pressure.

12. **BOM:** $40/device, $81 two-arm kit, ~$243 retail (3×). Athlete saves $183 vs. RFID design ($426).

13. **PC Coach Dashboard:** Full trace visualization, gate comparison, CSV export, multi-stream architecture for future body-part tracking (boots, waist, helmet via F11 mount_type extension).

### Fixes Applied (2026-06-09 — Post-Review)

19 issues found and fixed across Grok + manual review:

**HIGH (8 fixed):**
- H1: SETUP state removed from state machine (system_design.md + devices.md)
- H2: Pin map corrected in devices.md config.h to match hardware.md
- H3: Start detector feed-rate bug fixed (SPEED_WINDOW=2, denominator=0.1, oldest index corrected)
- H4: Flash sector numbers corrected (512 sectors, 0-509 data, 510-511 index)
- H5: All `sgc_architecture_v2.md` cross-refs → `sgc_architecture_decisions.md`
- H6: LOGGING state diagram: "UHF RFID active" → "⚠️ v2 only (unpopulated)"
- H7: LED table SETUP/white row marked ⚠️ v2 only
- H8: Session budget header + H02 traceability updated for no-RFID numbers

**MEDIUM (8 fixed):**
- M1: Silver tier removed from phone arch (header + table + pipeline)
- M2: All `sgc_architecture_modules.md` → `sgc_architecture_devices.md`
- M3: Phone arch §6.1 old SETUP/NFC/RFID course setup → Mode A/B flow
- M4: Ring buffer 20 bytes → 16 bytes (encoding overhead is Flash-time only)
- M5: LED class → SK6812-mini strip (was old 3-GPIO PWM)
- M6: Power tree MOSFET for RFID → marked ⚠️ v2 only
- M7: Pin P0.20 "RFID VDD enable" → marked ⚠️ v2 only
- M8: F58-F61 + P09 + H11 + H12 + I10 + course_gates schema marked ⚠️ v2 only

**LOW (3 fixed):**
- L1: Phone arch §5 RFID L/R note → marked ⚠️ v2 only
- L2: context_gemini_01.md second stale module ref fixed
- L3: GateTimestamp/CourseGate rfidTagId docstrings → marked ⚠️ v2 only

**Additional cleanup:**
- system_design.md module decomposition: pole_setup → course_setup, led.cpp description updated, rfid_reader marked v2 only
- Stale "added SETUP=4" coherency note removed from system_design.md header

### Open Questions

- Pressure delta stability: how much does ΔP drift across hours/days? (Bench test needed)
- BMP390 noise floor at 100 Hz: sufficient for 0.25m altitude resolution?
- Gate detection validation: synthetic data first, then real slalom runs
- PC Dashboard: Electron? Flutter Desktop? Web app?
- Minimum gate spacing for unambiguous pressure detection? (Especially on flats)

---

## 2026-06-10 — Firmware Implementation + PCB Layout

### Firmware (41 files, 2,754 lines)

**Build system:** PlatformIO with `nordicnrf52` platform, two targets:
- `nicla` — Nicla Sense ME prototyping (framework-arduino-mbed, Mbed OS)
- `custom_pcb` — Custom nRF52832-QFAA PCB (framework-arduinoadafruitnrf52, Adafruit core)

**Build results:** Both targets compile clean. Nicla: 276 KB Flash (52%), 46 KB RAM (72%).

**Module structure** (per sgc_architecture_devices.md §1):
```
src/
├── main.cpp          (setup + loop, state dispatch, sensor pipeline)
├── sensors/          (bhi260ap, bmp390, ldc1612 — I2C drivers)
├── storage/          (ring_buffer, bit_packer, flash_manager, run_file, spi_bus, gate_event)
├── state_machine/    (state_machine, start_detector, end_detector)
├── ble/              (sgc_service, file_transfer)
├── led/              (SK6812-mini NZR driver — Nicla: no-op stubs)
├── beeper/           (analogWrite PWM — tone() unavailable on nRF52)
├── battery/          (ADC VBAT monitor, Qi detect)
├── sleep/            (WFE low-power, LDC1612 wake)
└── rfid_reader/      (v2 stub)
```

**Key fixes applied during compilation:**
1. `RingBuffer` → `SGCRingBuffer` (Mbed class name collision)
2. `config.h` uses `#ifdef NICLA_SENSE_ME` for pin separation
3. `Wire.write()` casts to `uint8_t` (Mbed ambiguous overload)
4. Stray `#endif` removed from bmp390/ldc1612/file_transfer
5. `tone()` → `analogWrite()` for beeper (AVR-specific)
6. CMSIS `<core_cm4.h>` added for `__WFE()` / `NVIC_SystemReset()`
7. `Adafruit NeoPixel` excluded from Nicla target (Mbed incompatible)
8. `-I` include paths added to platformio.ini for all subdirectories
9. Mbed framework warnings suppressed (`-Wno-sign-conversion`, `-Wno-conversion`, `-Wno-pedantic`)
10. Blew through 6 wrong platform URLs before landing on `nordicnrf52` + `nicla_sense_me` (required `pio platform update nordicnrf52`)

### PCB Layout

**KiCad 8.x project** in `PCB_layout/sgc_pcb/`:
- `sgc_pcb.kicad_pro` — 4-layer stackup, DRC rules, ERC config
- `sgc_custom.kicad_sym` — Custom symbols for SK6812-mini, LDC1612 coil, piezo transducer
- `sgc_pcb_netlist.md` — Complete wiring guide (5 sheets, per-pin netlist)

**Board specs:** 4-layer FR4, 22×55mm, 0.8mm, ENIG, Tg≥150°C
**Stackup:** Signal (L1) – GND (L2) – PWR (L3) – Signal (L4)
**PCB prototype:** 5 boards from JLCPCB (~$30)

**Next to do:**
1. Place components + wire in KiCad per netlist
2. Download missing footprints from SnapEDA (BHI260AP LGA-44, BMP390 LGA-10, LDC1612 WSON-12, BQ25100 DSG-8)
3. Phone_app_prototype (last empty implementation folder)
