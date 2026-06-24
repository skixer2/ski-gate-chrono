import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import '../models/sensor_frame.dart';

/// Parsed run header from the first 16 bytes of a compressed run file.
class RunHeader {
  final int formatVersion;
  final int armSide;       // 0=left, 1=right
  final int startTimestamp; // UTC unixtime
  final double baroTempC;   // from baro_temp (tenths °C)
  final int compressedSize; // bytes of compressed frame data (excl. header)
  final int calAccuracy;   // 0-3

  const RunHeader({
    required this.formatVersion,
    required this.armSide,
    required this.startTimestamp,
    required this.baroTempC,
    required this.compressedSize,
    required this.calAccuracy,
  });

  static RunHeader parse(Uint8List data) {
    if (data.length < 16) throw Exception('Run file too short for header');
    return RunHeader(
      formatVersion: data[0],
      armSide: data[1],
      startTimestamp: data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24),
      baroTempC: ((data[6] | (data[7] << 8)).toSigned(16)) / 10.0,
      compressedSize: data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24),
      calAccuracy: data[12],
    );
  }
}

/// Result of decompressing a run file.
class DecompressResult {
  final RunHeader header;
  final List<SensorFrame> frames;
  final int frameCount;
  final double totalDurationSec;

  const DecompressResult({
    required this.header,
    required this.frames,
    required this.frameCount,
    required this.totalDurationSec,
  });
}

class Decompressor {
  final int formatVersion;
  Decompressor({this.formatVersion = 2});

  /// Parse header and decompress in one call.
  DecompressResult decompressFull(Uint8List compressed) {
    final header = RunHeader.parse(compressed);
    final frames = decompress(compressed);
    final totalMs = frames.isNotEmpty ? frames.last.msFromStart.toDouble() : 0.0;
    return DecompressResult(
      header: header,
      frames: frames,
      frameCount: frames.length,
      totalDurationSec: totalMs / 1000.0,
    );
  }

  /// Decompress frame data starting at offset 16 (after header).
  List<SensorFrame> decompress(Uint8List compressed) {
    final frames = <SensorFrame>[];
    if (compressed.length < 16) return frames;

    int offset = 16; // skip 16-byte run header
    double qW = 0, qX = 0, qY = 0, qZ = 0;
    double laX = 0, laY = 0, laZ = 0;
    int accMs = 0;

    while (offset + 4 <= compressed.length) {
      final deltaMs = compressed[offset] | (compressed[offset + 1] << 8);
      final baroRaw = compressed[offset + 2] | (compressed[offset + 3] << 8);
      final pktType = (deltaMs >> 14) & 0x03;
      offset += 4;

      if (pktType == 2) {
        // Type 3: absolute 16-bit values (14 bytes)
        if (offset + 14 > compressed.length) break;
        qW = _i16(compressed, offset).toDouble();
        qX = _i16(compressed, offset + 2).toDouble();
        qY = _i16(compressed, offset + 4).toDouble();
        qZ = _i16(compressed, offset + 6).toDouble();
        laX = _i16(compressed, offset + 8).toDouble();
        laY = _i16(compressed, offset + 10).toDouble();
        laZ = _i16(compressed, offset + 12).toDouble();
        offset += 14;
      } else if (pktType == 1) {
        // Type 2: 8-bit signed deltas (7 bytes)
        if (offset + 7 > compressed.length) break;
        qW += compressed[offset].toSigned(8).toDouble();
        qX += compressed[offset + 1].toSigned(8).toDouble();
        qY += compressed[offset + 2].toSigned(8).toDouble();
        qZ += compressed[offset + 3].toSigned(8).toDouble();
        laX += compressed[offset + 4].toSigned(8).toDouble();
        laY += compressed[offset + 5].toSigned(8).toDouble();
        laZ += compressed[offset + 6].toSigned(8).toDouble();
        offset += 7;
      } else {
        // Type 1: 4-bit signed deltas (4 bytes, 3.5 used)
        if (offset + 4 > compressed.length) break;
        final b0 = compressed[offset], b1 = compressed[offset + 1];
        final b2 = compressed[offset + 2], b3 = compressed[offset + 3];
        qW += _signExt4(b0 >> 4).toDouble();
        qX += _signExt4(b0 & 0x0F).toDouble();
        qY += _signExt4(b1 >> 4).toDouble();
        qZ += _signExt4(b1 & 0x0F).toDouble();
        laX += _signExt4(b2 >> 4).toDouble();
        laY += _signExt4(b2 & 0x0F).toDouble();
        laZ += _signExt4(b3 >> 4).toDouble();
        offset += 4;
      }

      accMs += (deltaMs & 0x03FF);
      final baroPa = (baroRaw & 0xFFFF) * 4.0;

      frames.add(SensorFrame(
        msFromStart: accMs,
        qW: qW, qX: qX, qY: qY, qZ: qZ,
        laX: laX, laY: laY, laZ: laZ,
        baroPressurePa: baroPa,
        baroAltitudeM: 44330 * (1 - _pow(baroPa / 101325, 0.1903)),
      ));
    }
    return frames;
  }

  /// Validate CRC at end of data buffer (trailing 6 bytes: magic + CRC32).
  /// Note: BLE file transfer protocol uses separate CRC characteristic (ABCC).
  /// This method is for offline/legacy file validation.
  bool validateCRC(Uint8List data) {
    final len = data.length;
    if (len < 6) return false;
    if (data[len - 6] != 0xC3 || data[len - 5] != 0x32) return false;
    final storedCrc = (data[len - 4]) |
        (data[len - 3] << 8) |
        (data[len - 2] << 16) |
        (data[len - 1] << 24);
    final computedCrc = _crc32(data.sublist(0, len - 6));
    return storedCrc == computedCrc;
  }

  // ── Helpers ─────────────────────────────────────────────────────

  static int _signExt4(int v) => (v & 0x08) != 0 ? (v - 16) : (v & 0x0F);

  static int _i16(Uint8List buf, int off) =>
      (buf[off] | (buf[off + 1] << 8)).toSigned(16);

  static int _crc32(List<int> data) {
    int crc = 0xFFFFFFFF;
    for (final byte in data) {
      crc ^= byte;
      for (int i = 0; i < 8; i++) {
        crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
      }
    }
    return crc ^ 0xFFFFFFFF;
  }

  static double _pow(double b, double e) {
    if (b <= 0) return 0;
    final lnB = _ln(b);
    return _expTaylor(e * lnB);
  }

  static double _ln(double x) {
    if (x <= 0) return double.negativeInfinity;
    double y = x - 1;
    for (int i = 0; i < 10; i++) {
      final expY = _expTaylor(y);
      y = y + (x - expY) / expY;
    }
    return y;
  }

  static double _expTaylor(double x) {
    double sum = 1.0, term = 1.0;
    for (int i = 1; i < 20; i++) {
      term *= x / i;
      sum += term;
    }
    return sum;
  }
}
