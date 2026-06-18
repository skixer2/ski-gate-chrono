# SGC Nicla Sense ME — Phased Build Plan
## 2026-06-16 · updated 2026-06-18 18:13

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
- [x] StartDetector (barometric drop), EndDetector (10s stillness)
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

---

## Version Tags

| Tag | Date | Description |
|-----|------|-------------|
| v0.8.1 | 06-18 18:11 | Quaternion magnitude sensor check + clean cal display |
| v0.8.0 | 06-18 17:56 | Removed calibration arming gate; kept meta-event hook |
| v0.7.9 | 06-18 17:50 | Disabled BHY2 delegate mode (callback table active) |
| v0.7.6 | 06-18 16:28 | Start detector fix; cal gate deferred |
| v0.7.5 | 06-18 16:19 | Calibration scaling fix + start detector drift fix |
| v0.7.4 | 06-18 15:44 | Calibration gate (broken — removed in v0.7.6) |
| v0.7.3 | 06-18 15:25 | Phase 7c complete. Repo init at ski_gate_chrono/ |
