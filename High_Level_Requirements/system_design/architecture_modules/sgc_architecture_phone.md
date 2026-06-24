# SGC — Architecture Modules: Phone (v2.0)

*2026-06-09 — v2.0: Architecture pivot — phone does ALL gate detection post-run (pressure ΔP + IMU). Two operational tiers (Gold/Bronze). Course map management. See sgc_architecture_decisions.md for pivot rationale.*

*2026-06-06 — Coherence fixes: Vec3 model added, Ẑ sign corrected (use up vector, not gravity), position trajectory uses cloud course data (not raw GPS), F15 arm pairing method added, F30 tag added, chargingStream → H10 tag, GateTimestamp.side clarified, §5→§7 cross-reference.*

*2026-06-05 — Flutter/Dart phone application architecture. Complements sgc_architecture_devices.md (firmware) with phone-side implementation detail.*

---

## 1. Module Dependency Graph

```
                              main.dart
                                  │
                    ┌─────────────┼─────────────┐
                    │             │             │
              ┌─────▼──────┐ ┌───▼────┐ ┌──────▼──────┐
              │   ble/     │ │  ui/   │ │   setup/    │
              │ ble_mgr    │ │ screens│ │ course_setup │
              │ sgc_service│ └───┬────┘ └──────┬──────┘
              │ file_xfer  │     │             │
              └─────┬──────┘     │             │
                    │            │             │
              ┌─────▼────────────▼─────────────▼──────┐
              │           processing/                 │
              │ decompressor  impact_detector         │
              │ gate_time_estimator  banana_detector   │
              │ cross_correlator  barometric_speed     │
              │                                      │
              └──────────────────┬───────────────────┘
                                 │
                    ┌────────────┼────────────┐
                    │            │            │
              ┌─────▼─────┐ ┌───▼────┐ ┌─────▼──────┐
              │  models/  │ │ cloud/ │ │  storage/  │
              │ run       │ │ api    │ │ local_db   │
              │ gate_ts   │ │ sync   │ │ settings   │
              │ baro_pt   │ │ gdpr   │ └────────────┘
              │ dev_cfg   │ └────────┘
              │ user_prof │
              └───────────┘
```

**Ownership rules:**
- `ble/` owns the BLE connection lifecycle — no other module touches BLE directly
- `processing/` is pure Dart — no platform dependencies, fully testable with synthetic data
- `models/` are immutable value objects — no side effects, `==` by value
- `cloud/` depends only on `models/` and `storage/` — never on `ble/` or `ui/`
- `ui/` depends on everything below but never reaches around to `ble/` directly
- `storage/` is the persistence layer — SQLite for runs, SharedPreferences for settings
- **No circular dependencies.**

---

## 2. Data Models (`models/`)

### Run (`run.dart`)

A `Run` represents the raw data downloaded from a **single** SGC device (left arm OR right arm). Each device produces one run file. The phone downloads both, then merges them into a `CombinedRun`.

```dart
class Run {
  final int id;                    // local DB primary key
  final String? cloudRunId;        // set after cloud upload
  final int deviceRunId;           // run_id from device Flash (2-byte)
  final String armSide;            // 'left' | 'right' — which arm recorded this
  final String deviceId;           // BLE MAC or bond ID of the device that recorded this
  final DateTime startTime;        // UTC unixtime from RTC
  final int frameCount;
  final int compressedSizeBytes;
  final String? label;             // user-editable (F45)
  final bool isUploaded;
  final String? visibility;        // 'full' | 'athlete_only' | 'denied' (F35)
  
  // Decompressed data (populated after BLE download):
  List<SensorFrame> frames;        // 100 Hz, nullable until decompressed
  List<BarometricPoint> barometricData; // 10 Hz altitude + speed
  List<GateTimestamp> gateTimestamps;   // detected + guessed gates from this arm only
  
  int formatVersion;
}
```

### CombinedRun

The result of merging left and right arm runs after cross-correlation alignment (F15, F16):

```dart
class CombinedRun {
  final Run leftRun;
  final Run rightRun;
  final int timeOffsetMs;          // right_time - left_time (from cross-correlator)
  final List<MergedGate> mergedGates;  // interleaved L/R gate sequence
  final List<BarometricPoint> combinedBaroData; // averaged from both arms
}

class MergedGate {
  final int gateNumber;
  final GateSide? side;            // leftGate | rightGate | null only during pipeline — resolved to non-null by GateTimeEstimator (impact/RFID/LocalFrame Y half-plane)
  final String? detectingArm;      // 'left' | 'right' — which arm touched this gate (null if unknown)
  final bool isEstimated;
  final Duration timeFromStart;
  final double? impactForce;
  final String? rfidTagId;
  final bool isBanana;
}
```

### SensorFrame

```dart
class SensorFrame {
  final int msFromStart;           // time delta accumulated
  final double qW, qX, qY, qZ;    // quaternion (normalized)
  final double laX, laY, laZ;     // linear acceleration (m/s²)
  final double baroPressurePa;    // barometric pressure (Pa)
}
```

### ZeroCrossing (`zero_crossing.dart`)

A turn boundary detected from the rotation-speed trace.

```dart
class ZeroCrossing {
  final Duration timeFromStart;      // center of the below-threshold region
  final int frameIndex;               // index in decompressed frames
}
```

### GateTimestamp (`gate_timestamp.dart`)

```dart
class GateTimestamp {
  final int gateNumber;            // sequential 1..N (or 0 if unknown)
  final GateSide? side;            // leftGate | rightGate | null only during pipeline — resolved to non-null by GateTimeEstimator (impact/RFID/LocalFrame Y half-plane)
  final bool isEstimated;          // true = guessed (missed gate), false = detected
  final Duration timeFromStart;    // ms from run start
  final double? impactForce;       // linear accel magnitude (null if estimated)
  final String? rfidTagId;         // ⚠️ v2 only — UHF RFID tag ID from pole (null in v1)
}
```

### CourseGate (from course_gates cloud table, F60)

v1 uses **relative delta positions** — civilian GPS errors cancel over short gate-to-gate windows. v2 adds RFID tag ID when poles are surveyed with UHF tags.

```dart
class CourseGate {
  final String courseId;
  final int gateNumber;            // 0 = START, 1..N = gates
  final GateSide side;             // 'leftGate' | 'rightGate'
  final double? deltaP;            // ΔP from START pressure (Pa). START = 0, null if unknown
  final double? deltaLat;          // ΔGPS from previous gate (deg), null for START
  final double? deltaLon;          // ΔGPS from previous gate (deg), null for START
  final double? deltaAltitudeM;    // Δ altitude from START (m). START = 0, null if unknown
  final String? rfidTagId;         // ⚠️ v2 only — EPC from pole tag (null in v1)
}
```

### Course (course map container)

Holds the full course definition including the absolute START pressure used to compute ΔP deltas during detection.

```dart
class Course {
  final String id;
  final String name;
  final int createdAtUnix;
  final double pStart;             // absolute pressure at START (Pa) — reference for all ΔP_n
  final List<CourseGate> gates;

  const Course({
    required this.id,
    required this.name,
    required this.createdAtUnix,
    required this.pStart,
    required this.gates,
  });
}
```

### BarometricPoint (`barometric_point.dart`)

```dart
class BarometricPoint {
  final Duration timeFromStart;    // ms from run start (10 Hz cadence)
  final double altitudeM;          // barometric altitude (m)
  final double verticalSpeedMs;    // vertical speed (m/s, positive = descending)
}
```

### Vec3 (`vec3.dart`)

3D world-frame vector used by GateTimeEstimator for position trajectory and local-frame transforms. Immutable value type with arithmetic and geometric operations.

```dart
class Vec3 {
  final double x, y, z;
  
  const Vec3(this.x, this.y, this.z);
  
  Vec3 operator +(Vec3 other) => Vec3(x + other.x, y + other.y, z + other.z);
  Vec3 operator -(Vec3 other) => Vec3(x - other.x, y - other.y, z - other.z);
  Vec3 operator *(double s)     => Vec3(x * s, y * s, z * s);
  
  double dot(Vec3 other)    => x * other.x + y * other.y + z * other.z;
  Vec3 cross(Vec3 other)    => Vec3(
    y * other.z - z * other.y,
    z * other.x - x * other.z,
    x * other.y - y * other.x,
  );
  double get length         => sqrt(x*x + y*y + z*z);
  Vec3 normalized()         => this * (1.0 / length);
}
```

