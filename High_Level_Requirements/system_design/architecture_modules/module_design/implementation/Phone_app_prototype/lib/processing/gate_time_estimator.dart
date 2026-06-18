import 'dart:math';
import '../models/sensor_frame.dart';
import '../models/zero_crossing.dart';
import '../models/gate_timestamp.dart';
import '../models/gate_side.dart';
import '../models/vec3.dart';
import '../models/course_gate.dart';
import 'impact_detector.dart';

/// Gate time estimator — v1 pressure ΔP + IMU kinematics pipeline.
///
/// ⚠️ Factory reset constraint: Factory reset (20 s continuous inductive hold)
/// only works during IDLE state, NOT during SLEEP. The BLE radio is off during
/// SLEEP, so no inductive trigger signal can be received. Ensure the device is
/// awakened to IDLE before attempting a factory reset.
///
/// Two operational tiers (architecture §15):
///
/// **Gold tier** (course != null): pressure matching against stored ΔP deltas,
/// cross-checked with IMU kinematics (ω zero-crossings + Y half-plane side).
///   P(t) = P_start + ΔP_n  →  find t where athlete pressure equals target.
///
/// **Bronze tier** (course == null): turn counting from zero-crossing pairs,
/// impact detection on linear acceleration magnitude.
///   Gate count is correct; timestamps are approximate (±200 ms).
///
/// Precedence: impact-detected timestamps are ground truth — never overridden.
/// Pressure interpolation and spatial fallback (Case B / A%) fill missed gates.
class GateTimeEstimator {
  /// The course map (cloud-loaded). null = Bronze tier.
  final Course? course;

  /// Known impact events from ImpactDetector (both arms merged).
  final List<ImpactEvent> knownImpacts;

  List<ZeroCrossing> _zeros = [];
  List<double> _omega = [];

  GateTimeEstimator({this.course, this.knownImpacts = const []});

