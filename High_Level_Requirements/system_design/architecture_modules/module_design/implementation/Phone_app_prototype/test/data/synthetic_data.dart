/// Synthetic test data generators for SGC phone app tests.
///
/// All generators are deterministic (fixed seeds). This module is the
/// single source of truth for phone test data — the companion Python
/// script `scripts/generate_phone_test_data.py` produces identical
/// binary blobs for cross-validation.
library;

import 'dart:typed_data';
import 'dart:math';

// ═══════════════════════════════════════════════════════════════════
// Decompressor test data
// ═══════════════════════════════════════════════════════════════════

/// Build a 16-byte run header for testing.
Uint8List buildRunHeader({
  int formatVersion = 2,
  int armSide = 0,
  int startTimestamp = 1719000000,
  double baroTempC = 15.5,
  int compressedSize = 0,
  int calAccuracy = 0,
}) {
  final buf = ByteData(16);
  buf.setUint8(0, formatVersion);
  buf.setUint8(1, armSide);
  buf.setUint32(2, startTimestamp, Endian.little);
  buf.setInt16(6, (baroTempC * 10).round(), Endian.little);
  buf.setUint32(8, compressedSize, Endian.little);
  buf.setUint8(12, calAccuracy);
  return buf.buffer.asUint8List();
}

/// Encode a single frame as a Type 1 (4-bit deltas, 4-byte payload).
Uint8List encodeType1Frame(int deltaMs, int baroPaDiv4, List<int> deltas7) {
  final buf = ByteData(10);
  final pktType = 0; // Type 1
  final word0 = (pktType << 14) | (deltaMs & 0x03FF);
  buf.setUint16(0, word0, Endian.little);
  buf.setUint16(2, baroPaDiv4 & 0xFFFF, Endian.little);

  // Pack 7 × 4-bit deltas into 4 bytes (last nibble unused)
  buf.setUint8(4, ((deltas7[0] & 0x0F) << 4) | (deltas7[1] & 0x0F));
  buf.setUint8(5, ((deltas7[2] & 0x0F) << 4) | (deltas7[3] & 0x0F));
  buf.setUint8(6, ((deltas7[4] & 0x0F) << 4) | (deltas7[5] & 0x0F));
  buf.setUint8(7, ((deltas7[6] & 0x0F) << 4)); // last nibble pad
  buf.setUint16(8, 0, Endian.little);
  return buf.buffer.asUint8List().sublist(0, 8); // 8 bytes: 4 header + 4 payload
}

/// Encode a single frame as Type 2 (8-bit deltas, 7-byte payload).
Uint8List encodeType2Frame(int deltaMs, int baroPaDiv4, List<int> deltas7) {
  final buf = ByteData(11);
  final pktType = 1;
  final word0 = (pktType << 14) | (deltaMs & 0x03FF);
  buf.setUint16(0, word0, Endian.little);
  buf.setUint16(2, baroPaDiv4 & 0xFFFF, Endian.little);
  for (int i = 0; i < 7; i++) {
    buf.setInt8(4 + i, deltas7[i]);
  }
  return buf.buffer.asUint8List().sublist(0, 11); // 4 header + 7 payload
}

/// Encode a single frame as Type 3 (absolute 16-bit values, 14-byte payload).
Uint8List encodeType3Frame(int deltaMs, int baroPaDiv4, List<int> abs7) {
  final buf = ByteData(18);
  final pktType = 2; // Type 3 = absolute (decompressor checks pktType == 2)
  final word0 = (pktType << 14) | (deltaMs & 0x03FF);
  buf.setUint16(0, word0, Endian.little);
  buf.setUint16(2, baroPaDiv4 & 0xFFFF, Endian.little);
  for (int i = 0; i < 7; i++) {
    buf.setInt16(4 + i * 2, abs7[i], Endian.little);
  }
  return buf.buffer.asUint8List().sublist(0, 18); // 4 header + 14 payload
}

/// Build a complete compressed run blob: header + N frames.
Uint8List buildCompressedRun({
  required List<int> frameTypes,       // 1, 2, or 3 for each frame
  required List<int> deltaMsList,
  required List<int> baroPaDiv4List,
  required List<List<int>> payloadList, // 7 values per frame
}) {
  final header = buildRunHeader(
    compressedSize: frameTypes.length * 11, // rough
  );
  final builder = BytesBuilder();
  builder.add(header);
  for (int i = 0; i < frameTypes.length; i++) {
    switch (frameTypes[i]) {
      case 2:
        builder.add(encodeType2Frame(deltaMsList[i], baroPaDiv4List[i], payloadList[i]));
        break;
      case 3:
        builder.add(encodeType3Frame(deltaMsList[i], baroPaDiv4List[i], payloadList[i]));
        break;
      default:
        builder.add(encodeType1Frame(deltaMsList[i], baroPaDiv4List[i], payloadList[i]));
        break;
    }
  }
  return builder.toBytes();
}