### DeviceConfig (`device_config.dart`)

```dart
class DeviceConfig {
  final String deviceId;           // BLE MAC or bond ID
  final String deviceName;
  final String armSide;            // 'left' | 'right'
  final Discipline discipline;     // sl, gs, sg, dh
  final int mountType;             // 0=arm, 1=pole (reserved)
  final int calibrationAccuracy;   // BHI260AP accuracy 0-3
}

enum Discipline { sl, gs, sg, dh }
```

### UserProfile (`user_profile.dart`)

```dart
class UserProfile {
  final String athleteName;
  final String clubName;
  final String groupName;
  final int? age;
  final String? category;
  final bool pushToCloud;
  final double impactMultiplier;   // 1.5 (Kid) to 3.0 (Adult), controls impact detection sensitivity
}
```

---

## 3. BLE Client (`ble/`)

### BLE Manager (`ble_manager.dart`)

```dart
class BLEManager {
  // State
  Stream<BLEState> get stateStream;          // scanning | connecting | connected | disconnected
  
  // Scan
  Future<List<ScanResult>> scan();           // returns SGC devices by service UUID
  Future<void> stopScan();
  
  // Connection lifecycle
  Future<void> connect(String deviceId);      // auto-bond on first, encrypt after
  Future<void> disconnect();
  bool get isConnected;
  
  // Time sync — performed immediately after every connect (F37)
  Future<void> syncTime();                   // write unixtime to ...ABC0
  
  // MTU negotiation (F38)
  int get negotiatedMtu;                      // target ≥ 247
  bool get is2MPhy;                          // LE 2M PHY preferred
}
```

**Connection handshake sequence:**

```
1. Scan for SGC Service UUID (53470000-0000-1000-8000-00805F9B34FB)
2. Connect → BLE stack auto-bonds if first time (F39)
3. Request MTU ≥ 247 bytes
4. Write UTC unixtime to ...ABC0 (F37: before any other GATT operation)
5. Read device config (...ABC1–...ABC3, ...ABD0)
6. If calibration accuracy < 2: show calibration warning (F51)
7. Ready for operations
```

### SGC Service (`sgc_service.dart`)

Wraps all GATT characteristic read/write/notify. All async, all throw on BLE error.

```dart
class SGCService {
  final BLEManager ble;
  
  // Device info
  Future<DeviceConfig> readConfig();
  Future<void> writeName(String name);
  Future<void> writeArmSide(String side);
  Future<void> writeDiscipline(Discipline d);
  
  // Run management
  Future<int> getRunCount();
  Future<int> getFlashUsedPercent();
  Future<int> getOldestRunAge();
  Future<List<RunMeta>> getRunList();    // parses JSON from ...ABC9
  
  // Calibration
  Stream<int> get calibrationStream;     // notifies on accuracy change
  
  // File transfer entry point
  Future<Uint8List> downloadRun(int runId);  // returns decompressed raw bytes
  
  // Arm pairing (F15)
  // Groups left/right runs by RTC timestamp proximity (±3 s).
  // Called after both arm runs are downloaded.
  Future<PairingResult> pairArms(List<Run> downloadedRuns);
  
  // Charging status
  Stream<bool> get chargingStream;       // notifies on Qi state change (H10)
}
```

### File Transfer (`file_transfer.dart`)

```dart
class FileTransfer {
  final SGCService service;
  
  Future<TransferResult> download(int runId) async {
    // 1. Write runId to ...ABCA → triggers device transfer
    // 2. Listen for chunks on ...ABCB (244B each)
    // 3. Assemble complete file in memory
    // 4. Read CRC32 from ...ABCC
    // 5. Verify CRC32 matches
    // 6. If mismatch → re-request (R05)
    // 7. Return decompressed bytes
    
    TransferStatus status;       // idle | transferring | complete | error
    int chunksReceived;
    int totalChunks;             // estimated from compressed size
    Uint8List buffer;
  }
}

class TransferResult {
  final Uint8List compressedData;
  final int crc32;
  final int runId;
}
```

**Chunk reassembly:**

```dart
// Callback registered on ...ABCB notification
void onChunkReceived(Uint8List chunk) {
  buffer.setRange(offset, offset + chunk.length, chunk);
  offset += chunk.length;
  
  // Estimate progress
  // totalChunks = ceil(compressedSize / 244)
  progress = chunksReceived / totalChunks;
}
```

**Error recovery (R06):** If BLE disconnects mid-transfer, the partial buffer is discarded and the transfer restarts from chunk 0 on reconnect. SPI Flash reads are idempotent — no data corruption from retry.

---

## 4. Decompressor (`decompressor.dart`)

### Reverse Bit-Packing

```dart
class Decompressor {
  int formatVersion;
  
  List<SensorFrame> decompress(Uint8List compressed, RunHeader header) {
    // 1. Read 16-byte header: format_version, arm_side, timestamp, baro_temp, data_size, frame_count, reserved
    // 2. Initialize prev_frame from zeros (first delta accumulates from zero)
    // 3. For each frame in the compressed stream:
    //    a. Read time_delta (uint16 LE): bits 15-14 = type, 13-10 = seq, 9-0 = delta_ms
    //    b. Read baro_pressure (uint16 LE, Pa/4)
    //    c. Switch on type:
    //       Type 1 (00): read 4-byte payload → extract 7×4-bit signed deltas
    //       Type 2 (01): read 7-byte payload → 7×int8 deltas
    //       Type 3 (10): read 14-byte payload → 7×int16 absolute values
    //    d. Apply deltas to prev_frame or use absolute values
    //    e. Accumulate time_ms from delta
    //    f. Append SensorFrame to output list
    // 4. Return list of SensorFrame
  }
}
```

### Type-Specific Decoding

```dart
SensorFrame decodeType1(ByteData data, SensorFrame prev, double baroPa, int deltaMs) {
  // Payload: 4 bytes with 7×4-bit deltas
  int b0 = data.getUint8(0); int b1 = data.getUint8(1);
  int b2 = data.getUint8(2); int b3 = data.getUint8(3);
  
  int dqw  = signExtend4((b0 >> 4) & 0x0F);
  int dqx  = signExtend4(b0 & 0x0F);
  int dqy  = signExtend4((b1 >> 4) & 0x0F);
  int dqz  = signExtend4(b1 & 0x0F);
  int dlax = signExtend4((b2 >> 4) & 0x0F);
  int dlay = signExtend4(b2 & 0x0F);
  int dlaz = signExtend4((b3 >> 4) & 0x0F);
  // b3[3:0] is padding — unused
  
  return applyDeltas(prev, dqw, dqx, dqy, dqz, dlax, dlay, dlaz, baroPa, deltaMs);
}

int signExtend4(int val) => (val & 0x08) != 0 ? val | 0xFFFFFFF0 : val;
```

### CRC Validation

```dart
bool validateCRC(Uint8List compressedData, int headerCrc32) {
  // CRC32 is stored at end of file (AM §5), preceded by 0xC3 0x32 markers
  // Scan backward from end:
  int len = compressedData.length;
  if (compressedData[len - 6] != 0xC3 || compressedData[len - 5] != 0x32) {
    return false; // marker not found
  }
  int storedCrc = compressedData.getUint32(len - 4, Endian.little);
  int computedCrc = Crc32.compute(compressedData.sublist(0, len - 6));
  return storedCrc == computedCrc;
}
```

---

## 5. Gate Classification — Left/Right from Local-Frame Y Half-Plane

Gate side is determined by the spatial trajectory between turn boundaries (zeros), not by arm-side heuristics. This is discipline-agnostic and works equally well for SL, GS, SG, and DH.

**This section is a conceptual summary.** The full implementation lives in §7 (Gate Time Estimator), which handles L/R classification as step 7.6 of the pipeline.

### Principle

For each zero-to-zero segment (one turn), the skier's 3D positions are transformed into a local frame:

1. **X̂** = normalized(P_{i+1} − P_i) — forward direction between zeros
2. **Ẑ** = the up direction (opposite gravity), made perpendicular to X̂ by subtracting its X̂-component, then normalized:
   ```
   g_perp = -ĝ - (-ĝ · X̂) · X̂
   Ẑ = g_perp / |g_perp|
   ```
   This keeps Ẑ in the X̂-gravity plane while ensuring X̂ ⊥ Ẑ.
   *Why this is needed:* X̂ is not horizontal — it tilts downhill. Ẑ must be
   orthogonal to X̂ so the local frame is a proper Cartesian basis (dot products
   between axes = 0). Without this step, Ŷ would be skewed and the left/right
   classification unreliable.
