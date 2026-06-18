# SGC Nicla Sense ME — Phased Build Plan
## 2026-06-16 · updated 2026-06-18

Goal: incrementally validate each hardware subsystem in isolation before combining.
Each phase: write → build → upload → power-cycle → test → confirm → NEXT.

---

### Phase 1: BLE Advertising ✅ DONE (06-16 07:05)
**Test:** "SGC" appears on BLE scanner. No Cordio 0x80FF0144 assert.

### Phase 2: BLE + BHY2 coexistence ✅ DONE (06-16 07:34)
**Test:** BLE advertises, BHY2 sensors stream. RAM 57%, Flash 58%.

### Phase 3: BLE + BHY2 + LED ✅ DONE (06-16 08:46)
**Test:** Onboard RGB breathing blue. Fix: `nicla::begin()` inits Wire1.

### Phase 4: BLE + BHY2 + LED + Battery ✅ DONE (06-16 09:04)
**Test:** Battery % on BLE characteristic. PMIC via Nicla_System.

### Phase 5: State Machine ✅ DONE (06-16 09:50)
**Test:** Serial commands (i/a/l/p/s/?). LED patterns follow state.
Timeouts: ARMED→IDLE 30s, POST_RUN→IDLE 10s, IDLE→SLEEP 2min.
Bugs fixed: `BLE.stopAdvertise()`, cooldown spam, POST_RUN auto-transition.

### Phase 6: Flash Storage ✅ DONE (06-16 12:04)
**Test:** MX25R1635F via `mbed::BlockDevice::get_default_instance()`.
Erase, write 256B, read-back, verify. Flash on SPI1 (p4/p5/p3, CS=p26).

### Phase 7: Full Pipeline 🔄 CURRENT

#### 7a — Core Pipeline ✅ DONE (06-16 20:42)
- [x] Flash-based ring buffer: 4 blocks, 1000-slot circular, 500-frame sliding window
- [x] RawFrame: 16B (quat 8B + lin_acc 6B + baro/4 2B). CompressedFrame: 22B
- [x] BitPacker: delta encoding (quaternion diffs, acc/baro pass-through)
- [x] StartDetector: dual-mode — baro speed (per-sample delta) or drop (min-tracking)
- [x] EndDetector: 10s ACC stillness
- [x] Run headers: 16B per run (format ver, arm side, timestamp, data size)
- [x] Multi-run: sequential stacking on flash, block-aligned

#### 7b — Persistence & Integrity ✅ DONE (06-16 20:57)
- [x] Run index sector (block 4, magic 0x53474300, survives reboot)
- [x] Factory reset ('R' command or proximity hold, erases index + data)
- [x] Run header includes calibration accuracy, baro temp
- [x] `data_size` fixed: uint16_t → uint32_t (truncation at 65 KB → now 4 GB) — 06-18
- [ ] CRC-16 per frame (deferred — low risk, flash is reliable)

#### 7c — Sensors & BLE Integration ✅ DONE (06-16 20:57)
- [x] BLE characteristics: state, battery, transfer status (`...ABCD`), calibration (`...ABCE`)
- [x] Battery low detection (< 15% forces SLEEP from LOGGING)
- [x] Qi charging pin configured (P0.10 = digital pin 0, INPUT_PULLUP)
- [x] Beeper PWM on ARMED (P0.09 = digital pin 1, PWM piezo)
- [x] Calibration accuracy read from BHY2 rotation.accuracy(), pushed to BLE

#### 7d — Integrity #### 7d — Integrity & Completeness 🔄 IN PROGRESS Completeness 🔄 IN PROGRESS (7d-1 done, 4 remaining)
- [x] 7d-1: **Calibration gate** ✅ DONE (06-18 16:15) — accuracy unscaling fix
- [x] 7d-1b: **Start detector** — only feed after ring full, fix baseline drift (06-18) — refuse arming when BHI260AP accuracy < 2 — refuse arming when BHI260AP accuracy < 2 (P05)
- [ ] 7d-2: **BLE module refactoring** — extract `src/ble/sgc_service.cpp` + `file_transfer.cpp` from main.cpp
- [ ] 7d-3: **BLE file transfer** — Run List characteristic (`.ABC9`), chunked download protocol, CRC32 validation
- [ ] 7d-4: **CRC32 on run data** — append CRC32 at run close, validate on phone download
- [ ] 7d-5: **Deep sleep** — enter WFE on SLEEP state, actual µA-level current draw

#### 7e — Hardware-Dependent (needs custom PCB or breakout)
- [ ] **LDC1612 proximity arming** — I2C1 driver, 1000 ms hold detect, INTB wake from sleep
- [ ] **SK6812 LED strip** — level shifter (74HCT1G125) or battery-rail power, NeoPixel protocol on nRF52
- [ ] **UWB + RFID** — v2 only (unpopulated footprint)

### Phase 8: Phone App ↔ Device Integration (pending)
- [ ] 8a: BLE scan fix — runtime permission check on Android 12+ (added 06-18, untested)
- [ ] 8b: File download + decompression + CRC validation
- [ ] 8c: Impact detection + gate timestamp table
- [ ] 8d: Left/right arm cross-correlation
- [ ] 8e: Cloud upload (gate timestamps + baro, not raw 100 Hz)

### Phase 9: Enclosure + Field Testing (pending)
- [ ] 9a: PCB layout (KiCad) — Nicla form factor, LDC1612 footprint, SK6812 strip connector
- [ ] 9b: 3D-print enclosure (polycarbonate translucent for LED light piping)
- [ ] 9c: Battery selection (2× 300 mAh Li-Po in parallel, ≤ 4 mm each)
- [ ] 9d: On-slope field test — real gates, real athlete, compare vs. manual timing

---

### v0.7.3 — tagged 2026-06-18 15:44 UTC
Phase 7c complete. 176 files committed. Repo at `ski_gate_chrono/`.
