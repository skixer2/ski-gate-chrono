import 'gate_side.dart';

/// CourseGate represents a single gate in a course map.
///
/// v1 uses RELATIVE delta positions — civilian GPS errors cancel over short
/// gate-to-gate windows. ΔP from START pressure is the primary detection signal.
/// v2 adds rfidTagId when poles are surveyed with UHF RFID tags.
///
/// Gate 0 = START (deltaP = 0, deltaLat/deltaLon = null).
/// Gates 1..N use ΔP from START and ΔGPS from the previous gate.
class CourseGate {
  final String? courseId;
  final int gateNumber;          // 0 = START, 1..N = gates
  final GateSide side;           // 'leftGate' | 'rightGate' (START has no side — use leftGate as placeholder)
  final double? deltaP;          // ΔP from START pressure (Pa). START = 0, null if unknown
  final double? deltaLat;        // ΔGPS from previous gate (deg), null for START
  final double? deltaLon;        // ΔGPS from previous gate (deg), null for START
  final double? deltaAltitudeM;  // Δ altitude from START (m). START = 0, null if unknown
  final String? rfidTagId;       // ⚠️ v2 only — EPC from pole tag (null in v1)

  const CourseGate({
    this.courseId,
    required this.gateNumber,
    required this.side,
    this.deltaP,
    this.deltaLat,
    this.deltaLon,
    this.deltaAltitudeM,
    this.rfidTagId,
  });
}

/// Course holds the full course definition loaded from cloud.
///
/// pStart is the absolute barometric pressure at START (Pa) — this is the
/// reference value used to compute ΔP_n = P(t) − pStart during gate detection.
///
/// Absolute gate positions for the kinematics pipeline (Case A/B) are reconstructed
/// from the delta chain: P_n = P_start + Σ_{i=0}^{n} ΔGPS_i. This happens once
/// when loading the course map; thereafter positions are treated as absolute.
class Course {
  final String id;
  final String name;
  final int createdAtUnix;
  final double pStart;           // absolute pressure at START (Pa) — reference for all ΔP_n
  final List<CourseGate> gates;

  const Course({
    required this.id,
    required this.name,
    required this.createdAtUnix,
    required this.pStart,
    required this.gates,
  });

  /// Returns the gate gates excluding START (gate 0).
  List<CourseGate> get nonStartGates =>
      gates.where((g) => g.gateNumber > 0).toList();

  /// Returns START gate (gate 0), or null if not present.
  CourseGate? get startGate =>
      gates.cast<CourseGate?>().firstWhere(
        (g) => g?.gateNumber == 0,
        orElse: () => null,
      );
}
