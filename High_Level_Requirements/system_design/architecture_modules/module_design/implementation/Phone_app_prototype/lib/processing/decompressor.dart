import 'dart:typed_data';
import '../models/sensor_frame.dart';

class Decompressor {
  final int formatVersion;
  Decompressor({this.formatVersion = 1});

  List<SensorFrame> decompress(Uint8List compressed) {
    final frames = <SensorFrame>[];
    if (compressed.length < 16) return frames;
    int offset = 16; // skip run file header
    double qW = 0, qX = 0, qY = 0, qZ = 0;
    double laX = 0, laY = 0, laZ = 0;
    int accMs = 0;

    while (offset + 4 <= compressed.length) {
      final deltaMs = compressed[offset] | (compressed[offset + 1] << 8);
      final baroRaw = compressed[offset + 2] | (compressed[offset + 3] << 8);
      final pktType = (deltaMs >> 14) & 0x03;
      offset += 4;

      if (pktType == 2) {
        // Type 3: absolute 16-bit (14 bytes)
        if (offset + 14 > compressed.length) break;
        qW = (compressed[offset] | (compressed[offset + 1] << 8)).toSigned(16).toDouble();
        qX = (compressed[offset + 2] | (compressed[offset + 3] << 8)).toSigned(16).toDouble();
        qY = (compressed[offset + 4] | (compressed[offset + 5] << 8)).toSigned(16).toDouble();
        qZ = (compressed[offset + 6] | (compressed[offset + 7] << 8)).toSigned(16).toDouble();
        laX = (compressed[offset + 8] | (compressed[offset + 9] << 8)).toSigned(16).toDouble();
        laY = (compressed[offset + 10] | (compressed[offset + 11] << 8)).toSigned(16).toDouble();
        laZ = (compressed[offset + 12] | (compressed[offset + 13] << 8)).toSigned(16).toDouble();
        offset += 14;
      } else if (pktType == 1) {
        // Type 2: 8-bit deltas (7 bytes)
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
        // Type 1: 4-bit deltas (4 bytes, 3.5 useful)
        if (offset + 4 > compressed.length) break;
        final b0 = compressed[offset], b1 = compressed[offset + 1];
        final b2 = compressed[offset + 2], b3 = compressed[offset + 3];
        qW += _signExtend4(b0 >> 4).toDouble();
        qX += _signExtend4(b0 & 0x0F).toDouble();
        qY += _signExtend4(b1 >> 4).toDouble();
        qZ += _signExtend4(b1 & 0x0F).toDouble();
        laX += _signExtend4(b2 >> 4).toDouble();
        laY += _signExtend4(b2 & 0x0F).toDouble();
        laZ += _signExtend4(b3 >> 4).toDouble();
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

  static int _signExtend4(int v) => (v & 0x08) != 0 ? (v | 0xFFFFFFF0) : v;

  /// Standard CRC-32 (Ethernet/IP) with polynomial 0xEDB88320.
  static int _crc32(List<int> data) {
    int crc = 0xFFFFFFFF;
    for (final byte in data) {
      crc ^= byte;
      for (int i = 0; i < 8; i++) {
        if ((crc & 1) != 0) {
          crc = (crc >> 1) ^ 0xEDB88320;
        } else {
          crc >>= 1;
        }
      }
    }
    return crc ^ 0xFFFFFFFF;
  }

  /// Compute b^e via exp(e * ln(b)).
  static double _pow(double b, double e) {
    if (b <= 0) return 0;
    final lnB = _ln(b);
    final x = e * lnB;
    return _expTaylor(x);
  }

  /// Natural logarithm via Newton's method.
  static double _ln(double x) {
    if (x <= 0) return double.negativeInfinity;
    double y = x - 1; // initial guess
    for (int i = 0; i < 10; i++) {
      final expY = _expTaylor(y);
      y = y + (x - expY) / expY;
    }
    return y;
  }

  /// exp(x) via Taylor series expansion.
  static double _expTaylor(double x) {
    double sum = 1.0, term = 1.0;
    for (int i = 1; i < 20; i++) {
      term *= x / i;
      sum += term;
    }
    return sum;
  }
}