3. **Ŷ** = X̂ × Ẑ — lateral (right-handed)

The sign of the mean local Y coordinate tells us which side of the forward axis the skier passed on:

```
mean(P_local.y) < 0  →  right gate
mean(P_local.y) > 0  →  left gate
```

The Ŷ sign convention is validated once on a known right gate from real data and flipped if reversed — one-line fix.

### Integration with RFID

When RFID tag IDs are available (⚠️ v2 only — SL with surveyed poles), gate side comes from the `course_gates` table. The local-frame classification acts as a cross-check and fallback when RFID reads are missing. For GS/SG/DH, the local-frame result is the primary classification.

```dart
GateSide classifySide(
  List<SensorFrame> frames,
  List<ZeroCrossing> zeros,
  int i, // zero pair [i, i+1]
) {
  // See §7 for full pipeline — this only extracts the L/R decision.
  final localYs = <double>[];
  for (int t = zeros[i].frameIndex; t <= zeros[i+1].frameIndex; t++) {
    localYs.add(projectToLocalY(frames[t], zeros[i], zeros[i+1]));
  }
  return localYs.reduce((a, b) => a + b) / localYs.length < 0
      ? GateSide.rightGate
      : GateSide.leftGate;
}
```

---

## 6. Impact Detector (`impact_detector.dart`)

```dart
class ImpactDetector {
  // Baseline window depends on discipline (shorter for SL, longer for speed events)
  static const int BASELINE_SL  = 30;   // 0.3s at 100 Hz
  static const int BASELINE_GS  = 50;   // 0.5s at 100 Hz
  
  static const double MULT_MIN = 1.5;   // kids
  static const double MULT_MAX = 3.0;   // adults
  static const int COOLDOWN_SAMPLES = 20; // 200 ms dead time between impacts
  
  final double multiplier;             // 1.5–3.0, set by slider (F20 athlete profile)
  final int baselineWindow;             // 30 (SL) or 50 (GS/SG/DH), set by discipline
  
  ImpactDetector({required this.multiplier, required this.baselineWindow});
  
  List<ImpactEvent> detect(List<SensorFrame> frames) {
    List<ImpactEvent> impacts = [];
    int lastImpactIdx = -COOLDOWN_SAMPLES;
    
    // Pre-compute acceleration magnitude for all frames
    List<double> mags = frames.map((f) =>
      sqrt(f.laX * f.laX + f.laY * f.laY + f.laZ * f.laZ) / 9.81
    ).toList();
    
    for (int i = baselineWindow; i < frames.length; i++) {
      // Rolling median of last baselineWindow samples
      List<double> window = mags.sublist(i - baselineWindow, i);
      window.sort();
      double baseline = window[window.length ~/ 2]; // median
      
      double threshold = baseline * multiplier;
      
      if (mags[i] > threshold && (i - lastImpactIdx) >= COOLDOWN_SAMPLES) {
        // Find local peak within ±3 samples
        double peakMag = mags[i];
        int peakIdx = i;
        for (int j = max(0, i - 3); j < min(frames.length, i + 4); j++) {
          if (mags[j] > peakMag) { peakMag = mags[j]; peakIdx = j; }
        }
        
        impacts.add(ImpactEvent(
          timeFromStart: frames[peakIdx].timeFromStart,
          force: peakMag,
        ));
        lastImpactIdx = peakIdx;
      }
    }
    
    return impacts;
  }
}
```

---

## 7. Gate Time Estimator (`gate_time_estimator.dart`)

Replaces the old `MissedGateEstimator`. Instead of time-gap heuristics and max rotation speed, this uses a kinematics-driven pipeline: rotation-speed zero-finding → local-frame coordinate transform → geometric interpolation (Case A) or statistical fallback (Case B). Works for all disciplines (SL, GS, SG, DH) without discipline-specific tuning.

**Precedence rule:** Impact-detected timestamps and RFID reads are hardware ground truth — they are used as-is, never overridden. The kinematics pipeline estimates times **only** for gates where no impact or RFID was registered (missed gates). The pipeline may optionally cross-check: if an estimated time diverges significantly from a known impact time for the same gate, flag it as a data quality warning.

### 7.1 Pipeline Overview

```
q(t), a(t), P(t), gates_world, known_hits
              │
    7.1  ω(t) = arccos(q·q) · 2/Δt
              │
    7.2  LPF 0.5 Hz  (4th-order Butterworth, filtfilt)
              │
    7.3  Zeros: rolling_avg(|ω_filt|, 0.2s) < 0.3 rad/s
              │
    7.4  P(t): anchor → IMU ΔP → linear drift correction
         (0 anchors → Bronze tier fallback (IMU turn counting) | 1 → IMU only | ≥2 → full)
              │
    7.5  Per zero pair: X̂, Ẑ (Gram-Schmidt), Ŷ = X̂×Ẑ
              │
    7.6  L/R: mean(P_local.y) < 0 → right gate
              │
    7.7  Calibrate A from Case-A gates
              │
    7.8  Per gate: Case A (interpolate) or Case B (A%)
              │
         → t_hit for every gate
```

**Computational cost:** < 0.5 ms per 90 s run (100 Hz). Runs on-device, post-run.

### 7.2 Rotation Speed

```dart
List<double> computeRotationSpeed(List<SensorFrame> frames) {
  final omega = <double>[];
  const dt = 0.010; // 10 ms at 100 Hz
  for (int i = 1; i < frames.length; i++) {
    final dot = frames[i].qW * frames[i-1].qW +
                frames[i].qX * frames[i-1].qX +
                frames[i].qY * frames[i-1].qY +
                frames[i].qZ * frames[i-1].qZ;
    final angle = 2 * acos(min(1.0, dot.abs())); // rad
    omega.add(angle / dt);                         // rad/s
  }
  return omega;
}
```

### 7.3 Low-Pass Filter (0.5 Hz) + Zero-Finding

```dart
List<ZeroCrossing> findZeros(List<double> omega, List<SensorFrame> frames) {
  // 4th-order Butterworth, 0.5 Hz, zero-phase (filtfilt)
  final omegaFilt = butterworthFiltfilt(omega, cutoffHz: 0.5, order: 4);
  
  // Rolling average over 0.2 s = 20 samples at 100 Hz
  const window = 20;        // 0.2 s
  const threshold = 0.3;    // rad/s
  
  final zeros = <ZeroCrossing>[];
  int? belowStart;
  
  for (int i = window; i < omegaFilt.length; i++) {
    final avg = omegaFilt.sublist(i - window, i).reduce((a, b) => a + b) / window;
    
    if (avg.abs() < threshold) {
      if (belowStart == null) belowStart = i;
    } else {
      if (belowStart != null) {
        // Center of below-threshold region
        final centerIdx = (belowStart + i) ~/ 2;
        zeros.add(ZeroCrossing(
          timeFromStart: frames[centerIdx].timeFromStart,
          frameIndex: centerIdx,
        ));
        belowStart = null;
      }
    }
  }
  
  // Local-minimum fallback for linked flush gates:
  // if gap between zeros > 1.5 s, insert zero at min ω_filt in that window
  // (see §7.9 Edge Cases)
  
  return zeros;
}
```

**Suggested threshold `x = 0.3 rad/s`.** Rationale: peak rotation SL ~3–5 rad/s, GS ~2–3, SG/DH ~1–2. Near-zero should be <10% of typical peak. Tune on real data per discipline if needed.

### 7.4 Position Trajectory — Anchored & Drift-Corrected

Gate pole positions come from **course setup** (F57-F60), not from the athlete's phone during the run. Athletes do not carry phones on course. The workflow:

