import 'package:flutter_test/flutter_test.dart';
import 'package:sgc_phone/models/sensor_frame.dart';
import 'package:sgc_phone/models/gate_timestamp.dart';
import 'package:sgc_phone/models/gate_side.dart';
import 'package:sgc_phone/models/run.dart';
import 'package:sgc_phone/models/barometric_point.dart';

void main() {
  group('SensorFrame', () {
    test('constructs with all fields', () {
      final f = SensorFrame(
        msFromStart: 100,
        qW: 0.7, qX: 0.1, qY: 0.2, qZ: 0.3,
        laX: 1.5, laY: 2.0, laZ: 3.0,
        baroPressurePa: 101325.0,
        baroAltitudeM: 150.0,
        verticalSpeedMs: 5.0,
      );
      expect(f.msFromStart, equals(100));
      expect(f.baroAltitudeM, closeTo(150.0, 0.01));
      expect(f.verticalSpeedMs, closeTo(5.0, 0.01));
    });

    test('defaults altitude and vertical speed to 0', () {
      final f = SensorFrame(
        msFromStart: 0,
        qW: 1, qX: 0, qY: 0, qZ: 0,
        laX: 0, laY: 0, laZ: 0,
        baroPressurePa: 101325.0,
      );
      expect(f.baroAltitudeM, closeTo(0, 0.01));
      expect(f.verticalSpeedMs, closeTo(0, 0.01));
    });
  });

  group('GateTimestamp', () {
    test('constructs with all fields optional', () {
      final gt = GateTimestamp(
        gateNumber: 1,
        side: GateSide.rightGate,
        msFromStart: 1500,
        isEstimated: false,
        impactForce: 3.5,
      );
      expect(gt.gateNumber, equals(1));
      expect(gt.isEstimated, isFalse);
      expect(gt.impactForce, closeTo(3.5, 0.01));
      expect(gt.rfidTagId, isNull);
    });

    test('estimated gate has no impact force', () {
      final gt = GateTimestamp(
        gateNumber: 2,
        side: GateSide.leftGate,
        msFromStart: 2500,
        isEstimated: true,
        impactForce: null,
      );
      expect(gt.isEstimated, isTrue);
      expect(gt.impactForce, isNull);
    });
  });

  group('Run', () {
    test('constructs with required fields', () {
      final run = Run(
        id: 1,
        deviceRunId: 42,
        armSide: ArmSide.left,
        deviceId: 'AA:BB:CC:DD:EE:FF',
        startTimeUnix: 1719000000,
        frameCount: 500,
        compressedSizeBytes: 4096,
        formatVersion: 2,
      );
      expect(run.id, equals(1));
      expect(run.armSide, equals(ArmSide.left));
      expect(run.frameCount, equals(500));
      expect(run.duration, equals(Duration.zero));
    });

    test('duration is computed from last frame', () {
      final run = Run(
        id: 1,
        deviceRunId: 1,
        armSide: ArmSide.right,
        deviceId: '00:00:00:00:00:00',
        startTimeUnix: 0,
        frameCount: 10,
        compressedSizeBytes: 100,
        formatVersion: 2,
        frames: [
          SensorFrame(
            msFromStart: 0, qW: 1, qX: 0, qY: 0, qZ: 0,
            laX: 0, laY: 0, laZ: 0, baroPressurePa: 101325.0,
          ),
          SensorFrame(
            msFromStart: 500, qW: 1, qX: 0, qY: 0, qZ: 0,
            laX: 0, laY: 0, laZ: 0, baroPressurePa: 101325.0,
          ),
        ],
      );
      expect(run.duration, equals(Duration(milliseconds: 500)));
    });

    test('default values for optional fields', () {
      final run = Run(
        id: 1, deviceRunId: 1,
        armSide: ArmSide.left,
        deviceId: 'x', startTimeUnix: 0,
        frameCount: 0, compressedSizeBytes: 0,
        formatVersion: 2,
      );
      expect(run.isUploaded, isFalse);
      expect(run.frames, isEmpty);
      expect(run.gateTimestamps, isEmpty);
      expect(run.barometricData, isEmpty);
    });
  });

  group('CombinedRun', () {
    test('holds both runs and alignment data', () {
      final left = Run(
        id: 1, deviceRunId: 1,
        armSide: ArmSide.left, deviceId: 'a',
        startTimeUnix: 0, frameCount: 100,
        compressedSizeBytes: 1000, formatVersion: 2,
      );
      final right = Run(
        id: 2, deviceRunId: 2,
        armSide: ArmSide.right, deviceId: 'b',
        startTimeUnix: 0, frameCount: 100,
        compressedSizeBytes: 1000, formatVersion: 2,
      );
      final combined = CombinedRun(
        leftRun: left,
        rightRun: right,
        timeOffsetMs: 120,
        mergedGates: [],
        combinedBaroData: [],
      );
      expect(combined.timeOffsetMs, equals(120));
      expect(combined.leftRun.armSide, equals(ArmSide.left));
      expect(combined.rightRun.armSide, equals(ArmSide.right));
    });
  });

  group('MergedGate', () {
    test('constructs with all required fields', () {
      final mg = MergedGate(
        gateNumber: 5,
        side: GateSide.rightGate,
        detectingArm: 'right',
        isEstimated: false,
        timeFromStart: Duration(milliseconds: 3500),
        impactForce: 4.2,
      );
      expect(mg.gateNumber, equals(5));
      expect(mg.detectingArm, equals('right'));
      expect(mg.timeFromStart.inMilliseconds, equals(3500));
      expect(mg.isBanana, isFalse);
    });
  });

  group('BarometricPoint', () {
    test('constructs with all fields', () {
      final bp = BarometricPoint(
        msFromStart: 500,
        altitudeM: 1234.5,
        verticalSpeedMs: -2.5,
      );
      expect(bp.altitudeM, closeTo(1234.5, 0.01));
      expect(bp.verticalSpeedMs, closeTo(-2.5, 0.01));
    });
  });

  group('ArmSide', () {
    test('label returns human-readable', () {
      expect(ArmSide.left.label, equals('Left'));
      expect(ArmSide.right.label, equals('Right'));
    });
  });

  group('Discipline', () {
    test('label returns full name', () {
      expect(Discipline.sl.label, equals('Slalom'));
      expect(Discipline.gs.label, equals('Giant Slalom'));
      expect(Discipline.sg.label, equals('Super-G'));
      expect(Discipline.dh.label, equals('Downhill'));
    });
  });
}