// ═══════════════════════════════════════════════════════════════════
// Pre-built test fixtures
// ═══════════════════════════════════════════════════════════════════

/// 10-frame run with all 3 packet types mixed.
Uint8List get mixedTypeRun {
  final types = [1, 1, 1, 2, 2, 2, 3, 1, 2, 1];
  final deltas = List.generate(10, (_) => 10); // 10ms each
  final baros = List.generate(10, (_) => 25000); // ~100000 Pa / 4
  final payloads = <List<int>>[
    [0, 0, 0, 0, 0, 0, 0],
    [1, 0, -1, 0, 0, 0, 0],  // small drift
    [0, 1, 0, -1, 0, 0, 0],
    [0, 0, 0, 0, 2, -2, 1],
    [-1, 1, -1, 1, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0],
    [100, 50, -30, 20, 500, -200, 300], // Type 3 absolute
    [0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0],
  ];
  return buildCompressedRun(
    frameTypes: types, deltaMsList: deltas,
    baroPaDiv4List: baros, payloadList: payloads,
  );
}

/// 100-frame run: steady descent for start detection (pressure dropping).
Uint8List get steadyDescentRun {
  final n = 100;
  final types = List.filled(n, 2);
  final deltas = List.filled(n, 10);
  // 100000 Pa → 98000 Pa over 100 frames (~200 Pa drop = ~16.7m)
  final baros = List.generate(n, (i) => 25000 - (i * 2));
  final payloads = List.generate(n, (_) => [0, 0, 0, 0, 0, 0, 0]);
  return buildCompressedRun(
    frameTypes: types, deltaMsList: deltas,
    baroPaDiv4List: baros, payloadList: payloads,
  );
}

// ═══════════════════════════════════════════════════════════════════
// ImpactDetector test data
// ═══════════════════════════════════════════════════════════════════

/// Generate frames with a known impact spike at frame [impactFrame].
List<Map<String, dynamic>> buildImpactFrames({
  int totalFrames = 200,
  int impactFrame = 150,
  double impactForceG = 5.0,
  double baselineG = 1.0,
}) {
  final frames = <Map<String, dynamic>>[];
  for (int i = 0; i < totalFrames; i++) {
    final d = i - impactFrame;
    // Gaussian-shaped impact spike
    final spike = impactForceG * exp(-d * d / 4.0);
    final noise = (sin(i * 0.7) * 0.2) + baselineG;
    final magG = noise + spike;
    // Distribute magnitude across axes
    final la = magG * 9.81; // convert G to m/s²
    frames.add({
      'msFromStart': i * 10,
      'laX': la * 0.5, 'laY': la * 0.7, 'laZ': la * 0.3,
    });
  }
  return frames;
}

/// Multiple impacts (slalom: roughly every 1s = 100 frames).
List<Map<String, dynamic>> get slalomImpactFrames {
  final frames = <Map<String, dynamic>>[];
  const total = 600; // 6 seconds
  const impacts = [50, 140, 230, 320, 410, 500]; // ~0.9s spacing
  for (int i = 0; i < total; i++) {
    double spike = 0;
    for (final imp in impacts) {
      spike += 3.5 * exp(-(i - imp) * (i - imp) / 4.0);
    }
    final mag = 1.0 + spike + sin(i * 0.3) * 0.15;
    frames.add({
      'msFromStart': i * 10,
      'laX': mag * 0.5 * 9.81,
      'laY': mag * 0.7 * 9.81,
      'laZ': mag * 0.3 * 9.81,
    });
  }
  return frames;
}

// ═══════════════════════════════════════════════════════════════════
// CrossCorrelator test data
// ═══════════════════════════════════════════════════════════════════

