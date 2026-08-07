# SGC Nicla Sense ME — Phased Build Plan
## 2026-06-16 · updated 2026-08-07 (Opt-A v4.64)

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

### Phase 7: Full Pipeline ✅ DONE (06-18)

#### 7a — Core Pipeline ✅ DONE (06-16 20:42)
- [x] Flash ring buffer (1000-slot circular, 500-frame window)
- [x] RawFrame 16B, CompressedFrame 22B, BitPacker delta encoding
- [x] StartDetector (barometric drop, altitude-adaptive PA_PER_M, noise-gated diagnostics)
- [x] EndDetector: 5 s elevation delta, 0.5 Hz sampling (10-sample ring, 40 B RAM). Replaced flatline+stillness derivative (v4.0)
- [x] Run headers 16B, multi-run block-aligned flash storage

#### 7b — Persistence & Integrity ✅ DONE (06-16 20:57)
- [x] Run index sector (block 4, magic 0x53474300, survives reboot)
- [x] Factory reset ('R' command)
- [x] `data_size` fixed: uint16_t → uint32_t (overflow at 65 KB)
- [ ] CRC-16 per run (deferred — low risk)

#### 7c — Sensors & BLE Integration ✅ DONE (06-16 20:57)
- [x] BLE characteristics: state, battery, transfer status, calibration
- [x] Battery low detection (< 15% forces SLEEP)
- [x] Qi charging pin, beeper PWM
- [x] BHY2 meta-event hook via `bhy2_cal_hook.cpp` (captures SENSOR_STATUS events)

#### 7d — Completeness ✅ DONE (06-18)
- [x] **Sensor readiness**: quaternion magnitude check (0.8 < |q| < 1.2) — catches dead BHI260AP
- [x] **Start detector**: baseline seeded at ring-full, no drift (was causing 2s delay)
- [x] **Calibration accuracy**: BHY2 meta-event hook captures real 0-3 values. Sensor 31 (LACC) reports 3. Sensor 34 (RV) doesn't emit SENSOR_STATUS on this firmware — deferred.
- [ ] **BLE module refactoring** — NEXT STEP (extract sgc_service.cpp, file_transfer.cpp from main.cpp)

#### 7e — Hardware-Dependent (needs custom PCB or breakout)
- [ ] LDC1612 proximity arming — I2C1 driver, 1000 ms hold, INTB wake
- [ ] SK6812 LED strip — level shifter or battery-rail power
- [ ] UWB + RFID — v2 only (unpopulated footprint)

### Phase 8: BLE File Transfer + Phone App (pending)
- [ ] 8a: BLE file transfer protocol — Run List characteristic, chunked download, CRC32
- [ ] 8b: Phone BLE scan fix — runtime permission check (code added 06-18, untested)
- [ ] 8c: File download + decompression + CRC validation
- [ ] 8d: Impact detection + gate timestamp table
- [ ] 8e: Left/right arm cross-correlation
- [ ] 8f: Cloud upload (gate timestamps + baro, not raw 100 Hz)

### Phase 9: Enclosure + Field Testing (pending)
- [ ] 9a: PCB layout (KiCad)
- [ ] 9b: 3D-print enclosure (translucent polycarbonate)
- [ ] 9c: Battery selection (2× 300 mAh Li-Po parallel)
- [ ] 9d: On-slope field test

### Phase 10: Circular Flash Storage ✅ DONE (06-25 21:00) — later superseded for payloads
- [x] FlashManager class — read_head/write_head, auto-overwrite (F08)
- [x] Index sector at end of flash (sectors 510-511)
- [x] CRC32 per run with magic bytes (R05)
- [x] Power-loss index recovery via header scan
- [x] RunHeader aligned: +frame_count, data_size uint32 (Phase 7b fix)
- [x] Ring buffer drain: proper pop-2/push-1 interleaving (C5)
- [x] StartDetector: fixed thresholds + cumulative-drop-from-P₀ algorithm (C6/C7)
- [x] EndDetector: v4.0 elevation delta (replaced C8 flatline+IMU stillness)
- [x] StartDetector feed rate: 10 Hz via separate timer (C11)
- [x] State machine enum aligned to design (C10)
- [x] Quaternion mapping fix: x→w bug corrected (C9)
- [x] Test harness: start-detector test thresholds updated for new algorithm
- [x] Cordio: CORDIO_ZERO_COPY_HCI=1 (default) works correctly with current firmware. Warning harmless — ignore. Do NOT patch mbed_config.h (HardFault risk).
- [x] BHY2 heap fix: `BHY2.begin(NICLA_STANDALONE)` — skips bleHandler/eslovHandler/dfuManager, saves ~several KB heap
- [x] Init ordering: BLE.begin() before BHY2.begin() (BLE gets thread allocation first)

### Phase 11–12: LittleFS era + stream tests ✅ DONE (historical)
- [x] LittleFS v1 run files (v2 broken on nRF52832 Arduino-mbed)
- [x] Append-only close, stream mode simplification, hex dump / integrity path
- [x] S03 stream reliability (v4.02–v4.34+)
- ⚠️ **Superseded for run payloads by Phase 13 Opt A** — LFS code kept as `.disabled`

### Phase 13: Opt-A Raw Run Store ✅ DONE (2026-08-06/07) — CURRENT
- [x] S04 BHY2 rate test (`system_tests/test_bhy2_rate.py`)
- [x] Root cause: LittleFS hot path ~42–47 fps; design budget assumed raw program
- [x] BHY2 ODR fixed to 100 Hz (v4.59); no large RAM ring (Cordio OOM v4.60/61)
- [x] Spike raw writer 99.4 fps (v4.62)
- [x] Production `RawRunStore`: 8×~249 KB slots, index @ 0x1FD000 (v4.63)
- [x] BLE file transfer + run_count wired to `g_runs`
- [x] Full-slot `prepare_next_run()` at POST_RUN cooldown + boot, **not ARM** (v4.64)
- [x] ADR-003 amended; `FW_VERSION` 4.64
- [ ] JP hardware confirm S04 on v4.64 (in progress)
- [ ] S03 stream smoke on Opt-A path
- [ ] Multi-run without `-R` + 9th-slot overwrite

---

## Version Tags

| Tag | Date | Description |
|-----|------|-------------|
| **v4.64** | **08-07** | **Full-slot erase in prepare_next_run (POST_RUN/boot); BLE RawRunStore include fix** |
| v4.63 | 08-06 | Opt-A production RawRunStore multi-slot; LFS unlinked; S04 99.4 fps proven on spike |
| v4.62 | 08-06 | Opt-A spike raw pre-erased SPI run |
| v4.59–61 | 08-06 | BHY2 100 Hz ODR; RAM ring abort; force-l FlashRing bypass |
| v4.34 | 07-28 | Stream simplification: ARM-only stream, start-det auto-init |
| v4.07 | 07-23 | Stream reliability; arrival_ms on FlashRing |
| v3.0 | 06-30 | End detector v4 elevation delta |
| v2.4 | 06-27 | Start detector drop-only |
| v2.3 | 06-25 | FlashManager, CRC32, drain interleaving |
| v0.8.1 | 06-18 | Quaternion magnitude sensor check |
