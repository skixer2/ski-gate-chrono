# High-Frequency Ski Telemetry & Timing Device

**Status: Requirements v5.4 complete, System Design v1.8 complete, Phone Architecture v1.3 — 2026-06-06**

> This document is the original enriched plan. As of 2026-06-04, two additional documents capture the evolved design:
> - **`sgc_requirements.md`** (v5) — 61 functional + 9 performance + 12 hardware + 10 interface + 8 robustness requirements, with UHF RFID gate-tag detection, dual-tag NFC+UHF pole setup, full cloud DB schema, access control, and V-model traceability.
> - **`sgc_system_design.md`** (v1.4) — Device state machine (incl. SETUP mode), ring-buffer drain algorithm, adaptive bit-packing format, BLE GATT service, Flutter phone app architecture, cloud API endpoints, firmware module decomposition (incl. UHF RFID), gate event format, battery budget (with/without RFID), and risk register.

## 1. Project Overview
The goal is to design an ultra-compact, forearm-mounted telemetry device to accurately time ski training runs across Alpine disciplines (Slalom, Giant Slalom, Super-G, and Downhill). The system autonomously detects the exact moment of a race start and finish, logs high-frequency motion data, and stores multiple runs locally for offline post-run analysis (AI/Neural Networks/deterministic models) on a smartphone via Bluetooth Low Energy (BLE).

### Historical Baseline (SensiBLE 2.0 Failures)
An earlier prototype built on the SensiEDGE SensiBLE 2.0 (ARM Cortex-M0 BlueNRG-1 SoC) failed due to:
* **Real-Time Bottlenecks:** Real-time BLE transmission caused packet/timestamp drops during high-speed gate strikes.
* **False Positives:** Simple acceleration spike thresholding confused ski chatter, vibration, and edge changes with physical pole impacts.
* **Start/Loss Errors:** Relying on static arm gestures or inclination angles (-70°) caused frequent missed starts or false triggers when athletes adjusted goggles, planted poles, or stretched.
* **RAM Limits:** The 24 KB RAM bottleneck prevented caching long time-series data for backward-looking digital filtering.

---

## 2. Firmware Approach: FreeRTOS vs Arduino/mbedOS

Three viable options exist for the Nicla Sense ME firmware:

| | Arduino + mbedOS | FreeRTOS + nRF5 SDK | Zephyr |
|---|---|---|---|
| **Nicla support** | ✅ First-class board package | ❌ Manual BSP bring-up | ⚠️ Partial (community) |
| **Scheduler** | mbedOS threads (preemptive) | Full preemptive, priority-based, SW timers | Zephyr kernel (priority-based) |
| **BLE stack** | ArduinoBLE wrapper (suitable for simple GATT) | Nordic SoftDevice S132 (full control) | SoftDevice via Zephyr BLE subsystem |
| **Debugging** | Serial prints, basic | Segger RTT + Ozone (excellent) | Logging subsystem + debug probe |
| **RAM overhead** | ~12–16 KB (mbedOS + Arduino) | ~8–10 KB (FreeRTOS kernel only) | ~10–14 KB (Zephyr kernel) |
| **Learning curve** | Low | Medium | High (Kconfig, devicetree, west) |
| **100 Hz jitter** | `mbed::Ticker` — typically < 100 µs | HW timer ISR — < 10 µs | Comparable to FreeRTOS |
| **BHI260AP init** | Arduino `Nicla_System` handles firmware + Fuser2 boot automatically | Manual: load Bosch blob, configure virtual sensors via I²C | Manual: same as FreeRTOS |

### Why mbedOS Gets the BHI260AP Running Faster

The Nicla ships with BHI260AP firmware pre-loaded in 2 MB QQSPI Flash. `Nicla_System::begin()` handles:
1. Reset BHI260AP
2. Wait for internal M0 boot + firmware load from QQSPI
3. Verify "Fuser2" (Bosch virtual sensor framework) is alive
4. Configure virtual sensors via I²C host interface

On FreeRTOS/nRF5 SDK you replicate this manually — documented but hundreds of lines of init code.

### Recommendation

