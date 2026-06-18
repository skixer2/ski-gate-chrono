# SGC Nicla Sense ME — Phased Build Plan
## 2026-06-16 · updated 2026-06-17

Goal: incrementally validate each hardware subsystem in isolation before combining.
Each phase: write → build → upload → power-cycle → test → confirm → NEXT.

---

### Phase 1: BLE Advertising ✅ DONE (07:05)
**Test:** "SGC" appears on BLE scanner. No Cordio 0x80FF0144 assert.

### Phase 2: BLE + BHY2 coexistence ✅ DONE (07:34)
**Test:** BLE advertises, BHY2 sensors stream. RAM 57%, Flash 58%.

### Phase 3: BLE + BHY2 + LED ✅ DONE (08:46)
**Test:** Onboard RGB breathing blue. Fix: `nicla::begin()` inits Wire1.

### Phase 4: BLE + BHY2 + LED + Battery ✅ DONE (09:04)
**Test:** Battery % on BLE characteristic. PMIC via Nicla_System.

### Phase 5: State Machine ✅ DONE (09:50)
**Test:** Serial commands (i/a/l/p/s/?). LED patterns follow state.
Timeouts: ARMED→IDLE 30s, POST_RUN→IDLE 10s, IDLE→SLEEP 2min.
Bugs fixed: `BLE.stopAdvertise()`, cooldown spam, POST_RUN auto-transition.

### Phase 6: Flash Storage ✅ DONE (12:04)
**Test:** MX25R1635F via `mbed::BlockDevice::get_default_instance()`.
Erase, write 256B, read-back, verify. Flash on SPI1 (p4/p5/p3, CS=p26).

### Phase 7: Full Pipeline 🔄 CURRENT

#### 7a — Core Pipeline ✅ DONE (20:42)
- [x] Flash-based ring buffer: 4 blocks, 1000-slot circular, 500-frame sliding window
- [x] RawFrame: 16B (quat 8B + lin_acc 6B + baro/4 2B). CompressedFrame: 22B
- [x] BitPacker: delta encoding (quaternion diffs, acc/baro pass-through)
- [x] StartDetector: dual-mode — baro speed (per-sample delta) or drop (min-tracking)
- [x] EndDetector: 10s ACC stillness
- [x] Run headers: 16B per run (format ver, arm side, timestamp, data size)
- [x] Multi-run: sequential stacking on flash, block-aligned

#### 7b — Persistence & Integrity ✅ DONE (20:57)
- [x] Run index sector (block 4, magic 0x53474300, survives reboot)
- [x] Factory reset ('R' command or proximity hold, erases index + data)
- [x] Run header includes calibration accuracy, baro temp
- [ ] CRC-16 per frame (deferred — low risk, flash is reliable)

#### 7c — Sensors & BLE Integration ✅ DONE (20:57)
- [x] BLE characteristics: state, battery, transfer status (`...ABCD`), calibration (`...ABCE`)
- [x] Battery low detection (< 15% forces SLEEP from LOGGING)
- [x] Qi charging pin configured (P0.10 = digital pin 0, INPUT_PULLUP)
- [x] Beeper PWM on ARMED (P0.09 = digital pin 1, PWM piezo)
- [x] Calibration accuracy read from BHY2 rotation.accuracy(), pushed to BLE

#### 7d — Power & Sleep (pending)
- [ ] Deep sleep entry on SLEEP state (low-power mode)
- [ ] Wake from sleep on proximity interrupt (LDC1612 — deferred, needs hardware)

#### 7e — Deferred (needs hardware)
- [ ] LDC1612 proximity arming (I2C1 driver, cross-arm detection)
- [ ] SK6812 LED strip (NeoPixel nRF52 compatibility)
- [ ] UWB + RFID (v2 only — unpopulated)

---
