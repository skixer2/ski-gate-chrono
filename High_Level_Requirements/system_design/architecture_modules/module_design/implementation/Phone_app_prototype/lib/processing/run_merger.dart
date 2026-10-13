import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'decompressor.dart';
import 'impact_detector.dart';
import 'gate_time_estimator.dart';
import '../models/gate_side.dart';
import '../models/gate_timestamp.dart';

/// Dual-device run merging (JP directive 2026-10-13 16:39).
///
/// A left-pole device confirms only left gates; a right-pole device only
/// right gates (verified: runs 32/33 bench pair). Merging the two
/// corresponding runs yields the full impact-confirmed course.
///
/// Pairing criteria (no clock dependency — relative times only):
///   - frame counts within 2% (same course length)
///   - one run confirms ONLY left gates, the other ONLY right gates
///   - at least 3 confirmed gates on each side
class RunMerger {
  /// Mirror of the run-detail pipeline: decompress → impacts → estimate.
  /// Runs inside a compute isolate (call via compute()).
  static List<Map<String, dynamic>> estimateGatesForIsolate(Uint8List data) {
    final result = Decompressor().decompressFull(data);
    final impacts =
        ImpactDetector(multiplier: 2.5, baselineWindow: 50).detect(result.frames);
    final gates = GateTimeEstimator(course: null, knownImpacts: impacts)
        .estimate(result.frames);
    return [
      for (final g in gates)
        {
          'ms': g.msFromStart,
          'side': g.side == GateSide.rightGate ? 'R' : 'L',
          'estimated': g.isEstimated,
          'force': g.impactForce,
        }
    ];
  }

  /// Confirmed-side summary of a gate list.
  /// Returns (confirmedL, confirmedR) counts.
  static (int, int) confirmedSides(List<Map<String, dynamic>> gates) {
    int l = 0, r = 0;
    for (final g in gates) {
      if (g['estimated'] == true) continue;
      if (g['side'] == 'R') {
        r++;
      } else {
        l++;
      }
    }
    return (l, r);
  }

  /// Are two runs a mergeable complementary pair?
  /// [framesA]/[framesB]: run frame counts (metadata, cheap).
  /// [gatesA]/[gatesB]: estimated gate lists (decoded, heavy).
  static bool isComplementaryPair({
    required int framesA,
    required int framesB,
    required List<Map<String, dynamic>> gatesA,
    required List<Map<String, dynamic>> gatesB,
  }) {
    final maxF = framesA > framesB ? framesA : framesB;
    if (maxF == 0) return false;
    final fDiff = (framesA - framesB).abs() / maxF;
    if (fDiff > 0.02) return false;

    final (la, ra) = confirmedSides(gatesA);
    final (lb, rb) = confirmedSides(gatesB);
    // One purely-left device + one purely-right device, both with real
    // evidence on their own side.
    final aPureLeft = la >= 3 && ra == 0;
    final aPureRight = ra >= 3 && la == 0;
    final bPureLeft = lb >= 3 && rb == 0;
    final bPureRight = rb >= 3 && lb == 0;
    return (aPureLeft && bPureRight) || (aPureRight && bPureLeft);
  }

  /// Merge: union of CONFIRMED gates from both runs, sorted by time.
  /// Estimated gates are dropped — the merged course is impact-only.
  static List<Map<String, dynamic>> mergeConfirmed(
    List<Map<String, dynamic>> gatesA,
    List<Map<String, dynamic>> gatesB,
  ) {
    final out = <Map<String, dynamic>>[
      ...gatesA.where((g) => g['estimated'] != true),
      ...gatesB.where((g) => g['estimated'] != true),
    ];
    out.sort((a, b) => (a['ms'] as int).compareTo(b['ms'] as int));
    return out;
  }
}