**Start with Arduino + mbedOS.** The 100 Hz `mbed::Ticker` preemptive priority will suffice. ArduinoBLE handles post-run GATT.

**Migrate to FreeRTOS + nRF5 SDK only if:**
- `mbed::Ticker` jitter exceeds ~500 µs (bench-check first)
- ArduinoBLE throughput too low for practical file transfer
- RAM exhausted (64 KB total, ~40 KB needed for app)

**Avoid `Arduino_FreeRTOS`** — stacking two schedulers increases RAM, creates unpredictable latencies, and is harder to debug.

---

## 3. Updated System Architecture

[9-Axis IMU + Barometer] ──> [5s RAM Ring Buffer] ──> [Adaptive Bit-Packing] ──> [2MB Local Flash]│[Mobile Offline Analysis] <────────────────── [BLE Sync Post-Run] <───────────────────────┘


### Decision: Fused Sensor Data from BHI260AP

The BHI260AP is a **smart sensor** with its own Cortex-M0 dedicated to sensor fusion. Reading raw 9-axis data from the nRF52832 and fusing in software wastes the sensor's primary capability.

**The BHI260AP runs Bosch 9-axis fusion firmware and outputs pre-fused data:**

| Output Virtual Sensor | Data | Fixed-Point Encoding | Bytes |
|---|---|---|---|
| **Rotation Vector** (quaternion) | Absolute 3D orientation — drift-corrected by magnetometer every frame | 4 × int16 (1/32768 resolution) | 8 |
| **Linear Acceleration** | Accel with gravity removed — clean impact signal, zero baseline regardless of arm pose | 3 × int16 (1/1000 g resolution) | 6 |
| **Barometer** (BMP390, separate I²C) | Pressure + temperature | 2 × int16 | 4 |
| **Timestamp** | µs since run start | uint32 | 4 |
| **Total** | | | **22 bytes/sample** |

> ⚠️ **2026-06-06:** This 22 B/sample figure was from the original design. The SD/HLR evolved to a more compact format (RawFrame struct = 16 bytes, ~20 B with encoding overhead). Compressed average: ~9.25 B/sample (~108 KB per 2-min run, not 258 KB). See `sgc_system_design.md` §3 and `sgc_architecture_devices.md` §2-3 for current bit-packing.

### Why Fused Beats Raw for This Application

**1. Cross-correlation on quaternion orientation** — Raw gyro drifts; left and right arms accumulate different bias errors over a 2-min run, distorting end-of-run correlation. Fused quaternions are continuously corrected by the magnetometer — both arms share the same absolute reference (magnetic north + gravity).

**2. Gate impact on linear acceleration** — The SensiBLE 2.0 pitfall: raw accel sees gravity (~9.81 m/s²) rotating through the sensor frame during a carve, mimicking an impact spike. Linear acceleration has gravity *subtracted* — zero baseline independent of arm angle. A gate strike is an unambiguous spike:

```
Raw accel during carve:   [0.2, -9.6, 1.3]   ← gate strike or tilted arm?
Linear accel same instant: [0.1, -0.3, 0.1]   ← clearly no strike
Linear accel at pole hit:  [0.2, -2.8, 0.1]   ← unambiguous impact
```

**3. Data rate parity** — 22 bytes/sample fused ≈ the original 18-byte raw plan, with more useful data. After adaptive bit-packing: ~9.25 bytes/sample (see SD §3, AM §3 for current format).

### High-Frequency Data Target
* **Sampling Rate:** 100 Hz continuous acquisition.
* **Sensor Profile (Revised):** BHI260AP in 9-axis fusion mode → Rotation Vector + Linear Acceleration + BMP390 Barometer.
* **Data Footprint:** 22 bytes/sample raw = 2.15 KB/sec (~258 KB/2-min run). With adaptive bit-packing: ~9.25 bytes/sample average → ~1.08 KB/sec (~108 KB/2-min run). 2 MB Flash holds ~18 runs per arm.

---

## 4. Core Hardware Specifications

The selected platform is the **Arduino Nicla Sense ME (ABX00050)**, which fulfills all requirements natively in an ultra-miniature form factor.

