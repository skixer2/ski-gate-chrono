import 'dart:math' show sqrt, max, min;
import '../models/sensor_frame.dart';

class ImpactEvent {
  final int msFromStart;
  final double force;

  const ImpactEvent({required this.msFromStart, required this.force});
}

class ImpactDetector {
  final double multiplier;
  final int baselineWindow;
  static const int cooldownSamples = 20;

  /// Firmware / BHY2 LACC wire units are **mm/s²** (int16). |g| ≈ 9810 mm/s².
  /// (Was incorrectly 9.81 — treated LA as m/s² and inflated "g" by 1000×.)
  static const double gMmPerS2 = 9810.0;

  ImpactDetector({required this.multiplier, required this.baselineWindow});

  List<ImpactEvent> detect(List<SensorFrame> frames) {
    final impacts = <ImpactEvent>[];
    int lastImpactIdx = -cooldownSamples;
    final mags = frames
        .map((f) => sqrt(f.laX * f.laX + f.laY * f.laY + f.laZ * f.laZ) / gMmPerS2)
        .toList();

    for (int i = baselineWindow; i < frames.length; i++) {
      final window = mags.sublist(i - baselineWindow, i).toList()..sort();
      final baseline = window[window.length ~/ 2];
      final threshold = baseline * multiplier;

      if (mags[i] > threshold && (i - lastImpactIdx) >= cooldownSamples) {
        double peakMag = mags[i];
        int peakIdx = i;
        final start = max(0, i - 3);
        final end = min(frames.length, i + 4);
        for (int j = start; j < end; j++) {
          if (mags[j] > peakMag) { peakMag = mags[j]; peakIdx = j; }
        }
        impacts.add(ImpactEvent(msFromStart: frames[peakIdx].msFromStart, force: peakMag));
        lastImpactIdx = peakIdx;
      }
    }
    return impacts;
  }


}
