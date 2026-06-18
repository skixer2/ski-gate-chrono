import 'dart:math' show min, sqrt;
import '../models/sensor_frame.dart';

/// Dual-arm timeline alignment via quaternion magnitude cross-correlation.
///
/// Architecture §9: uses sqrt(qx² + qy² + qz²) — the rotation magnitude trace,
/// which is arm-agnostic (left and right arms rotate together through turns).
/// This is more robust than linear acceleration because arm swings and gate
/// impacts differ between left/right arms in a_lin but are synchronized in ω.
class CrossCorrelator {
  /// Compute time offset between left and right arm sensor streams.
  ///
  /// Returns offset in milliseconds: offset_ms = right_time - left_time.
  /// Precision target: < 10 ms (architecture §9, F16).
  int computeOffset(List<SensorFrame> left, List<SensorFrame> right) {
    if (left.length < 100 || right.length < 100) return 0;

    // 2. Cross-correlate: compute dot product at each offset within ±3s window.
    const range = 300; // ±3s window in frames (10 ms/frame at 100 Hz)
    final window = min(left.length ~/ 10, right.length ~/ 10);

    int bestOffset = 0;
    double bestCorr = -1;

    for (int off = -range; off <= range; off++) {
      // Find valid start index in both arrays for given offset
      final leftStart = off < 0 ? -off : 0;
      final rightStart = off > 0 ? off : 0;
      if (rightStart + window >= right.length ||
          leftStart + window >= left.length) continue;

      double corr = 0;
      for (int i = 0; i < window; i++) {
        final lMag = _quatMag(left[leftStart + i]);
        final rMag = _quatMag(right[rightStart + i]);
        corr += lMag * rMag;
      }
      if (corr > bestCorr) {
        bestCorr = corr;
        bestOffset = off;
      }
    }

    // 3. Convert frame offset to milliseconds (10 ms per frame at 100 Hz).
    return bestOffset * 10;
  }

  /// Compute quaternion magnitude: sqrt(qx² + qy² + qz²).
  double _quatMag(SensorFrame f) {
    final s = f.qX * f.qX + f.qY * f.qY + f.qZ * f.qZ;
    return s <= 0 ? 0.0 : sqrt(s);
  }
}