* **Processor:** 64 MHz ARM® Cortex-M4 (Nordic nRF52832) supporting FreeRTOS and Zephyr OS.
* **Dimensions:** 22.86 mm × 22.86 mm.
* **Primary Sensor (IMU):** Bosch BHI260AP – Self-learning AI smart sensor with an integrated 6-axis IMU and hardware Machine Learning Core (MLC).
* **Environmental Sensors:** Bosch BMP390 (High-performance barometer with 25cm resolution) + BME688.
* **Magnetometer:** Bosch BMM150 (3-Axis).
* **Onboard Storage:** 2 MB SPI Flash dedicated for user data logging + 2 MB QQSPI Flash for BHI260AP firmware.
* **Connectivity:** Bluetooth 5.0 Low Energy.
* **Power & Battery:** Integrated 3.7V Li-Po linear charger managed directly on-board via a 3-pin JST-ACH micro-connector. See §9 for procured battery details.

---

## 5. BHI260AP 100 Hz Fusion Configuration

The BHI260AP requires explicit configuration to output fused data at 100 Hz. This section documents the key parameters for the Bosch virtual sensor framework (Fuser2).

### A. Virtual Sensor Selection

Two virtual sensors must be enabled via the I²C host interface:

| Virtual Sensor ID | Name | Rate | Output |
|---|---|---|---|
| **VS_WAKEUP_ROTATION_VECTOR** (0x11) | 9-axis rotation vector | 100 Hz | Quaternion (w,x,y,z), accuracy |
| **VS_WAKEUP_LINEAR_ACCELERATION** (0x1A) | Gravity-removed linear accel | 100 Hz | x, y, z in m/s² |

Both wakeup-type virtual sensors run on the BHI260AP's internal M0 at configurable ODR.

### B. Initialization Sequence

```
1. GPIO reset BHI260AP (Nicla_System::begin handles this for Arduino)
2. Wait for boot + Fuser2 ready (poll status register, ~50 ms)
3. Upload configuration to Fuser2:
   a. Set physical sensor ODRs: Accel 200 Hz, Gyro 400 Hz, Mag 50 Hz
   b. Set fusion output rate: 100 Hz for VS_ROTATION_VECTOR, VS_LINEAR_ACCEL
   c. Set FIFO watermark: 10 samples (flush every 100 ms)
4. Enable virtual sensors
5. Confirm watermark interrupt fires at expected cadence
```

### C. Key Fusion Parameters

| Parameter | Value | Rationale |
|---|---|---|
| Accel ODR | 200 Hz | Twice fusion rate — Nyquist for 100 Hz orientation output |
| Gyro ODR | 400 Hz | 4× fusion rate — gyro dominates orientation integration, needs oversampling |
| Mag ODR | 50 Hz | Mag is slow by nature; 50 Hz is sufficient for drift correction at 100 Hz quaternion output |
| Fusion ODR | 100 Hz | Target logging rate |
| FIFO watermark | 10 samples | Flush every 100 ms — balances latency vs. power (fewer I²C transactions) |
| FIFO size | 256 bytes | Default, sufficient for quat (8B) + lin_accel (6B) × 10 = 140B + headers |

### D. Data Format from FIFO

Each FIFO read returns a header byte followed by timestamped sensor data. For our two virtual sensors, each 100 ms flush reads ~10 frames × (8 + 6) = 140 bytes of payload plus BHI timestamps. The nRF52832 reads the FIFO via I²C burst (max 400 kHz Fast-mode) and copies into the RAM ring buffer for delta encoding.

### E. Magnetometer Calibration

Fusion accuracy depends on proper magnetometer calibration. The BMM150 must be calibrated once per device after final assembly:

1. Enable BHI260AP's built-in calibration (it tracks hard/soft iron over time)
2. Initial calibration: rotate device in figure-8 for ~10 seconds on first power-up
3. Calibration status reported in the quaternion accuracy field (0=unreliable, 3=high accuracy)
4. Do not log data until accuracy ≥ 2
5. Store calibration matrix in SPI Flash for warm-start on subsequent boots