/// Generate left/right frame pairs with known time offset.
/// Right arm is shifted by [offsetMs] relative to left.
({List<Map<String, dynamic>> left, List<Map<String, dynamic>> right})
buildCorrelatedFrames({
  int totalFrames = 500,
  int offsetMs = 150, // right arm 150ms late
  double noiseScale = 0.02,
}) {
  final rng = Random(42);
  final offsetFrames = offsetMs ~/ 10;

  List<Map<String, dynamic>> build(bool isRight) {
    final frames = <Map<String, dynamic>>[];
    for (int i = 0; i < totalFrames; i++) {
      // Rotation magnitude ∼ sin(2π · i / 150) for turns every ~1.5s
      final t = (i - (isRight ? offsetFrames : 0)) / 150.0;
      final omega = sin(2 * pi * t);
      // Distribute into quaternion components
      final mag = 0.7 + omega * 0.3;
      final qw = cos(mag / 2);
      final axis = sin(mag / 2);
      final qx = axis * 0.8 + rng.nextDouble() * noiseScale;
      final qy = axis * 0.3 + rng.nextDouble() * noiseScale;
      final qz = axis * 0.5 + rng.nextDouble() * noiseScale;
      frames.add({
        'msFromStart': i * 10,
        'qW': qw, 'qX': qx, 'qY': qy, 'qZ': qz,
        'laX': 0.0, 'laY': 0.0, 'laZ': 0.0,
      });
    }
    return frames;
  }

  return (left: build(false), right: build(true));
}

// ═══════════════════════════════════════════════════════════════════
// GateTimeEstimator test data
// ═══════════════════════════════════════════════════════════════════

/// Generate frames for a full slalom run with known gate positions.
///
/// Each gate cycle: [turn → straight_zone → next turn].
/// The straight zone (~40 frames at gate center) produces omega ≈ 0
/// which the zero-crossing finder detects as turn boundaries.
/// The 40-frame width is needed because the low-pass filter (alpha=0.1)
/// takes time to decay from turn-rate (~4 rad/s) to below 0.3 rad/s.
List<Map<String, dynamic>> buildSlalomRunFrames({
  int numGates = 10,
  int framesPerGate = 100, // ~1s between gates
}) {
  final frames = <Map<String, dynamic>>[];
  final total = numGates * framesPerGate;
  final rng = Random(123);

  // Pressure starts at 101325 Pa, drops ~2 Pa/frame
  double pressure = 101325.0;

  for (int i = 0; i < total; i++) {
    final gateIdx = i ~/ framesPerGate;
    final posInGate = i % framesPerGate;
    final halfGate = framesPerGate ~/ 2;
    const straightHalf = 20; // 40-frame straight zone (20 each side of center)

    // Alternating turns
    final side = gateIdx.isEven ? 1.0 : -1.0;

    // Straight zone at gate center (40 frames): no rotation
    // Turn zone before: 0 → pi/2 over 30 frames
    // Turn zone after: pi/2 → pi over 30 frames
    double angle;
    if (posInGate < halfGate - straightHalf) {
      // First half-turn: 0 → pi/2
      final progress = posInGate / (halfGate - straightHalf);
      angle = side * progress * (pi / 2);
    } else if (posInGate > halfGate + straightHalf) {
      // Second half-turn: pi/2 → pi
      final progress = (posInGate - halfGate - straightHalf) / (halfGate - straightHalf);
      angle = side * ((pi / 2) + progress * (pi / 2));
    } else {
      // Straight zone (40 frames): constant angle = pi/2
      angle = side * (pi / 2);
    }

    final qw = cos(angle / 2);
    final qx = 0.0;
    final qy = sin(angle / 2);  // unit quaternion (normalized)
    final qz = 0.0;

    // Impact spike at exact gate center
    final distFromGate = (posInGate - halfGate).abs();
    final impact = (distFromGate < 3) ? 4.0 * exp(-distFromGate * distFromGate / 2.0) : 0.0;

    final la = 1.0 + impact + rng.nextDouble() * 0.2;
    final laX = la * 0.4 * side * 9.81;
    final laY = la * 0.8 * 9.81;
    final laZ = la * 0.3 * 9.81;

    // Descending pressure
    pressure -= 2.0 + rng.nextDouble() * 0.5;

    frames.add({
      'msFromStart': i * 10,
      'qW': qw, 'qX': qx, 'qY': qy, 'qZ': qz,
      'laX': laX, 'laY': laY, 'laZ': laZ,
      'baroPressurePa': pressure,
    });
  }
  return frames;
}

// ═══════════════════════════════════════════════════════════════════
// Vec3 test data
// ═══════════════════════════════════════════════════════════════════

const vec3TestCases = [
  {'a': [1.0, 2.0, 3.0], 'b': [4.0, 5.0, 6.0], 'sum': [5.0, 7.0, 9.0], 'diff': [-3.0, -3.0, -3.0], 'dot': 32.0},
  {'a': [0.0, 0.0, 0.0], 'b': [1.0, 2.0, 3.0], 'sum': [1.0, 2.0, 3.0], 'diff': [-1.0, -2.0, -3.0], 'dot': 0.0},
  {'a': [1.0, 0.0, 0.0], 'b': [0.0, 1.0, 0.0], 'sum': [1.0, 1.0, 0.0], 'diff': [1.0, -1.0, 0.0], 'dot': 0.0},
];