1. **Course setup (trainer before runs):** Trainer walks the course with their phone — Mode A (New Course: sequential recording) or Mode B (Update Existing: GPS + ΔP detection with Move/Delete/Add). Pressure deltas (ΔP_n = P_n − P_start) and GPS positions are persisted to the cloud (`courses` + `course_gates` tables). See sgc_architecture_decisions.md AD-007 for full flow.
2. **Post-run (phone, on first cloud upload):** The phone retrieves the course map and runs gate detection (pressure ΔP + IMU).
   ```sql
   SELECT c.* FROM courses c
   JOIN users2groups u2g ON c.created_by = u2g.user_id
   JOIN users2groups u2g2 ON u2g.group_id = u2g2.group_id
   WHERE u2g2.user_id = :athlete_id
     AND u2g.role = 'trainer'
     AND ABS(EXTRACT(EPOCH FROM c.created_at - :run_start)) < 7200  -- ±2h
   ORDER BY ABS(EXTRACT(EPOCH FROM c.created_at - :run_start)) ASC
   LIMIT 1
   ```
   If a match is found, the phone writes `runs.course_id` back to cloud. This makes the association deterministic — the run forever references the exact course used for estimation, even if the course is later deleted.
3. **Re-opening a run:** The phone simply loads `run.course_id` → `course_gates` — no re-matching needed.
4. **Manual fallback:** The user can manually assign a course (or change the auto-match) if needed.

Once pole positions are available in (lat, lon, altitude), they are converted to a local world-frame `Vec3` using a flat-Earth approximation centered on the course centroid:

> **v1 delta-based anchoring:** Course maps store ΔGPS vectors and ΔP deltas. The phone reconstructs absolute positions from START by chaining the deltas: `P_n = P_start + Σ_{i=0}^n ΔGPS_i`. This reconstruction happens once when loading the course map; thereafter the gate positions are treated as absolute world-frame coordinates for the estimator.

```dart
Vec3 latLonToWorld(double lat, double lon, double altM, Vec3 origin) {
  const metersPerDegLat = 111320.0;
  final metersPerDegLon = 111320.0 * cos(origin.y * pi / 180);
  return Vec3(
    (lon - origin.x) * metersPerDegLon,
    (lat - origin.y) * metersPerDegLat,
    altM,  // Z = altitude (world-frame Z-up)
  );
}
```

Positions are anchored to known gate pole coordinates (surveyed ground truth) and corrected for IMU drift.

| Anchors near segment | Strategy |
|----------------------|----------|
| **0** | **Bronze tier fallback:** IMU turn counting + impact detection only. No position anchoring — gate times estimated from turn centers (midpoint of [z_i, z_{i+1}]). Gate count is correct; individual timestamps are approximate (±200 ms). L/R classification from Y half-plane still works. Prompt user to download/assign a course to upgrade to Gold tier (±50 ms). |
| **1** | P(t) = P_anchor + IMU_ΔP(t → t_anchor). No smoothing. Drift grows with distance. |
| **≥ 2** | Full pipeline: IMU integration + linear drift correction between consecutive anchors. P snaps to pole at each known hit. |

```dart
List<Vec3> buildPositionTrajectory(
  List<SensorFrame> frames,
  List<({Duration time, Vec3 pos})> knownHits,  // gates with known t_hit AND surveyed pole coords
) {
  if (knownHits.isEmpty) {
    // 0 anchors: Bronze tier — no course data. Gate times from turn centers.
    // Does NOT throw — caller routes to Bronze pathway gracefully.
    return null;
  }
  
  // Integrate IMU acceleration to world-frame displacement
  // P_imu(t) = starting_point + ∫∫ R(q(t)) · (a(t) − g) dt²
  final pImu = integrateImuPosition(frames, knownHits.first.timeFromStart);
  
  if (knownHits.length == 1) {
    // 1 anchor: offset entire trajectory to match
    final offset = knownHits[0].pos - pImu[anchorFrameIndex(knownHits[0].time, frames)];
    return pImu.map((p) => p + offset).toList();
  }
  
  // ≥ 2 anchors: linear drift correction between consecutive anchors
  return applyLinearDriftCorrection(pImu, frames, knownHits);
}

List<Vec3> applyLinearDriftCorrection(
  List<Vec3> pImu,
  List<SensorFrame> frames,
  List<({Duration time, Vec3 pos})> anchors,
) {
  final corrected = List<Vec3>.from(pImu);
  
  for (int a = 0; a < anchors.length - 1; a++) {
    final tA = anchors[a].time.inMilliseconds / 1000.0;
    final tB = anchors[a + 1].time.inMilliseconds / 1000.0;
    final idxA = frameIndexAt(frames, anchors[a].time);
    final idxB = frameIndexAt(frames, anchors[a + 1].time);
    
    // Drift at anchor B: how far IMU position differs from surveyed pole
    final driftB = anchors[a + 1].pos - pImu[idxB];
    
    // Linear correction from anchor A to anchor B
    for (int i = idxA; i <= idxB; i++) {
      final alpha = (frames[i].timeFromStart.inMilliseconds / 1000.0 - tA) / (tB - tA);
      corrected[i] = pImu[i] + driftB * alpha;
    }
  }
  
  return corrected;
}
```

### 7.5 Local Frame (per Zero Pair)

For each consecutive pair of zeros [i, i+1], build a local orthogonal basis:

```dart
class LocalFrame {
  final Vec3 xHat;   // forward: P_{i+1} − P_i, normalized
  final Vec3 zHat;   // up, ⊥ xHat, in X-gravity plane (Gram-Schmidt)
  final Vec3 yHat;   // lateral = xHat × zHat
  
  LocalFrame(Vec3 pI, Vec3 pI1) {
    xHat = (pI1 - pI).normalized();
    
    // Gravity vector in world frame (from accelerometer at rest or IMU reference)
    const up = Vec3(0, 0, 1);   // Z-up convention (world frame)
    final upPerp = up - (up.dot(xHat)) * xHat;  // remove X-component
    zHat = upPerp.normalized();
    
    yHat = xHat.cross(zHat);
    // Sign convention: validate on one known right gate, flip yHat if needed
  }
  
  Vec3 worldToLocal(Vec3 world) {
    // R_L→W^T · world
    return Vec3(
      xHat.dot(world),
      yHat.dot(world),
      zHat.dot(world),
    );
  }
}
```

### 7.6 Left/Right Classification

```dart
GateSide classifySide(List<Vec3> pLocal, ZeroCrossing zI, ZeroCrossing zI1) {
  // Average Y position of the trajectory between zeros
  final sumY = pLocal.map((p) => p.y).reduce((a, b) => a + b);
  final meanY = sumY / pLocal.length;
  return meanY < 0 ? GateSide.rightGate : GateSide.leftGate;
}
```

Validate sign convention on one known right gate from real data — flip Ŷ if reversed.

### 7.7 Calibrate A

`A` = spatial percentage where the hit occurs within a turn, learned from all Case-A gates (gates where a surveyed pole coordinate projects between the zero positions).

```dart
double calibrateA(
  List<GateTimestamp> knownHits,  // gates with known t_hit AND surveyed pole coords
  List<Vec3> gatePositionsWorld,
  List<SensorFrame> frames,
  List<ZeroCrossing> zeros,
  List<Vec3> positionTrajectory,
) {
  final ratios = <double>[];
  
  for (int k = 0; k < knownHits.length; k++) {
    final G = gatePositionsWorld[k];
    final tHit = knownHits[k].timeFromStart;
    
    // Find which zero pair contains this gate
    final seg = findSegment(zeros, tHit);
    if (seg == null) continue;
    
    final frame = buildLocalFrame(positionTrajectory, zeros, seg.i);
    final D = (positionTrajectory[zeros[seg.i + 1].frameIndex] -
               positionTrajectory[zeros[seg.i].frameIndex]).length;
    final G_local = frame.worldToLocal(G - positionTrajectory[zeros[seg.i].frameIndex]);
    
    if (G_local.x > 0 && G_local.x < D) {
      ratios.add(G_local.x / D);
    }
  }
  
  return ratios.isEmpty ? 0.5 : ratios.reduce((a, b) => a + b) / ratios.length;
}
```

### 7.8 Time Estimation — Case A / Case B