The inductive trigger's non-magnetic design (§6) is essential here — any permanent magnet in the enclosure would saturate the BMM150 and break fusion permanently.

---

## 6. Cold-Weather Hardware Additions

### The Arm Trigger: Inductive Proximity Sensor
To survive sub-zero temperatures, ice, and water without mechanical failure or compromised gaskets, the system utilizes an **inductive switch** (e.g., TI LDC1612) inside a fully sealed, seamless enclosure.
* **The Problem:** Ski poles and gear are frequently made of aluminum, which exhibits low magnetic permeability, dropping inductive sensor range by 50% to 60%.
* **The Mechanical Solution:** A flexible, sealed external button pad is mounted on the *outside* of the plastic forearm protection armor. A tiny steel washer or iron foil disc is glued to the inside of this membrane. When pressed by a heavy winter glove, the iron moves 1–2mm closer to the internal PCB induction coil, bypassing the aluminum reduction factor entirely.

### Power & Battery Selection
* **Active System Draw:** ~15 mA to 20 mA during 100 Hz logging.
* **Freezing Derating:** A 20% to 30% temporary capacity drop is factored in for sub-zero alpine use.
* **Battery Spec (Procured):**
  * **Cell:** Renata ICP622540PMT (622540 form-factor, 3.7V, 600 mAh Li-Po). Includes integrated NTC thermistor on a 3-wire lead (Red=BAT+, Black=BAT−/NTC GND, White/Yellow=NTC sense). 2.4× capacity vs. the previously-spec'd Adafruit 1578.
  * **Connector:** JST SR series A03SR03SR30K102A — 3-position, 1.00 mm pitch, pre-assembled 102 mm socket-to-socket cable. Correct for the Nicla's battery header.
  * **Pin mapping to Nicla:** Black → GND (pin 1), NTC sense → NTC (pin 2), Red → VBAT (pin 3). No external 10 kΩ bypass needed — the Renata's built-in NTC is compatible with the BQ25100 charger IC.
* **Physical Assembly:** Stacks directly beneath the Nicla Sense ME inside a 3D-printed enclosure. Renata cell is 6.2×25×40 mm (~40 mm length vs. 30 mm for the 502030 form factor) — enclosure will need slight elongation. Total module thickness under 14 mm, weight under 20 g.

---

## 7. Hardware Interface: Solid-State Inductive Cross-Arm Trigger
To preserve the absolute purity of the 9-axis sensor fusion data and prevent magnetometer saturation, the system utilizes a magnet-free proximity handshake.


### A. System Configuration & Wireless Remote Control
*   **The Sensor:** An eddy-current inductive sensing chip (e.g., TI LDC series) connected to a 25mm copper trace coil etched directly onto the internal PCB.
*   **The Target:** A completely passive, non-magnetic copper foil disk or conductive textile patch integrated directly into the opposite arm's securing strap.
*   **The Validation:** The system detects the passive metal target through the solid outer polycarbonate shell. A continuous 2000ms proximity hold is required to arm the 5-second RAM ring buffer, eliminating false triggers from fast-moving arm sweeps.

### B. Barometric Filter & Descent Trigger
Because track orientation varies on every mountain, static magnetometer headings are discarded. Instead, the device uses the mountain's vertical elevation profile.

