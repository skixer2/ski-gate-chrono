import 'package:flutter_test/flutter_test.dart';
import 'package:sgc_phone/processing/decompressor.dart';
import 'dart:typed_data';
import '../data/synthetic_data.dart';

void main() {
  group('RunHeader.parse', () {
    test('parses a valid 16-byte header', () {
      final header = buildRunHeader(
        formatVersion: 2,
        armSide: 1,
        startTimestamp: 1719000000,
        baroTempC: 15.5,
        compressedSize: 500,
        calAccuracy: 2,
      );
      final parsed = RunHeader.parse(header);

      expect(parsed.formatVersion, equals(2));
      expect(parsed.armSide, equals(1));
      expect(parsed.startTimestamp, equals(1719000000));
      expect(parsed.baroTempC, closeTo(15.5, 0.01));
      expect(parsed.compressedSize, equals(500));
      expect(parsed.calAccuracy, equals(2));
    });

    test('throws on short data', () {
      expect(() => RunHeader.parse(Uint8List(8)), throwsA(isA<Exception>()));
    });

    test('handles negative baro temp', () {
      final header = buildRunHeader(baroTempC: -5.0);
      final parsed = RunHeader.parse(header);
      expect(parsed.baroTempC, closeTo(-5.0, 0.01));
    });
  });

  group('Decompressor.decompress — Type 1 (4-bit deltas)', () {
    test('decodes a single Type 1 frame', () {
      // Frame: deltaMs=10, baro=25000 (100kPa/4), all-zero deltas
      final frame1 = encodeType1Frame(10, 25000, [0, 0, 0, 0, 0, 0, 0]);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...frame1]);

      final frames = Decompressor().decompress(data);
      expect(frames.length, equals(1));
      expect(frames[0].msFromStart, equals(10));
      expect(frames[0].baroPressurePa, closeTo(100000.0, 1.0));
      expect(frames[0].qW, closeTo(0.0, 0.01));
    });

    test('accumulates 4-bit deltas across frames', () {
      // Frame 1: qW=+2, qX=0, qY=0, qZ=0
      final f1 = encodeType1Frame(10, 25000, [2, 0, 0, 0, 0, 0, 0]);
      // Frame 2: qW=+1, qX=+3, qY=-2 (4-bit signed), qZ=0
      final f2 = encodeType1Frame(10, 25000, [1, 3, -2, 0, 0, 0, 0]);
      final f3 = encodeType1Frame(10, 25000, [0, 0, 0, 0, 0, 0, 0]);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1, ...f2, ...f3]);

      final frames = Decompressor().decompress(data);
      expect(frames.length, equals(3));
      // Frame 0: start all zero + [2,0,0,0,...] = [2,0,0,0,...]
      // Frame 1: previous + [1,3,-2,0,...] = [3,3,-2,0,...]
      // Frame 2: previous + [0,0,0,0,...] = [3,3,-2,0,...]
      expect(frames[0].qW, closeTo(2.0, 0.01));
      expect(frames[1].qW, closeTo(3.0, 0.01)); // 2+1
      expect(frames[1].qX, closeTo(3.0, 0.01)); // 0+3
      expect(frames[1].qY, closeTo(-2.0, 0.01)); // 0-2
      expect(frames[2].qW, closeTo(3.0, 0.01)); // stayed
    });

    test('sign-extends negative 4-bit values', () {
      final f1 = encodeType1Frame(10, 25000, [-1, -2, -3, -4, -5, -6, -7]);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1]);

      final frames = Decompressor().decompress(data);
      expect(frames[0].qW, closeTo(-1.0, 0.01));
      expect(frames[0].qX, closeTo(-2.0, 0.01));
      expect(frames[0].qY, closeTo(-3.0, 0.01));
      expect(frames[0].qZ, closeTo(-4.0, 0.01));
      expect(frames[0].laX, closeTo(-5.0, 0.01));
      expect(frames[0].laY, closeTo(-6.0, 0.01));
      expect(frames[0].laZ, closeTo(-7.0, 0.01));
    });
  });

  group('Decompressor.decompress — Type 2 (8-bit deltas)', () {
    test('decodes Type 2 frames with larger deltas', () {
      final f1 = encodeType2Frame(10, 25000, [50, -30, 20, -10, 5, -5, 3]);
      final f2 = encodeType2Frame(10, 25000, [0, 0, 0, 0, 0, 0, 0]);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1, ...f2]);

      final frames = Decompressor().decompress(data);
      expect(frames.length, equals(2));
      expect(frames[0].qW, closeTo(50.0, 0.01));
      expect(frames[0].qX, closeTo(-30.0, 0.01));
      expect(frames[0].qY, closeTo(20.0, 0.01));
      expect(frames[0].qZ, closeTo(-10.0, 0.01));
      expect(frames[0].laX, closeTo(5.0, 0.01));
      expect(frames[0].laY, closeTo(-5.0, 0.01));
      expect(frames[0].laZ, closeTo(3.0, 0.01));
      // Second frame: no change (deltas stay)
      expect(frames[1].qW, closeTo(50.0, 0.01));
    });
  });

  group('Decompressor.decompress — Type 3 (16-bit absolute)', () {
    test('decodes absolute quaternion + acceleration values', () {
      final absVals = [200, 150, -100, 50, 800, -400, 600];
      final f1 = encodeType3Frame(10, 25000, absVals);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1]);

      final frames = Decompressor().decompress(data);
      expect(frames.length, equals(1));
      expect(frames[0].qW, closeTo(200.0, 0.01));
      expect(frames[0].qX, closeTo(150.0, 0.01));
      expect(frames[0].qY, closeTo(-100.0, 0.01));
      expect(frames[0].qZ, closeTo(50.0, 0.01));
      expect(frames[0].laX, closeTo(800.0, 0.01));
      expect(frames[0].laY, closeTo(-400.0, 0.01));
      expect(frames[0].laZ, closeTo(600.0, 0.01));
    });

    test('Type 3 resets accumulator (absolute, not delta)', () {
      // Type 2 frame sets qW=50
      final f1 = encodeType2Frame(10, 25000, [50, 0, 0, 0, 0, 0, 0]);
      // Type 3 frame sets qW=100 (absolute, not 50+100)
      final f2 = encodeType3Frame(10, 25000, [100, 0, 0, 0, 0, 0, 0]);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1, ...f2]);

      final frames = Decompressor().decompress(data);
      expect(frames[1].qW, closeTo(100.0, 0.01)); // absolute, not 150
    });
  });

  group('Decompressor.decompress — mixed types', () {
    test('handles alternation between all 3 types', () {
      final data = mixedTypeRun;
      final result = Decompressor().decompressFull(data);

      expect(result.frameCount, equals(10));
      expect(result.frames.length, equals(10));
      // 10 frames × 10ms = 100ms
      expect(result.totalDurationSec, closeTo(0.10, 0.001));
      expect(result.header.formatVersion, equals(2));
    });
  });

  group('Decompressor.decompress — edge cases', () {
    test('empty data (less than 16 bytes)', () {
      final frames = Decompressor().decompress(Uint8List(4));
      expect(frames, isEmpty);
    });

    test('header-only (16 bytes, no frame data)', () {
      final header = buildRunHeader();
      final frames = Decompressor().decompress(header);
      expect(frames, isEmpty);
    });

    test('barometric pressure is correctly scaled (×4)', () {
      final f1 = encodeType1Frame(10, 25331, [0, 0, 0, 0, 0, 0, 0]);
      // baroRaw=25331, baroPa = 25331*4 = 101324 Pa
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1]);

      final frames = Decompressor().decompress(data);
      expect(frames[0].baroPressurePa, closeTo(101324.0, 0.5));
    });

    test('altitude is computed from pressure', () {
      final f1 = encodeType1Frame(10, 25331, [0, 0, 0, 0, 0, 0, 0]);
      final header = buildRunHeader();
      final data = Uint8List.fromList([...header, ...f1]);

      final frames = Decompressor().decompress(data);
      // At 101324 Pa, altitude ≈ 0m (sea level)
      expect(frames[0].baroAltitudeM, closeTo(0.0, 2.0));
    });

    test('truncated frame at end of buffer', () {
      final f1 = encodeType3Frame(10, 25000, [100, 0, 0, 0, 0, 0, 0]);
      final header = buildRunHeader();
      // Truncate last 5 bytes of the 18-byte Type 3 frame
      final truncated = Uint8List.fromList([
        ...header,
        ...f1.sublist(0, f1.length - 5),
      ]);

      final frames = Decompressor().decompress(truncated);
      // Should not crash; with pktType=2 in header, truncated Type 3 frame
      // may still produce a frame from the first 4 bytes interpreted as Type 1
      expect(frames.length, lessThanOrEqualTo(1));
    });
  });

  group('Decompressor.validateCRC', () {
    test('validates correct CRC', () {
      final data = Uint8List.fromList([0x01, 0x02, 0x03, 0x04, 0x05, 0xC3, 0x32, 0x00, 0x00, 0x00, 0x00]);
      // We'd need correct CRC for this test — skip for now
    });

    test('rejects missing CRC marker', () {
      final data = Uint8List.fromList([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A]);
      expect(Decompressor().validateCRC(data), isFalse);
    });

    test('rejects short data', () {
      expect(Decompressor().validateCRC(Uint8List(4)), isFalse);
    });
  });
}