```dart
class GateTimeEstimator {
  final double A;                  // calibrated spatial hit percentage
  final List<ZeroCrossing> zeros;
  final List<Vec3> pTrajectory;
  
  GateTimeEstimator({
    required this.A,
    required this.zeros,
    required this.pTrajectory,
  });
  
  Duration estimateTime(Vec3 gateWorld, List<SensorFrame> frames) {
    // Find the zero pair that brackets this gate (by X projection)
    for (int i = 0; i < zeros.length - 1; i++) {
      final frame = LocalFrame(
        pTrajectory[zeros[i].frameIndex],
        pTrajectory[zeros[i + 1].frameIndex],
      );
      
      final origin = pTrajectory[zeros[i].frameIndex];
      final G_local = frame.worldToLocal(gateWorld - origin);
      final D = (pTrajectory[zeros[i + 1].frameIndex] - origin).length;
      
      if (G_local.x > 0 && G_local.x < D) {
        // Case A: gate pole projects between zeros — geometric interpolation
        return interpolateTimeAtX(
          frames, zeros[i].frameIndex, zeros[i + 1].frameIndex,
          origin, frame, G_local.x, pTrajectory,
        );
      }
    }
    
    // Case B: no pole in any zero-pair X range — statistical fallback
    // Assign to the temporally nearest zero pair
    final nearest = findNearestZeroPair(zeros, gateWorld, pTrajectory);
    final tI = zeros[nearest.i].timeFromStart;
    final tI1 = zeros[nearest.i + 1].timeFromStart;
    final deltaMs = tI1.inMilliseconds - tI.inMilliseconds;
    
    return Duration(milliseconds: tI.inMilliseconds + (deltaMs * A).round());
  }
  
  Duration interpolateTimeAtX(
    List<SensorFrame> frames,
    int idxI, int idxI1,
    Vec3 origin, LocalFrame frame,
    double targetX,
    List<Vec3> pTrajectory,
  ) {
    // Binary search or linear scan over frames between zeros
    // Find t where P_local.x(t) = targetX (crossing of Y-parallel line through pole)
    for (int i = idxI; i <= idxI1; i++) {
      final pLocal = frame.worldToLocal(pTrajectory[i] - origin);
      if (pLocal.x >= targetX) {
        // Linear interpolation between frames i-1 and i
        final pPrev = frame.worldToLocal(pTrajectory[i - 1] - origin);
        final alpha = (targetX - pPrev.x) / (pLocal.x - pPrev.x);
        final tPrev = frames[i - 1].timeFromStart.inMilliseconds;
        final tCurr = frames[i].timeFromStart.inMilliseconds;
        return Duration(milliseconds: (tPrev + (tCurr - tPrev) * alpha).round());
      }
    }
    // Fallback: center of segment
    return frames[(idxI + idxI1) ~/ 2].timeFromStart;
  }
}
```

### 7.9 Edge Cases

| Situation | Handling |
|-----------|----------|
| 0 anchors | **Bronze tier:** IMU turn counting + impact detection only. Gate times from turn centers (midpoint of [z_i, z_{i+1}]). Gate count correct; timestamps approximate (±200 ms). L/R from Y half-plane still works. Prompt user to download/assign a course to upgrade to Gold tier (±50 ms). |
| 1 anchor | IMU integration from single pole. No drift correction. Accuracy degrades with distance. |
| Linked flush gates (no zero) | Local minimum fallback: if gap between zeros > 1.5 s, insert zero at min ω_filt in that window. |
| First/last turn incomplete | Discard zero pairs at boundaries if ω never dropped. |
| A undefined (no Case-A gates) | Default A = 0.5 (center assumption). Improves as data accumulates. |
| Sign convention | Validate on one known right gate from real data; flip Ŷ if needed. |

**Precision target:** ±50 ms (F26). At 100 Hz, resolution is 10 ms — well within target.

### 7.10 Why This Replaces MissedGateEstimator

| Old approach | New approach |
|-------------|-------------|
| Time-gap heuristics (error-prone for SG/DH) | Kinematics-driven zero-finding |
| Max rotation speed (corrupted by arm swings) | 0.5 Hz LPF isolates torso rotation |
| Discipline-specific thresholds | Discipline-agnostic (x = 0.3 rad/s, tunable) |
| Guessed times from max ω frame | Geometric interpolation (Case A) or learned pattern (Case B) |
| Alternating side assumption (fails on verticals/bananas) | Spatial Y half-plane analysis |
| No position anchoring | Pole-anchored + drift-corrected position trajectory |

---

## 8. Banana Detector (`banana_detector.dart`)

```dart
class BananaDetector {
  List<GateTimestamp> detect(List<GateTimestamp> gates) {
    // A "banana" is two consecutive gates on the same side
    // Normal pattern: R, L, R, L, R, L, ...
    // Banana:         R, L, L, R, ...  (the two L's are a banana)
    //                 or L, R, R, L, ...
    
    for (int i = 1; i < gates.length; i++) {
      if (gates[i].side == gates[i - 1].side) {
        // Found banana — flag both gates
        gates[i - 1] = gates[i - 1].copyWith(isBanana: true);
        gates[i] = gates[i].copyWith(isBanana: true);
      }
    }
    
    return gates;
  }
}
```

---

## 9. Cross-Correlator (`cross_correlator.dart`)

### Dual-Arm Timeline Alignment (F16)

```dart
class CrossCorrelator {
  /// Align left and right arm timelines using quaternion dot-product cross-correlation.
  /// Returns time offset: offset_ms = right_time - left_time.
  /// Precision: < 10 ms (F16).
  int align(List<SensorFrame> leftFrames, List<SensorFrame> rightFrames) {
    // 1. Extract quaternion magnitude traces from both arms
    //    mag(t) = sqrt(qx² + qy² + qz²) — rotation magnitude, arm-agnostic
    List<double> leftMag = leftFrames.map((f) => sqrt(f.qX*f.qX + f.qY*f.qY + f.qZ*f.qZ)).toList();
    List<double> rightMag = rightFrames.map((f) => sqrt(f.qX*f.qX + f.qY*f.qY + f.qZ*f.qZ)).toList();
    
    // 2. Cross-correlate: compute dot product at each offset
    //    corr(offset) = Σ leftMag[t] * rightMag[t + offset]
    int maxOffset = min(leftFrames.length, rightFrames.length) ~/ 2;
    double bestCorr = -1;
    int bestOffset = 0;
    
    for (int offset = -maxOffset; offset <= maxOffset; offset++) {
      double corr = 0;
      int n = 0;
      for (int t = max(0, -offset); t < min(leftFrames.length, rightFrames.length - offset); t++) {
        corr += leftMag[t] * rightMag[t + offset];
        n++;
      }
      corr /= n; // normalize
      
      if (corr > bestCorr) {
        bestCorr = corr;
        bestOffset = offset;
      }
    }
    
    // 3. Convert frame offset to milliseconds
    return bestOffset * 10; // 10 ms per frame at 100 Hz
  }
}
```

**Fallback:** If cross-correlation fails (run < 10 s) or only one arm available (F49, R08), use RTC timestamp proximity (±3 s per F15) as backup alignment.

---

## 10. Barometric Speed (`barometric_speed.dart`)  (F30)

```dart
class BarometricSpeed {
  /// Compute vertical speed from barometric pressure trace.
  /// Input: 100 Hz pressure values from decompressed SensorFrames.
  ///         Already IIR-filtered by BMP390 on-device (coefficient 3) at 100 Hz.
  /// Output: 10 Hz altitude (m) + vertical speed (m/s).
  /// Decimation: average 10 consecutive frames (100ms window) → cleaner derivative.
  /// No phone-side low-pass filter needed — BMP390 hardware filter already applied.
  List<BarometricPoint> compute(List<SensorFrame> frames) {
    List<BarometricPoint> points = [];
    
    double p0 = frames.first.baroPressurePa;
    List<double> alts = [];
    
    // Decimate 100→10 Hz: average 10-sample windows (100 ms each)
    for (int i = 0; i < frames.length; i += 10) {
      double avgPa = 0;
      int count = 0;
      for (int j = i; j < min(frames.length, i + 10); j++) {
        avgPa += frames[j].baroPressurePa;
        count++;
      }
      avgPa /= count;
      // Pressure to altitude: h = 44330 × (1 − (p/p0)^(1/5.255))
      double alt = 44330 * (1 - pow(avgPa / p0, 1 / 5.255));
      alts.add(alt);
    }
    
    // Differentiate altitude → vertical speed (central difference, 200 ms window)
    // v(t) = (h(t+1) − h(t−1)) / 0.200
    for (int i = 1; i < alts.length - 1; i++) {
      double speed = (alts[i + 1] - alts[i - 1]) / 0.200; // m/s, positive = descending
      points.add(BarometricPoint(
        timeFromStart: Duration(milliseconds: i * 100), // 10 Hz cadence
        altitudeM: alts[i],
        verticalSpeedMs: speed,
      ));
    }
    
    return points;
  }
}
```