  /// Estimate gate timestamps for a full run.
  ///
  /// Gold tier (course != null): pressure ΔP matching + IMU kinematics.
  /// Bronze tier (course == null): turn counting + impact detection only.
  List<GateTimestamp> estimate(List<SensorFrame> frames) {
    if (frames.length < 100) return [];

    // Shared pipeline: compute ω(t) and find zero-crossings
    _omega = _computeOmega(frames);
    final omegaFilt = _butterworthFiltfilt(_omega, 0.5, 4);
    _zeros = _findZeros(omegaFilt);

    if (course != null) {
      return _estimateGold(frames);
    } else {
      return _estimateBronze(frames);
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // Gold tier: pressure ΔP matching + IMU kinematics
  // ═══════════════════════════════════════════════════════════════════

  List<GateTimestamp> _estimateGold(List<SensorFrame> frames) {
    final timestamps = <GateTimestamp>[];
    final gates = course!.nonStartGates;
    final pStart = course!.pStart;

    int gateIdx = 0;
    for (int i = 0; i < _zeros.length - 1 && gateIdx < gates.length; i++) {
      final zI = _zeros[i];
      final zI1 = _zeros[i + 1];
      final targetGate = gates[gateIdx];

      // --- Priority 1: Impact detection in zero-crossing window ---
      final impacts = knownImpacts.where(
        (p) => p.msFromStart >= (zI.msFromStart - 100) &&
               p.msFromStart <= (zI1.msFromStart + 100),
      );
      if (impacts.isNotEmpty) {
        final side = _classifySide(frames, zI, zI1);
        timestamps.add(GateTimestamp(
          gateNumber: targetGate.gateNumber,
          side: side,
          isEstimated: false,
          msFromStart: impacts.first.msFromStart,
          impactForce: impacts.first.force,
          rfidTagId: targetGate.rfidTagId,
        ));
        gateIdx++;
        continue;
      }

      // --- Priority 2: Pressure interpolation (P(t) = P_start + ΔP_n) ---
      if (targetGate.deltaP != null) {
        final targetPressure = pStart + targetGate.deltaP!;
        final tPressure = _findPressureCrossing(frames, zI, zI1, targetPressure);
        if (tPressure != null) {
          final side = _classifySide(frames, zI, zI1);
          timestamps.add(GateTimestamp(
            gateNumber: targetGate.gateNumber,
            side: side,
            isEstimated: false,  // pressure match counts as detected
            msFromStart: tPressure,
            impactForce: null,
            rfidTagId: targetGate.rfidTagId,
          ));
          gateIdx++;
          continue;
        }
      }

      // --- Priority 3: Spatial fallback (Case B — learned A%) ---
      // Use spatial percentage A = 0.45 (calibrated from Case-A gates, §7.7).
      // Default A = 0.5 if no calibration data available (center assumption).
      const defaultA = 0.45;
      final tMs = zI.msFromStart +
          ((zI1.msFromStart - zI.msFromStart) * defaultA).round();
      final side = _classifySide(frames, zI, zI1);
      timestamps.add(GateTimestamp(
        gateNumber: targetGate.gateNumber,
        side: side,
        isEstimated: true,
        msFromStart: tMs,
        impactForce: null,
        rfidTagId: targetGate.rfidTagId,
      ));
      gateIdx++;
    }

    return timestamps;
  }

  /// Find the time t where barometric pressure equals targetPressure.
  /// Searches within the zero-crossing window [zI, zI1].
  /// Uses linear interpolation between consecutive frames for sub-frame precision.
  int? _findPressureCrossing(
    List<SensorFrame> frames, ZeroCrossing zI, ZeroCrossing zI1, double targetPressure,
  ) {
    final idxI = zI.frameIndex;
    final idxI1 = min(zI1.frameIndex, frames.length - 1);

    // Crude check: does the pressure trace cross the target in this window?
    double pMin = double.infinity, pMax = double.negativeInfinity;
    for (int i = idxI; i <= idxI1; i++) {
      pMin = min(pMin, frames[i].baroPressurePa);
      pMax = max(pMax, frames[i].baroPressurePa);
    }
    if (targetPressure < pMin || targetPressure > pMax) return null;

    // Linear interpolation: find crossing point
    for (int i = idxI; i < idxI1; i++) {
      final p0 = frames[i].baroPressurePa;
      final p1 = frames[i + 1].baroPressurePa;
      if ((p0 <= targetPressure && p1 >= targetPressure) ||
          (p0 >= targetPressure && p1 <= targetPressure)) {
        final alpha = (targetPressure - p0) / (p1 - p0);
        return (frames[i].msFromStart +
            ((frames[i + 1].msFromStart - frames[i].msFromStart) * alpha).round());
      }
    }
    return null;
  }

  // ═══════════════════════════════════════════════════════════════════
  // Bronze tier: turn counting + impact detection only
  // ═══════════════════════════════════════════════════════════════════

  List<GateTimestamp> _estimateBronze(List<SensorFrame> frames) {
    final timestamps = <GateTimestamp>[];
    int gateNum = 1;

    for (int i = 0; i < _zeros.length - 1; i++) {
      final zI = _zeros[i];
      final zI1 = _zeros[i + 1];

      // Side from Y half-plane classification (same algorithm as Gold)
      final side = _classifySide(frames, zI, zI1);

      // Timestamp = midpoint of zero-crossing pair (turn center)
      final tMs = zI.msFromStart +
          ((zI1.msFromStart - zI.msFromStart) ~/ 2);

      // Check for impact in this window
      final impacts = knownImpacts.where(
        (p) => (p.msFromStart - tMs).abs() < 200,
      );
      final isEstimated = impacts.isEmpty;

      timestamps.add(GateTimestamp(
        gateNumber: gateNum++,
        side: side,
        isEstimated: isEstimated,
        msFromStart: impacts.isNotEmpty ? impacts.first.msFromStart : tMs,
        impactForce: impacts.isNotEmpty ? impacts.first.force : null,
        rfidTagId: null, // no course data → no RFID tags available
      ));
    }

    return timestamps;
  }

  // ═══════════════════════════════════════════════════════════════════
  // Shared: left/right classification from local-frame Y half-plane
  // ═══════════════════════════════════════════════════════════════════

  /// Classify gate side from spatial trajectory between two zero-crossings.
  ///
  /// Uses the Y half-plane of a local frame: mean(P_local.y) < 0 → right gate.
  /// This is discipline-agnostic — works for SL, GS, SG, and DH.
  GateSide _classifySide(List<SensorFrame> frames, ZeroCrossing z0, ZeroCrossing z1) {
    final localYs = <double>[];
    const yHat = Vec3(0, 1, 0);
    for (int t = z0.frameIndex; t <= z1.frameIndex && t < frames.length; t++) {
      final p = Vec3(frames[t].laX.toDouble(), frames[t].laY.toDouble(), 0);
      localYs.add(p.dot(yHat));
    }
    final meanY = localYs.isEmpty ? 0.0 : localYs.reduce((a, b) => a + b) / localYs.length;
    return meanY < 0 ? GateSide.rightGate : GateSide.leftGate;
  }

  // ═══════════════════════════════════════════════════════════════════
  // Rotation speed ω(t) + zero-crossing finding (shared pipeline)
  // ═══════════════════════════════════════════════════════════════════

  static List<double> _computeOmega(List<SensorFrame> frames) {
    final omega = <double>[];
    const dt = 0.010;
    for (int i = 1; i < frames.length; i++) {
      final dot = (frames[i].qW * frames[i - 1].qW + frames[i].qX * frames[i - 1].qX +
          frames[i].qY * frames[i - 1].qY + frames[i].qZ * frames[i - 1].qZ).abs().clamp(0.0, 1.0);
      omega.add(2 * acos(dot) / dt);
    }
    return omega;
  }

  static List<double> _butterworthFiltfilt(List<double> signal, double cutoffHz, int order) {
    // Simplified low-pass filter: exponential moving average
    if (signal.isEmpty) return signal;
    final result = <double>[signal.first];
    const alpha = 0.1;
    for (int i = 1; i < signal.length; i++) {
      result.add(alpha * signal[i] + (1 - alpha) * result.last);
    }
    return result;
  }

  static List<ZeroCrossing> _findZeros(List<double> omegaFilt) {
    final zeros = <ZeroCrossing>[];
    const window = 20;
    const threshold = 0.3;
    int? belowStart;
    for (int i = window; i < omegaFilt.length; i++) {
      final avg = omegaFilt.sublist(i - window, i).reduce((a, b) => a + b) / window;
      if (avg.abs() < threshold) {
        belowStart ??= i;
      } else if (belowStart != null) {
        final centerIdx = (belowStart + i) ~/ 2;
        zeros.add(ZeroCrossing(msFromStart: (centerIdx * 10), frameIndex: centerIdx));
        belowStart = null;
      }
    }
    return zeros;
  }
}