1. **Idle State (Lift/Warm-up):** The barometer senses rising altitude (lifts) or static flat altitude. The 100 Hz logging is kept completely offline.
2. **Armed State:** Receiving the wireless button beacon prompts both arms to initiate an internal rolling **5-second RAM Ring Buffer** (consuming only ~10 KB of the nRF52832's 64 KB RAM).
3. **Start Trigger:** The athlete drops into the course. The barometer registers an immediate, sharp pressure surge corresponding to a rapid descent drop (>1.5 m/s).
4. **Logging Engagement:** The device instantly flushes the 5 seconds of pre-start buffer data from RAM into the 2 MB permanent SPI Flash, then records the live 100 Hz run.
5. **Auto-End Trigger:** In the finish area, the barometer flatlines (vertical velocity drops to 0 m/s) and the IMU registers a braking deceleration followed by stillness. If this state holds for 10 continuous seconds, the file system closes and enters deep sleep.

### C. Adaptive Bit-Packing Compression Strategy (Fused Data)

Data is stored using delta encoding between consecutive frames. The compressor selects the packet type per-frame based on the maximum delta magnitude across all 7 axes (4 quaternion components + 3 linear acceleration). Timestamp and barometer are always stored uncompressed as anchors.

| Packet Type | Condition | Quaternion (4 axes) | Lin. Accel (3 axes) | Anchor (ts + baro) | Total |
|---|---|---|---|---|---|
| **Type 1** (coasting) | All deltas fit in ±7 | 4 × 4-bit nibbles = 2B | 3 × 4-bit = 1.5B | 8B | **11.5B** |
| **Type 2** (turns/chatter) | All deltas fit in ±127 | 4 × int8 = 4B | 3 × int8 = 3B | 8B | **15B** |
| **Type 3** (impacts/anchor) | Any delta overflows int8, or every 100th frame | 4 × int16 = 8B | 3 × int16 = 6B | 8B | **22B** |

A 1-byte header tags the packet type and includes a 4-bit sequence counter for corruption detection.

> ⚠️ **2026-06-06:** The 13.5 B/sample and 162 KB estimates below are from the original design. Current numbers per SD/HLR: ~9.25 B/sample average, ~108 KB per 2-min run. The comparison logic is still valid — the 2 MB Flash comfortably holds >15 runs per arm. See `sgc_system_design.md` §3 and `sgc_architecture_devices.md` §2-3 for the current adaptive bit-packing format.

**Expected Efficiency:** In alpine skiing, ~60% of frames are Type 1 (coasting between gates), ~35% Type 2 (turning), ~5% Type 3 (impact or periodic anchor). Average: **~13.5 bytes/sample** → a 2-minute run is ~162 KB compressed. The 2 MB Flash holds **12 complete runs** per arm (down from 23–24 with raw, but each run contains strictly higher-quality, analysis-ready fused data).

**Flash Management:**
- Runs stored as individual files with a 16-byte header (RTC timestamp, arm side, compressed/uncompressed size, CRC32)
- Circular buffer: oldest runs auto-deleted when Flash is full
- BLE advertises "Run count" and "Flash % used" for the phone to decide sync urgency

---

## 8. Offline Post-Run Synchronization

When the athlete completes their training session and returns to their smartphone app, the data alignment happens algorithmically rather than over live wireless links.

### A. Run Association
The mobile application downloads the raw data files sequentially via BLE from each arm. Files with absolute Real-Time Clock timestamps starting within a window of a few seconds are grouped together as the same run.

### B. Quaternion-Based Shape-Matching (Revised)

Because alpine racers start with poles already planted, there is no violent initial linear acceleration shock. The start is a smooth forward body hinge over the pole grips. The BHI260AP fusion output captures this as a precise 3D rotation sequence.

* **The Anchor Feature:** The start generates a highly symmetrical rotation in quaternion space — the arm rotates forward over the pole grip, producing a distinct trajectory in the quaternion's imaginary components (qx, qy, qz) followed by a reciprocal motion as poles are extracted and the arm enters a tuck. Because quaternions are drift-corrected by the magnetometer, this rotation signal is **absolute** — both arms share the same magnetic reference.
* **The Algorithm (Quaternion Dot-Product Correlation):** The phone app isolates the first 3–5 seconds of each arm's quaternion stream (retrieved from the 5-second pre-trigger buffer). For each candidate time offset τ, it computes:

  ```
  C(τ) = Σ q_left(t) · q_right(t + τ)
  ```

  where `q_a · q_b` is the quaternion dot product — a measure of rotational similarity (+1.0 = identical orientation, −1.0 = opposite). This is more robust than raw gyro cross-correlation because it operates on drift-free absolute orientation rather than angular velocity.
* **Sub-Sample Refinement:** The integer lag with the highest correlation peak is interpolated parabolically to achieve sub-10ms alignment precision.
* **Timeline Zeroing:** T = 0.00 s is declared at the alignment point, synchronizing both arms without any reliance on BLE latency or RTC drift — the quaternion convergence is the clock.

---

## 9. Procured Hardware for Prototype

All components purchased as of 2026-06-02:

| Component | Part | Source | Status |
|---|---|---|---|
| **Main board** | Arduino Nicla Sense ME (ABX00050) | [DigiKey](https://www.digikey.ch/en/products/detail/arduino/ABX00050/15632328) | ✅ On order |
| **Battery** | Renata ICP622540PMT — 3.7V, 600 mAh Li-Po, 3-wire with NTC | [Distrelec](https://www.distrelec.ch/en/rechargeable-battery-pack-icp-li-po-7v-600mah-wire-lead-renata-icp622540pmt/p/30158724) | ✅ On order |
| **Battery cable** | JST A03SR03SR30K102A — 1.00 mm pitch, 102 mm, socket-to-socket | [DigiKey](https://www.digikey.ch/en/products/detail/jst-sales-america-inc/A03SR03SR30K102A/9922175) | ✅ On order |
| **Inductive test board** | TI LDC1612EVM — evaluation module for eddy-current sensing | [RS Online](https://ch.rs-online.com/web/p/entwicklungstools-sensorik/1887096) | ✅ On order |

### Wiring Plan

```
Renata Battery                    JST Cable                   Nicla Sense ME
  (bare leads)             (A03SR03SR30K102A)                (3-pin JST header)

  Black (BAT−/NTC GND) ──── solder ──── socket ──── plug ──── Pin 1 (GND)
  White/Yellow (NTC)    ──── solder ──── socket ──── plug ──── Pin 2 (NTC)
  Red (BAT+)            ──── solder ──── socket ──── plug ──── Pin 3 (VBAT)
```

No external 10 kΩ NTC bypass resistor needed — the Renata's integrated NTC thermistor is standard 10 kΩ at 25°C, compatible with the Nicla's BQ25100 charger IC.

### First Bench Test Plan

1. Solder Renata leads to JST cable, insulate with heat-shrink
2. Connect to Nicla, verify charger LED lights (charging)
3. Flash Arduino blink test, confirm Nicla boots on battery power
4. Run `Nicla_System::begin()` + BHI260AP init, verify Fuser2 ready via I²C
5. Read quaternion + linear acceleration FIFO at 100 Hz, stream to serial
6. Verify fusion accuracy ≥ 2 after figure-8 calibration
7. Wire LDC1612EVM to Nicla I²C, test inductive proximity through enclosure material
8. Implement ring buffer + adaptive bit-packing, log to SPI Flash

---

## 10. Evolution Summary — Requirements v4 + System Design v1.1 (2026-06-03)

Since the original context was written, both the requirements and a full system_design have been completed. Key additions and design decisions:

### New Hardware Requirements
- **Qi wireless charging** (H10) — no exposed contacts, fully sealed IP67 enclosure. Qi coil placed opposite PCB side from BMM150 to avoid magnetometer interference.
- **Surface transducer beeper** (F14, I08) — PWM-driven piezo element bonded to inner enclosure wall. No sound port needed (IP67 compatible), audible through helmet.
- **200g shock** rating (H09) — typical slalom pole strike is 100-200g at the grip; device must survive without damage or decalibration.
- **3h / 10-run reference session** (H02) — FIS training day model.

### New Device Features
- **BLE bonding** (F39) — LE Secure Connections, Just Works pairing. Bond on first connection, encrypt all subsequent sessions.
- **OTA firmware update** (F40) — Nordic DFU service. Signed firmware images pushed from phone; corrupted image rejected.
- **Time sync** (F37) — Phone writes UTC unixtime on every BLE connect before any other operation. RTC survives System ON sleep; crystal drift up to ±20 ppm reset on each sync.
- **RGB LED** (F41) — Visual state indicator: off=sleep, blue slow blink=BLE advertising (uncalibrated), solid blue=BLE advertising (calibrated), white=SETUP mode, green=armed, red=logging, yellow=low battery/error. Light pipes through translucent polycarbonate.
- **Factory reset** (F42) — 20-second continuous inductive hold → LED flashes red 3× → clear bonding, reset name, erase Flash, restart.
- **RTC preserves time across sleep** (F13) — System ON WFE (not deep sleep), RAM + RTC retained. LDC1612 INTB → GPIO wake.

### Expanded Phone Features (F26–F51)
- **Gate time estimation** (F26) — Kinematics-driven pipeline replaces old MissedGateEstimator + GateClassifier: rotation-speed zero-finding (0.5 Hz LPF, 0.3 rad/s threshold) → local-frame coordinate transform per zero pair → Y half-plane L/R classification → Case A (geometric interpolation) / Case B (statistical A%). Runs on-device in < 0.5 ms.
- **Banana detection** (F28) — Two consecutive same-side gates detected and flagged.
- **Vertical speed** (F30) — Calculated from barometric pressure at 10 Hz (decimated from 100 Hz).
- **Altitude + speed graphs** (F31–F34) — With green/red gate markers. Side-by-side comparison mode for two runs.
- **Group visibility** (F35–F36) — Trainer-controlled: `full` / `athlete_only` / `denied`, with per-run override.
- **Gate numbering** (F44) — Sequential from start, displayed in gate table.
- **Run naming** (F45) — User-editable labels, default = date + run_number.
- **Run deletion** (F46) — Trainers and masters can delete from device, phone, and cloud. Athletes/parents/friends cannot.
- **Export** (F47) — CSV + JSON export for external tools.
- **Cloud run browsing** (F48) — Browse group runs from cloud for comparison, not just local.
- **Single-arm mode** (F49) — For SL: compare same-side arm data only. GS/SG/DH: gate side from arm side directly.
- **GDPR deletion** (F50) — Permanent, irreversible cloud data deletion with explicit warning.
- **Calibration display** (F51) — BHI260AP accuracy 0–3 shown with 🔴/🟢 indicator. Arming refused if accuracy < 2.
- **Offline cloud sync** (F43) — Queue uploads when offline, sync on connectivity restore.

### State Machine (System Design, §1)
Six states: **SLEEP** (System ON WFE, ~53 µA), **IDLE** (BLE advertising, calibration-aware: slow blue blink if accuracy<2, solid blue if ≥2), **SETUP** (UHF RFID at 1 Hz, LED white, tag IDs → BLE notify, ~16 mA), **ARMED** (ring buffer filling + beeper, LED green; **gated on accuracy ≥ 2** per F51/P05), **LOGGING** (push 1 live, pop 2 or 1 → Flash, + RFID per discipline, LED red, ~19–43 mA), **POST_RUN** (advertise updated run count, ~14 mA), **LOW_BATTERY** (any state → close file → SLEEP).
- **CHARGING** (H10): Qi power is a side condition — no separate state. Arming/logging proceed normally while charging; status notified via GATT.
- **FACTORY RESET** (F42): 20s continuous inductive hold from IDLE → LED flashes red 3× → clear bonding, reset name, erase Flash, restart.

### Ring-Buffer Drain (System Design, §2)
Single ring buffer (500 samples). Every 10ms cycle, always the same: push 1 live to top, try to pop 2 oldest from bottom → Flash. If only 1 available, pop that 1. No mode switch, no separate buffer, no steady state. Pre-start samples are at the bottom so they flush first — Flash naturally gets chronological order.

### Updated Bit-Packing Format
Header byte eliminated — packet type + sequence counter embedded in delta field's upper bits. Barometric pressure always uncompressed (anchor). Barometric temperature stored once in run file header (not per-frame — saves ~12 KB/run). Expected average: **9.25 bytes/frame** (vs. 20 raw) → 53.8% compression. 2-min run → ~108 KB. 2 MB Flash → ~19 runs.

### Cloud Database Schema
- **6 tables**: `ski_clubs`, `users`, `groups` (inter-club, no FK to clubs), `users2groups`, `users2athletes` (self-referencing many-to-many on users), `runs` (with `format_version` + `lock_version` for concurrency), `timestamps` (with `guessed` boolean), `barometric_data` (altitude + speed at 10 Hz).
- **4 user types**: athlete, trainer, parent, friend, **master** (self-responsible adult).
- **Access control**: visibility enforced server-side at `full` / `athlete_only` / `denied` levels.
- **Optimistic locking**: `lock_version` prevents conflicting simultaneous uploads.
- **Cloud upload**: timestamps + barometric_data only (not raw sensor data — stays local).

### Circular Flash Buffer (System Design, §7)
Two-index scheme: `read_head` (oldest run, overwritten when full) and `write_head` (next free position). Both wrap at 2 MB boundary. Indices stored in reserved Flash sector for power-loss survival. Automatic overwrite: if new run would overlap read_head, delete oldest → advance read_head.

### System Design Artifacts
- **Phone architecture**: Flutter app, 15+ modules (BLE, processing, UI, cloud, storage, setup).
- **Cloud API**: Bootstrap (redirect to active endpoint), JWT auth, CRUD for runs/timestamps/barometric_data/groups/members/courses/course_gates.
- **Firmware modules**: 18+ C++ modules (sensors, storage, BLE, state machine, detectors, RFID reader, gate events).
- **Battery budget**: All states profiled. 600 mAh → ~14 sessions at 20°C, ~11 sessions at −10°C. Shelf life ~6–8 months (self-discharge limited).
- **Qi charging**: 5W pad → BQ25100 charger. 0–100% in ~1.5 h.
- **Flash wear**: ~1,887 years at 1,000 runs/year — not a concern.

### UHF RFID Gate Detection (Requirements v5, System Design v1.4)
- **F52–F56**: Passive UHF RFID tags on gate poles (EPC Gen2, €0.20 each, no power). Active reader on device during LOGGING only.
- **F53**: Polling rate set by **discipline from phone** (F11): SL 5 Hz, GS 10 Hz, SG/DH 20 Hz. Single-tag round ≤ 15 ms. At 20 Hz DH, 2 m read zone catches a gate at 120 km/h with margin.
- **F55**: Gate events (tag ID + RSSI + timestamp) logged as separate records alongside sensor frames.
- **Hardware (H11, I10)**: Impinj E310 or ST ST25RU3993 reader IC, SPI to nRF52832. Ceramic chip antenna (3.2×1.6 mm), > 10 mm from BMM150. RFID powered only during LOGGING.
- **Power**: LOGGING + RFID = **43 mA** (19 baseline + 24 RFID at 15% duty cycle). 600 mAh battery → ~9 sessions at −10°C. H02 still met (1.4× margin).
- **Gate events**: timestamp (uint32 ms) + tag_id (uint32/64) + rssi (int8 dBm). 8–10 bytes per event, ~6 events/min → negligible Flash impact.
- **Risk**: Nicla Sense ME cannot host RFID — requires custom PCB. Prototype with external RFID module on breadboard.

### Pole Setup — Unified Tracing Mode (Requirements v5, System Design v1.4)
- **Dual tags**: Each pole carries both NFC (for phone tap) and UHF RFID (for device at speed). Same tag ID on both layers. Cost ~€0.30/pole.
- **F57**: SETUP mode — device RFID at 1 Hz, LED white, each detected tag ID streamed to phone via BLE notify.
- **F58**: Phone tracing mode workflow: (1) enter tracing mode → phone sets all connected devices to SETUP. (2) Wait for tag ID from either: phone NFC tap on pole, or BLE notify from device (forearm touch). (3) Record GPS + tag ID + gate number. (4) Duplicate detection: same tag within 5 s → merged.
- **F59**: Duplicate tag detection — if same pole is touched via both methods or twice, only one gate entry recorded.
- **F60**: Course gate positions persisted to cloud (`courses` + `course_gates` tables with tag_id, gate_number, lat, lon, altitude).
- **F61**: Post-run: gate events from run file correlated with stored gate positions. Enables spatial overlays on graphs — GPS track, gate-by-gate altitude profile, line-choice comparison with terrain context.

---

*Document updated 2026-06-04 — added evolution summary capturing Requirements v5 + System Design v1.4. Original context follows unchanged below this marker.*