---

## 11. Cloud Sync (`cloud/`)

### API Client (`api_client.dart`)

```dart
class ApiClient {
  final String baseUrl;
  final String authToken;  // JWT from authentication
  
  Future<Response> post(String path, Map<String, dynamic> body);
  Future<Response> get(String path);
  Future<Response> delete(String path);
  
  // Run endpoints
  Future<String> uploadRun(Run run, List<GateTimestamp> timestamps, List<BarometricPoint> baroData);
  Future<Run> getRun(String cloudRunId);
  Future<void> deleteRun(String cloudRunId);           // trainer/master only (F46)
  
  // Group endpoints
  Future<List<AthleteSummary>> getGroupAthletes();
  Future<void> setGroupVisibility(String groupId, String visibility); // F35
  Future<void> setRunVisibility(String runId, String visibility);    // F36
  
  // GDPR
  Future<void> deleteAthleteData(String athleteId);    // F50
}
```

### Bootstrap (`bootstrap.dart`)

```dart
class Bootstrap {
  static const String HARDCODED_URL = 'https://api.sgc-ski.com/bootstrap'; // F24
  
  Future<String> getCloudEndpoint() async {
    // HTTP GET to HARDCODED_URL → returns JSON { "api_base": "https://..." }
    // This URL can change over time — the hardcoded bootstrap acts as a redirect
    final response = await http.get(Uri.parse(HARDCODED_URL));
    final json = jsonDecode(response.body);
    return json['api_base'];
  }
}
```

### Sync Manager (`sync_manager.dart`)

```dart
class SyncManager {
  final ApiClient api;
  final LocalDB db;
  
  /// Push queued runs to cloud (F23, F43).
  /// Called: after each run download (if pushToCloud=true), and periodically.
  Future<void> sync() async {
    List<Run> queued = await db.getUnuploadedRuns();
    
    for (var run in queued) {
      try {
        // Upload only gate timestamps + barometric data (10 Hz)
        // NOT raw 100 Hz sensor frames (F23)
        String cloudId = await api.uploadRun(run, run.gateTimestamps, run.barometricData);
        await db.markUploaded(run.id, cloudId);
      } on SocketException {
        // Offline — queue persists in local DB. Retry on next sync() call.
        break;
      } on HttpException catch (e) {
        if (e.statusCode == 409) {
          // Conflict: run already exists on server (duplicate upload)
          await db.markUploaded(run.id, null);
        } else {
          // Other error — log and skip, don't block queue
          print('Upload failed for run ${run.id}: $e');
        }
      }
    }
  }
  
  /// Sync scheduler: attempt every 60 seconds when online.
  Timer? _timer;
  void start() {
    _timer = Timer.periodic(Duration(seconds: 60), (_) => sync());
  }
}
```

### GDPR Deletion (`gdpr_deletion.dart`)

```dart
class GDPRDeletion {
  Future<bool> deleteAll(String athleteId) async {
    // 1. Show explicit warning (F50)
    // 2. Delete from cloud
    // 3. Delete from local DB
    // 4. Delete from device Flash (via BLE command)
    // 5. Confirmation message
    
    await api.deleteAthleteData(athleteId);
    await localDb.deleteAllRuns();
    // Device Flash deletion handled by trainer via BLE (F46)
    
    return true;
  }
}
```

---

## 12. UI Screens (`ui/`)

### State Management: Provider + ChangeNotifier

```dart
// app_state.dart
class AppState extends ChangeNotifier {
  UserProfile? profile;
  List<DeviceConfig> connectedDevices = [];
  List<Run> localRuns = [];
  bool isSyncing = false;
  
  // Actions
  Future<void> loadProfile();
  Future<void> saveProfile(UserProfile p);
  Future<void> refreshRunList();
  Future<Run> downloadRun(DeviceConfig device, int runId);
  Future<void> deleteRun(Run run);
  Future<void> exportRun(Run run, ExportFormat format);  // (F47) CSV+JSON export with metadata
  Future<void> syncToCloud();
}
```

### Run Viewer (`run_viewer.dart`)

The most complex screen — combines altitude graph, speed graph, and gate table.

```dart
class RunViewer extends StatefulWidget {
  final Run run;
  
  // Layout (F29, F31, F33):
  // ┌────────────────────────────────────┐
  // │  Run name: "Race simulation" (F45) │
  // ├────────────────────────────────────┤
  // │  ┌──────────────────┬────────────┐ │
  // │  │ Altitude graph   │ Gate table │ │
  // │  │ (with gate marks)│ #  L/R  Time│ │
  // │  │                  │ 1  R  1.23 │ │
  // │  │  green = right   │ 2  L  2.01 │ │
  // │  │  red   = left    │ 3* R  2.89*│ │ ← guessed (F27)
  // │  └──────────────────┴────────────┘ │
  // ├────────────────────────────────────┤
  // │  Speed graph (with gate marks)     │
  // │  ┌──────────────────────────────┐  │
  // │  │  green/red vertical lines     │  │
  // │  └──────────────────────────────┘  │
  // └────────────────────────────────────┘
}
```

### Gate Table (`gate_table.dart`)

```dart
class GateTable extends StatelessWidget {
  final List<GateTimestamp> gates;
  
  // Renders a two-column table:
  //   Known right gate:  left-aligned (impact/RFID + course_gates or local-frame Y half-plane)
  //   Known left gate:   right-aligned
  //   Undetermined side: center-aligned (RFID missing AND course_gates unavailable — rare)
  // Creates the stair-step pattern for known gates (F19, F44)
  
  // Estimated times displayed with trailing * (F27)
  // Banana gates flagged with 🍌 emoji or highlight (F28)
}
```

### Graph Widget (`graph_widget.dart`)

```dart
class GraphWidget extends StatelessWidget {
  final List<double> data;            // altitude or speed
  final List<GateTimestamp> gates;    // vertical marker positions
  final String yLabel;
  final Color lineColor;
  
  // Uses fl_chart or similar library
  // Gate markers: thin vertical lines
  //   green = right gate (left-aligned column)
  //   red   = left gate (right-aligned column)
  // Per F31, F33 convention
}
```

### Run Comparison (`run_comparison.dart`)

```dart
class RunComparison extends StatefulWidget {
  final Run runA;
  final Run runB;
  
  // Side-by-side or overlaid graphs (F22, F32, F34)
  // Altitude comparison: reveals line choice differences (F34)
  // Speed comparison: reveals technique differences (F32)
  // Gate table: run A timestamps overlaid on run B (F22)
  // Both local and cloud runs supported (F48)
}
```

### Device Config Screen (`device_config_screen.dart`)

```dart
class DeviceConfigScreen extends StatefulWidget {
  // BLE device parameter editor (F21)
  // Shows calibration status (🔴/🟢) per F51
  // Writes device name, arm side, discipline
  // Prevents arming if calibration accuracy < 2 (F51)
}
```

### Trainer Dashboard (`trainer_dashboard.dart`)

```dart
class TrainerDashboard extends StatefulWidget {
  // Group athlete list + run browser (F25)
  // Run deletion with confirmation (F46)
  // Visibility controls: group-level (F35) + per-run override (F36)
  
  // Layout:
  // ┌────────────────────────────────────┐
  // │  Group: "Sci Club Aosta"           │
  // │  Visibility: [full ▼]             │
  // ├────────────────────────────────────┤
  // │  Athletes:                         │
  // │  ├─ Mario Rossi  → 12 runs        │
  // │  ├─ Anna Bianchi → 8 runs         │
  // │  └─ Luca Verdi   → 15 runs        │
  // └────────────────────────────────────┘
}
```

---

## 13. Storage (`storage/`)

### Local Database (`local_db.dart`)

SQLite schema:

