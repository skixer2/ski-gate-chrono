import 'package:flutter_test/flutter_test.dart';
import 'dart:math';
import 'package:sgc_phone/models/sensor_frame.dart';
import 'package:sgc_phone/processing/impact_detector.dart';
import '../data/synthetic_data.dart';

/// Helper: convert test data maps to SensorFrame list.
List<SensorFrame> toFrames(List<Map<String, dynamic>> data) {
  return data.map((m) => SensorFrame(
    msFromStart: m['msFromStart'] as int,
    qW: (m['qW'] as double?) ?? 0,
    qX: (m['qX'] as double?) ?? 0,
    qY: (m['qY'] as double?) ?? 0,
    qZ: (m['qZ'] as double?) ?? 0,
    laX: (m['laX'] as num).toDouble(),
    laY: (m['laY'] as num).toDouble(),
    laZ: (m['laZ'] as num).toDouble(),
    baroPressurePa: (m['baroPressurePa'] as double?) ?? 101325,
    baroAltitudeM: (m['baroAltitudeM'] as double?) ?? 0,
  )).toList();
}

void main() {
  group('ImpactDetector', () {
    test('detects single impact spike', () {
      final data = buildImpactFrames(
        totalFrames: 200,
        impactFrame: 150,
        impactForceG: 5.0,
        baselineG: 1.0,
      );
      final frames = toFrames(data);
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 20);
      final impacts = detector.detect(frames);

      expect(impacts.length, greaterThanOrEqualTo(1));
      // Impact should be near frame 150 (1500ms)
      final imp = impacts.first;
      expect(imp.msFromStart, greaterThan(1400));
      expect(imp.msFromStart, lessThan(1600));
      expect(imp.force, greaterThan(3.0));
    });

    test('detects multiple slalom impacts', () {
      final data = slalomImpactFrames;
      final frames = toFrames(data);
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 20);
      final impacts = detector.detect(frames);

      // 6 gates expected at frames 50, 140, 230, 320, 410, 500
      expect(impacts.length, equals(6));

      // All detections should be near their target frames
      const targets = [500, 1400, 2300, 3200, 4100, 5000];
      for (int i = 0; i < impacts.length; i++) {
        expect(impacts[i].msFromStart,
            closeTo(targets[i], 200)); // ±200ms tolerance
      }
    });

    test('cooldown prevents duplicate detections', () {
      // Single impact with long tail
      final data = buildImpactFrames(
        totalFrames: 300,
        impactFrame: 100,
        impactForceG: 6.0,
        baselineG: 1.0,
      );
      final frames = toFrames(data);
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 20);
      final impacts = detector.detect(frames);

      // Should detect only one impact despite the tail
      expect(impacts.length, equals(1));
    });

    test('ignores below-threshold signals', () {
      // All baseline, no spikes — all below multiplier threshold
      final data = List.generate(100, (i) => {
        'msFromStart': i * 10,
        'laX': 5.0, 'laY': 7.0, 'laZ': 3.0, // ~9.1 m/s² ≈ 0.93G
      });
      // Fill remaining fields (needed by toFrames → SensorFrame constructor)
      final fullData = data.map((m) => {
        ...m,
        'qW': 0.7, 'qX': 0.0, 'qY': 0.0, 'qZ': 0.0,
        'baroPressurePa': 101325.0,
      }).toList();
      final frames = toFrames(fullData);
      final detector = ImpactDetector(multiplier: 3.0, baselineWindow: 20);
      final impacts = detector.detect(frames);

      expect(impacts, isEmpty);
    });

    test('empty frames returns empty', () {
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 20);
      expect(detector.detect([]), isEmpty);
    });

    test('fewer frames than baselineWindow returns empty', () {
      final data = List.generate(10, (i) => {
        'msFromStart': i * 10,
        'qW': 0.7, 'qX': 0.0, 'qY': 0.0, 'qZ': 0.0,
        'laX': 10.0, 'laY': 0.0, 'laZ': 0.0,
        'baroPressurePa': 101325.0,
      });
      final frames = toFrames(data);
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 20);
      expect(detector.detect(frames), isEmpty);
    });

    test('child multiplier (1.5) detects more impacts', () {
      final data = slalomImpactFrames;
      final frames = toFrames(data);
      final detectorAdult = ImpactDetector(multiplier: 3.0, baselineWindow: 20);
      final detectorChild = ImpactDetector(multiplier: 1.5, baselineWindow: 20);

      final adultImpacts = detectorAdult.detect(frames);
      final childImpacts = detectorChild.detect(frames);

      // Child multiplier is more sensitive
      expect(childImpacts.length, greaterThanOrEqualTo(adultImpacts.length));
    });

    test('impactForce is in G units', () {
      final data = buildImpactFrames(
        totalFrames: 200,
        impactFrame: 100,
        impactForceG: 4.0,
        baselineG: 1.0,
      );
      final frames = toFrames(data);
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 20);
      final impacts = detector.detect(frames);

      // Force should be ~4G (≈39.2 m/s² normalized to G)
      expect(impacts.first.force, greaterThan(2.5));
      expect(impacts.first.force, lessThan(6.0));
    });
  });
}
