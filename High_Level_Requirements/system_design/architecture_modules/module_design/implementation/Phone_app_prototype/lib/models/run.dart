import 'sensor_frame.dart';
import 'barometric_point.dart';
import 'gate_timestamp.dart';
import 'gate_side.dart';

/// A Run represents raw data from a single SGC device (left arm OR right arm).
///
/// Each device produces one run file. The phone downloads both, then merges
/// them into a CombinedRun after cross-correlation alignment.
class Run {
  final int id;                    // local DB primary key
  final String? cloudRunId;        // set after cloud upload (architecture §2)
  final int deviceRunId;           // run_id from device Flash (2-byte)
  final ArmSide armSide;           // 'left' | 'right' — which arm recorded this
  final String deviceId;           // BLE MAC or bond ID
  final int startTimeUnix;         // UTC unixtime from device RTC
  final int frameCount;
  final int compressedSizeBytes;
  final int formatVersion;
  String? label;                   // user-editable (F45)
  bool isUploaded;
  String? visibility;              // 'full' | 'athlete_only' | 'denied' (architecture §2, F35)

  // Decompressed data (populated after BLE download):
  List<SensorFrame> frames;        // 100 Hz, nullable until decompressed
  List<BarometricPoint> barometricData; // 10 Hz altitude + speed
  List<GateTimestamp> gateTimestamps;   // detected + guessed gates from this arm only

  Run({
    required this.id,
    this.cloudRunId,
    required this.deviceRunId,
    required this.armSide,
    required this.deviceId,
    required this.startTimeUnix,
    required this.frameCount,
    required this.compressedSizeBytes,
    required this.formatVersion,
    this.label,
    this.isUploaded = false,
    this.visibility,
    this.frames = const [],
    this.barometricData = const [],
    this.gateTimestamps = const [],
  });

  Duration get duration => frames.isNotEmpty
      ? Duration(milliseconds: frames.last.msFromStart)
      : Duration.zero;
}

/// CombinedRun merges left and right arm runs after cross-correlation alignment.
///
/// Architecture §2: timeOffsetMs = right_time − left_time (from CrossCorrelator).
/// mergedGates interleaves gate events from both arms into a single L/R sequence.
/// combinedBaroData averages barometric pressure from both arms for cleaner
/// altitude/speed computation.
class CombinedRun {
  final Run leftRun;
  final Run rightRun;
  final int timeOffsetMs;              // right_time - left_time (from cross-correlator, F16)
  final List<MergedGate> mergedGates;  // interleaved L/R gate sequence
  final List<BarometricPoint> combinedBaroData; // averaged from both arms

  const CombinedRun({
    required this.leftRun,
    required this.rightRun,
    required this.timeOffsetMs,
    required this.mergedGates,
    required this.combinedBaroData,
  });
}

/// A single gate in the merged L/R sequence.
///
/// side is resolved to non-null by GateTimeEstimator (impact, pressure, or
/// local-frame Y half-plane classification). detectingArm tells which arm
/// physically touched this gate.
class MergedGate {
  final int gateNumber;
  final GateSide? side;            // leftGate | rightGate — null only during pipeline, resolved to non-null
  final String? detectingArm;      // 'left' | 'right' — which arm touched this gate (null if unknown)
  final bool isEstimated;
  final Duration timeFromStart;
  final double? impactForce;
  final String? rfidTagId;
  final bool isBanana;             // flagged by BananaDetector (two consecutive same-side gates)

  const MergedGate({
    required this.gateNumber,
    this.side,
    this.detectingArm,
    this.isEstimated = false,
    required this.timeFromStart,
    this.impactForce,
    this.rfidTagId,
    this.isBanana = false,
  });
}