```sql
CREATE TABLE runs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  cloud_run_id TEXT,
  device_run_id INTEGER NOT NULL,
  arm_side TEXT NOT NULL,             -- 'left' | 'right'
  start_time INTEGER NOT NULL,        -- unixtime
  frame_count INTEGER NOT NULL,
  compressed_size_bytes INTEGER NOT NULL,
  format_version INTEGER NOT NULL DEFAULT 1,
  label TEXT,
  is_uploaded INTEGER NOT NULL DEFAULT 0,
  visibility TEXT DEFAULT 'full',
  compressed_data BLOB,               -- raw compressed file from device
  decompressed_cache BLOB             -- JSON-serialized List<SensorFrame> (nullable, lazy)
);

CREATE TABLE gate_timestamps (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
  gate_number INTEGER NOT NULL,
  side TEXT NOT NULL,                 -- 'leftGate' | 'rightGate'
  is_estimated INTEGER NOT NULL DEFAULT 0,
  time_ms INTEGER NOT NULL,           -- ms from run start
  impact_force REAL,                  -- null if estimated
  rfid_tag_id TEXT,                   -- null if no RFID read
  is_banana INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE barometric_data (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
  time_ms INTEGER NOT NULL,           -- ms from run start (10 Hz)
  altitude_m REAL NOT NULL,
  vertical_speed_ms REAL NOT NULL
);

CREATE TABLE sync_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
  queued_at INTEGER NOT NULL,         -- unixtime
  retry_count INTEGER NOT NULL DEFAULT 0
);
```

### Settings (`settings.dart`)

```dart
class Settings {
  static const _keyProfile = 'user_profile';
  static const _keyCloudEndpoint = 'cloud_endpoint';
  
  Future<UserProfile?> loadProfile() async {
    final prefs = await SharedPreferences.getInstance();
    final json = prefs.getString(_keyProfile);
    return json != null ? UserProfile.fromJson(jsonDecode(json)) : null;
  }
  
  Future<void> saveProfile(UserProfile p) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_keyProfile, jsonEncode(p.toJson()));
  }
  
  Future<String> getCloudEndpoint() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_keyCloudEndpoint) ?? await Bootstrap().getCloudEndpoint();
  }
}
```

---

## 14. Configuration (`config.dart`)

```dart
class AppConfig {
  // BLE
  static const String sgcServiceUuid = '53470000-0000-1000-8000-00805F9B34FB';
  static const String charCurrentTime    = '5347ABC0-0000-1000-8000-00805F9B34FB';
  static const String charDeviceName     = '5347ABC1-0000-1000-8000-00805F9B34FB';
  static const String charArmSide        = '5347ABC2-0000-1000-8000-00805F9B34FB';
  static const String charDiscipline     = '5347ABC3-0000-1000-8000-00805F9B34FB';
  static const String charDeviceState    = '5347ABC4-0000-1000-8000-00805F9B34FB';
  static const String charBattery        = '5347ABC5-0000-1000-8000-00805F9B34FB';
  static const String charCharging       = '5347ABCF-0000-1000-8000-00805F9B34FB';
  static const String charRunCount       = '5347ABC6-0000-1000-8000-00805F9B34FB';
  static const String charFlashUsed      = '5347ABC7-0000-1000-8000-00805F9B34FB';
  static const String charOldestRun      = '5347ABC8-0000-1000-8000-00805F9B34FB';
  static const String charRunList        = '5347ABC9-0000-1000-8000-00805F9B34FB';
  static const String charFtRequest      = '5347ABCA-0000-1000-8000-00805F9B34FB';
  static const String charFtChunk        = '5347ABCB-0000-1000-8000-00805F9B34FB';
  static const String charFtCrc          = '5347ABCC-0000-1000-8000-00805F9B34FB';
  static const String charFtStatus       = '5347ABCD-0000-1000-8000-00805F9B34FB';
  static const String charSensorStatus   = '5347ABCE-0000-1000-8000-00805F9B34FB';
  static const String charCalAccuracy    = '5347ABD0-0000-1000-8000-00805F9B34FB';
  
  // Cloud
  static const String bootstrapUrl = 'https://api.sgc-ski.com/bootstrap';
  
  // Timing
  static const int syncIntervalSeconds = 60;
  
  // File transfer
  static const int fileTransferChunkSize = 244;  // MTU 247 - 3B header
  static const Duration fileTransferTimeout = Duration(seconds: 30);
}
```

---

## 15. Main Flow (`main.dart`)

```dart
void main() {
  runApp(
    ChangeNotifierProvider(
      create: (_) => AppState(),
      child: MaterialApp(
        home: HomeScreen(),
        routes: {
          '/run':       (_) => RunViewer(),
          '/compare':   (_) => RunComparison(),
          '/config':    (_) => DeviceConfigScreen(),
          '/trainer':   (_) => TrainerDashboard(),
          '/profile':   (_) => ProfileScreen(),
          '/course_setup':(_) => CourseSetupScreen(),
        },
      ),
    ),
  );
}
```

### Run Processing Pipeline (v2)

> ⚠️ **Updated 2026-06-09.** The pipeline below reflects the v2 architecture: no RFID on device, gate detection via pressure ΔP + IMU on phone post-run. See `sgc_architecture_decisions.md` AD-004 for algorithm details.

```
LEFT ARM DEVICE                              RIGHT ARM DEVICE
     │                                              │
1. BLE download: compressed file → Uint8List   1. BLE download: compressed file → Uint8List
     │                                              │
2. Decompressor → List<SensorFrame>             2. Decompressor → List<SensorFrame>
     │                                              │
3. ImpactDetector → List<ImpactEvent>           3. ImpactDetector → List<ImpactEvent>
     │                                              │
     └──────────────┬────────────────────────────────┘
                    │
4. CrossCorrelator: left frames + right frames → timeOffsetMs
                    │
5. Merge: align timelines, interleave gate events from both arms
                    │
6. Retrieve course_map from cloud (optional — Gold tier; Bronze skips this step)
                    │
7. Gate Detection Pipeline:
   a. Compute ω(t), find zero-crossings (0.5 Hz LPF, 0.3 rad/s)

   ═══ Gold tier (course map available) ═══
   b. If course_map: match P(t) against stored ΔP deltas
   c. For each gate:
      - Try impact detection (a_lin peak in zero-crossing window)
      - Try pressure interpolation (find t where P(t) = P_start + ΔP_n, using course ΔP deltas)
      - Cross-check pressure result with IMU kinematics (zero-crossing + Y half-plane side)
      - Fall back to learned spatial percentage A (F26 Case B) when both pressure and impact fail

   ═══ Bronze tier (no course map) ═══
   d. Turn counting: one gate per zero-crossing pair [z_i, z_{i+1}]
      - Side from Y half-plane classification (same as Gold)
      - Timestamp = midpoint of zero-crossing pair
      - Impact detection on a_lin magnitude (no pressure cross-check available)

   e. BananaDetector → flag same-side consecutive gates
                    │
8. CombinedRun ready
                    │
9. Save both Runs + merged gate timestamps to local DB
                    │
10. Display in RunViewer (altitude + speed graphs + gate table)
                    │
11. (async) CloudSync → push timestamps + barometric data to cloud (F23)
```

### Three Operational Tiers

| Tier | Course Map | Detection Method | Accuracy |
|---|---|---|---|
| **Gold** | Trainer loaded a course map (today or selectively updated) | Pressure ΔP + IMU | ±50–100 ms |
| **Bronze** | No map | IMU turn counting + impact detection | Gate count correct, timestamps estimated |

Same hardware. Trainer chooses effort vs. precision.

### Course Map Format

The course map is stored as a `Course` + `CourseGate` list (see §2 for the unified model). Relative ΔGPS vectors cancel civilian GPS errors over short gate-to-gate windows. ΔP from START pressure is the primary detection signal in Gold tier.

```dart
// Unified model — see §2 for full field definitions
// Course { id, name, createdAtUnix, pStart, gates: List<CourseGate> }
// CourseGate { courseId, gateNumber, side, deltaP, deltaLat, deltaLon, deltaAltitudeM, rfidTagId }
//
// gates = [(START=0, deltaP=0), (1, ΔP₁, ΔGPS₁), (2, ΔP₂, ΔGPS₂), ...]
//
// Reconstruction at load time:
//   P_n = P_start + Σ_{i=0}^{n} ΔGPS_i (for kinematics pipeline)
//   P_n(t) = P_start + ΔP_n      (for pressure matching)
```

---

## 16. Course Setup Flow (`setup/course_setup.dart`) — v2

> **Two modes: New Course (sequential recording, no detection) vs. Update Course (GPS + ΔP detection with Move/Delete/Add). Dual view: graphical map (GPS available) ↔ text list (always available).**
>
> **Implementation:** `course_setup_screen.dart` uses a `SetupMode` enum (`none` / `newCourse` / `updateExisting`) to drive the UI. Mode A records gates sequentially as the trainer walks the course. Mode B uses GPS + ΔP dual-signal matching to detect the nearest existing gate, supporting Move / Delete / Add actions via `CourseUpdater`. See AD-007 for the full workflow rationale.

> **Phone ownership:** Course setup (Mode A/B) is performed on the **trainer's** phone, not the athlete's. The athlete's phone only downloads runs and displays results.

### Mode A: New Course — Sequential Recording

No stored gates. Trainer walks course in order, taps to record each gate. Phone auto-increments the counter. No detection logic — there's nothing to detect against.

```dart
class CourseSetup {
  int gateCounter = 0;
  double? pStart;
  CourseGate? prevGate;
  
  Future<void> recordGate() async {
    final pressure = await barometer.read();
    final gps = await geolocator.getCurrentPosition();
    
    if (gateCounter == 0) {
      // START
      pStart = pressure;
      gates.add(CourseGate(gateNumber: 0, deltaP: 0,
        deltaLat: null, deltaLon: null));
    } else {
      final deltaP = pressure - pStart!;
      final deltaGPS = prevGate!.gps != null
        ? gps - prevGate!.gps
        : null;
      gates.add(CourseGate(
        gateNumber: gateCounter,
        deltaP: deltaP,
        deltaLat: deltaGPS?.lat,
        deltaLon: deltaGPS?.lon,
      ));
    }
    gateCounter++;
    prevGate = gates.last;
  }
}
```

### Mode B: Update Existing Course — GPS + ΔP Detection

Stored map exists. Phone detects which gate the trainer is near using dual-signal matching.

```dart
class CourseUpdater {
  CourseMap map;
  
  GateMatch? detectNearestGate(Position gps, double pressure, double pStart) {
    GateMatch? best;
    double bestScore = 0;
    
    for (final gate in map.gates.where((g) => g.gateNumber > 0)) {
      // GPS proximity score
      double gpsDist = haversine(gps, gate.storedGPS);
      double gpsScore = gpsDist < 5 ? 1.0 : gpsDist < 15 ? 0.5 : 0.1;
      
      // Pressure delta match score
      double currentDeltaP = pressure - pStart;
      double deltaError = (currentDeltaP - gate.deltaP).abs();
      double pressureScore = deltaError < 0.1 ? 1.0 : deltaError < 0.3 ? 0.5 : 0.1;
      
      double combined = gpsScore * 0.5 + pressureScore * 0.5;
      if (combined > bestScore && combined > 0.4) {
        bestScore = combined;
        best = GateMatch(gate, combined, gpsDist, deltaError);
      }
    }
    return best;
  }
  
  // Three actions on a detected gate:
  Future<void> moveGate(int gateNumber) async { /* record new P, GPS, keep sequence */ }
  Future<void> deleteGate(int gateNumber) async { /* remove, renumber */ }
  Future<void> addGateAfter(int gateNumber) async { /* insert, renumber */ }
}
```

### Dual View Mode

```dart
enum CourseSetupView { list, map }

// Map view available only when course has GPS data
bool get mapAvailable => map.gates.any((g) => g.hasGPS);

// Toggle button: [MAP] / [LIST]
// Map draws gates from stored GPS, trainer position as live dot
// Nearest detected gate pulses/highlights
// List shows table: #, ΔP, GPS, actions
```

---

## 17. Phone Test Strategy (v1.0)

*2026-06-23 — Initial test architecture.*
*2026-06-24 — Cross-referenced with `implementation/test/MASTER_TEST_PLAN.md` and `implementation/test/TEST_COVERAGE_MATRIX.md`.*

> 📋 **See also:** [MASTER_TEST_PLAN.md](module_design/implementation/MASTER_TEST_PLAN.md) (overall SGC test strategy) · [TEST_COVERAGE_MATRIX.md](module_design/implementation/TEST_COVERAGE_MATRIX.md) (all v1 requirements → method, hardware, peripheral, status) · [Phone TEST_SPEC](module_design/unit_tests/phone/TEST_SPEC.md) (phone-specific test spec + requirement coverage)

### Three-Tier Test Pyramid

```
         ┌──────────────────┐
         │  ADB Integration │  ← Real phone + real hardware
         │  (Python harness) │
         ├──────────────────┤
         │  Widget / Mock   │  ← Flutter widget_test + mock BLE
         │  BLE Tests       │
         ├──────────────────┤
         │  Pure Dart Unit  │  ← flutter test, zero hardware
         │  Tests (80%+)    │
         └──────────────────┘
```

### Tier 1: Pure Dart Unit Tests (Implemented)

**Location:** `test/processing/`, `test/models/`  
**Runner:** `flutter test` — zero hardware, runs in CI (GitHub Actions) with no emulator.

| Module | Test file | Covers |
|---|---|---|
| Decompressor | `test/processing/decompressor_test.dart` | Header parsing, Type 1/2/3 decoding, delta accumulation, absolute reset, pressure→altitude, edge cases |
| ImpactDetector | `test/processing/impact_detector_test.dart` | Single impact, multi-impact (6 gates), cooldown, threshold filtering, child/adult multiplier |
| CrossCorrelator | `test/processing/cross_correlator_test.dart` | ±150ms offset recovery, 0ms alignment, negative offset, short-run guard, reproducibility, ±3s window |
| GateTimeEstimator | `test/processing/gate_time_estimator_test.dart` | Bronze tier (no course), Gold tier (pressure matching), impact-detected vs estimated, alternating L/R, monotonic timestamps |
| Vec3 | `test/models/vec3_test.dart` | Addition, subtraction, scalar multiply, dot product, cross product, length, normalization |
| Run / Models | `test/models/run_test.dart` | SensorFrame, GateTimestamp, Run, CombinedRun, MergedGate, BarometricPoint, enums |

**Test data:** `test/data/synthetic_data.dart` — deterministic generators for decompressor blobs, impact frames, correlated streams, slalom runs. Companion Python script at `scripts/generate_phone_test_data.py` produces identical fixtures for cross-validation.

**Running:**
```bash
# Via Python runner (saves structured results to files)
python scripts/run_phone_tests.py                    # all tests
python scripts/run_phone_tests.py --filter cross     # filter by name
python scripts/run_phone_tests.py --regenerate       # regenerate fixtures first
python scripts/run_phone_tests.py --latest           # show last results

# Direct flutter test (console only, no file output)
cd Phone_app_prototype
flutter test                    # all tests
flutter test test/processing/   # processing only
flutter test test/models/       # models only
```

**Results output:** `test/results/YYYY-MM-DD_HHMMSS.json` + `.log` + `latest.json`.
Structured JSON with per-suite/per-test pass/fail, duration, and error details —
same pattern as device test reports.

### Tier 2: Widget + Mock BLE Tests (Planned)

- Mock `flutter_blue_plus` to simulate device discovery, connection, file transfer
- Widget tests for gate table L/R alignment, graph rendering, banana flags
- Run with `flutter test` — no phone needed (uses Flutter's test widget environment)

### Tier 3: Python + ADB Integration Tests (Planned)

**Architecture:**
```
Python harness (ADB) → Android Phone + SGC App → BLE → Nicla Sense ME (×2)
```

**Equivalent to firmware test harness:** Python sends ADB commands (`adb shell input tap`, `adb shell am start`, `adb logcat`) to control the phone, just like the firmware harness uses PySerial. App exposes a debug HTTP endpoint (`localhost:9876`, via `adb forward`) for structured state access — equivalent to firmware's `T`/`B`/`Q`/`L`/`Z` test commands.

### Test Data Generation

All test data is deterministic — same seed → same output. Regeneration script:

```bash
# Phone test data (JSON + binary fixtures)
python scripts/generate_phone_test_data.py

# Device mock replay (for sgc_mock_runner.py)
python unit_tests/sgc_mock_runner.py --replay replay_data/sample_full_run.jsonl test_*.py
```

### CI Pipeline (Planned)

```yaml
# .github/workflows/sgc_phone_test.yml
name: SGC Phone Tests
on: [push, pull_request]
jobs:
  unit:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: subosito/flutter-action@v2
        with:
          flutter-version: '3.22.0'
      - run: flutter test
        working-directory: Phone_app_prototype
```

---

*Next: sgc_architecture_hardware.md — PCB stackup, component placement, antenna keepout zones, enclosure mechanical details, assembly instructions.*
