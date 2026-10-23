import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'ble_manager.dart';
import '../models/device_config.dart';
import '../models/gate_side.dart';

String _shortUuid(String uuid) =>
    uuid.substring(uuid.length - 12, uuid.length - 4);

class SGCService {
  final BLEManager ble;


  // V1.21: read services LIVE from the manager so this instance self-heals
  // after a reconnect (services are rediscovered per connection).
  List<BluetoothService> get _services => ble.services ?? [];

  SGCService(this.ble);

  static const serviceUuid = '53470000-0000-1000-8000-00805F9B34FB';
  static const charCurrentTime = '5347ABC0-0000-1000-8000-00805F9B34FB';
  static const charDeviceName  = '5347ABC1-0000-1000-8000-00805F9B34FB';
  static const charArmSide     = '5347ABC2-0000-1000-8000-00805F9B34FB';
  static const charDiscipline  = '5347ABC3-0000-1000-8000-00805F9B34FB';
  static const charDeviceState = '5347ABC4-0000-1000-8000-00805F9B34FB';
  static const charBattery     = '5347ABC5-0000-1000-8000-00805F9B34FB';
  static const charFlashUsed   = '5347ABC7-0000-1000-8000-00805F9B34FB';
  static const charRunInfo     = '5347ABC8-0000-1000-8000-00805F9B34FB'; // count[2]+age[4]
  static const charRunList     = '5347ABC9-0000-1000-8000-00805F9B34FB';
  static const charFtRequest   = '5347ABCA-0000-1000-8000-00805F9B34FB';
  static const charFtStream     = '5347ABCD-0000-1000-8000-00805F9B34FB';
  static const charCalAccuracy = '5347ABD0-0000-1000-8000-00805F9B34FB';

  static String _toFullUuid(String uuid) {
    uuid = uuid.toUpperCase().replaceAll('-', '');
    if (uuid.length <= 8) return '${uuid.padLeft(8, '0')}-0000-1000-8000-00805F9B34FB';
    return '${uuid.substring(0,8)}-${uuid.substring(8,12)}-${uuid.substring(12,16)}-${uuid.substring(16,20)}-${uuid.substring(20)}';
  }

  void debugLogServices() {
    debugPrint('[SGC] Device services (${_services.length}):');
    for (int i = 0; i < _services.length; i++) {
      final s = _services[i];
      debugPrint('[SGC]   Service ${i}: ${_toFullUuid(s.serviceUuid.toString())}');
      for (int j = 0; j < s.characteristics.length; j++) {
        final c = s.characteristics[j];
        debugPrint('[SGC]     Char ${j}: ${_toFullUuid(c.characteristicUuid.toString())} props=${c.properties}');
      }
    }
  }

  BluetoothCharacteristic? _findChar(String uuid) {
    final target = _toFullUuid(uuid);
    for (final s in _services)
      for (final c in s.characteristics)
        if (_toFullUuid(c.characteristicUuid.toString()) == target) return c;
    return null;
  }

  /// v1.50 (audit A1/A2): reads and writes THROW on failure — the old
  /// silent Uint8List(0)/no-op pattern fabricated defaults downstream
  /// and masked bugs for days ("0 on device", fake config card).
  Future<Uint8List> _readChar(String uuid) async {
    final c = _findChar(uuid);
    if (c == null) {
      throw CharAccessException(_shortUuid(uuid), 'characteristic not found (FW too old or service mismatch)');
    }
    try {
      return Uint8List.fromList(await c.read());
    } catch (e) {
      throw CharAccessException(_shortUuid(uuid), 'read failed: $e');
    }
  }

  Future<void> _writeChar(String uuid, Uint8List data) async {
    final c = _findChar(uuid);
    if (c == null) {
      throw CharAccessException(_shortUuid(uuid), 'characteristic not found — write DROPPED');
    }
    try {
      await c.write(data, withoutResponse: false);
    } catch (e) {
      throw CharAccessException(_shortUuid(uuid), 'write failed: $e');
    }
  }

  /// Single-byte read that validates length (audit A4).
  Future<int> _readByte(String uuid) async {
    final b = await _readChar(uuid);
    if (b.isEmpty) {
      throw CharAccessException(_shortUuid(uuid), 'returned 0 bytes');
    }
    return b[0];
  }

  // ── Time ─────────────────────────────────────────────────────
  /// v1.50 (audit A8/B2): returns null when the sync is VERIFIED via
  /// ABC0 readback (FW ≥ 0.18.0), else a human-readable reason. Never
  /// throws — a failed sync must not kill the connect flow, but it
  /// MUST be surfaced (was: fully silent).
  Future<String?> syncTime() async {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final d = ByteData(4)..setUint32(0, now, Endian.little);
    try {
      await _writeChar(charCurrentTime, d.buffer.asUint8List());
    } on CharAccessException catch (e) {
      return 'time write failed (${e.reason})';
    }
    try {
      final rb = await _readChar(charCurrentTime);
      if (rb.length < 4) {
        return 'readback short (${rb.length} B) — sync not verified';
      }
      final synced = ByteData.sublistView(rb).getUint32(0, Endian.little);
      final drift = (synced - now).abs();
      if (synced == 0) {
        return 'device epoch is 0 — sync rejected/lost';
      }
      if (drift > 2) {
        return 'sync drift ${drift}s — not verified';
      }
      return null; // verified
    } on CharAccessException catch (e) {
      return 'readback failed (${e.reason}) — FW < 0.18.0?';
    }
  }

  // ── Config ───────────────────────────────────────────────────
  /// v1.50 (audit A3): per-char tolerant + signaled. Fields the device
  /// can't serve are null (UI shows 'Unknown'); failures land in
  /// warnings. NO fabricated defaults — ever.
  Future<DeviceConfig> readConfig() async {
    final warnings = <String>[];
    String? name; ArmSide? arm; Discipline? disc; int? cal;

    try {
      final nb = await _readChar(charDeviceName);
      name = String.fromCharCodes(nb.where((b) => b != 0));
      if (name.isEmpty) warnings.add('device name empty');
    } on CharAccessException catch (e) {
      warnings.add('name: ${e.reason}');
    }
    try {
      arm = (await _readByte(charArmSide)) == 1 ? ArmSide.right : ArmSide.left;
    } on CharAccessException catch (e) {
      warnings.add('arm side: ${e.reason}');
    }
    try {
      final v = await _readByte(charDiscipline);
      disc = Discipline.values[v.clamp(0, 3)];
    } on CharAccessException catch (e) {
      warnings.add('discipline: ${e.reason}');
    }
    try {
      cal = await _readByte(charCalAccuracy);
    } on CharAccessException catch (e) {
      warnings.add('cal: ${e.reason}');
    }
    for (final w in warnings) {
      debugPrint('[SGC] config warning: $w');
    }
    return DeviceConfig(deviceId: ble.device?.remoteId.str ?? '',
      deviceName: name, armSide: arm, discipline: disc,
      calibrationAccuracy: cal, warnings: warnings);
  }

  Future<void> writeDeviceName(String name) async {
    await _writeChar(charDeviceName, Uint8List.fromList(name.codeUnits.take(20).toList()));
  }

  // F21 (2026-10-23): arm side + discipline writes (single byte, same
  // throw-on-failure contract as writeDeviceName).
  Future<void> writeArmSide(ArmSide side) async {
    await _writeChar(charArmSide, Uint8List.fromList([side == ArmSide.right ? 1 : 0]));
  }

  Future<void> writeDiscipline(Discipline d) async {
    await _writeChar(charDiscipline, Uint8List.fromList([d.index]));
  }
  // ── Packed reads ─────────────────────────────────────────────
  /// v1.50: THROWS on failure (audit A4 — '0 on device' was a masker).
  Future<int> getRunCount() async {
    final b = await _readChar(charRunInfo);
    if (b.length < 2) {
      throw CharAccessException(_shortUuid(charRunInfo), 'short read: ${b.length} B (need 2)');
    }
    return b[0] | (b[1] << 8);
  }
  /// ABC5: 0x7F = unknown (no battery / battery-mode / I2C error) → null.
  /// Throws on transport failure. bit7 = charging (not yet driven by FW).
  Future<int?> getBattery() async {
    final v = await _readByte(charBattery);
    return (v & 0x7F) == 0x7F ? null : (v & 0x7F);
  }
  Future<bool> isCharging() async {
    final v = await _readByte(charBattery);
    return (v & 0x80) != 0;
  }
  Future<int> getDeviceState() async => (await _readByte(charDeviceState)) & 0x1F;
  Future<int> getSensorStatus() async => ((await _readByte(charDeviceState)) >> 5) & 0x07;
  Future<int> getFlashUsed() async => await _readByte(charFlashUsed);

  Future<void> sendFtAck() async {
    await _writeChar(charFtRequest, Uint8List.fromList([0x01]));
  }
  Future<String> getRunListJson() async {
    final b = await _readChar(charRunList);
    return String.fromCharCodes(b.where((x) => x != 0));
  }
  /// Download a run's compressed data via the BLE FT protocol (device-push).
  ///
  /// V1.21: RESUME-CAPABLE. On failure (timeout / disconnect / FT error) the
  /// transfer is re-requested from the byte offset already received
  /// (CMD_START with 4-byte LE offset, FW ≥ 5.63) instead of restarting —
  /// the S22's terminal BLE wedge (TC-2026-08-26-001) makes whole-run
  /// restarts unwinnable: each attempt gets ~75% through before the phone
  /// kills the link. With resume, one wedge costs one reconnect, not the run.
  ///
  /// Also fixes two latent parser bugs (invisible while no transfer ever
  /// completed): the START packet size was read from the wrong offset, and
  /// the per-chunk index byte was stored INLINE in the reassembled buffer,
  /// corrupting every 242-byte block.
  Future<Uint8List> downloadRun(int runId, {int expectedSize = 0, void Function(int received, int total)? onProgress}) async {
    const maxAttempts = 6; // 1 initial + up to 5 resumes
    final buf = BytesBuilder();
    final st = _FtState()..total = expectedSize;

    // Capture the device reference up front — survives unexpected drops so
    // we can reconnect for resume without user interaction.
    final dev = ble.device;
    if (dev == null) throw Exception('FT: no connected device');

    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
      try {
        if (!ble.isConnected) {
          debugPrint('[SGC] FT: reconnecting for attempt $attempt (have ${st.received} B)…');
          await ble.connectToDevice(dev);
          await Future.delayed(const Duration(milliseconds: 500)); // link settle
        }
        await _ftAttempt(runId, st.received, buf, st, onProgress);

        // V1.22: a "complete" with missing bytes is NOT complete. FW < 5.65
        // signalled abort with a bare 0x03 byte that the parser mistook for
        // FINAL — the partial buffer looked successful and the run was lost.
        if (st.total > 0 && st.received < st.total) {
          throw TimeoutException('FT: short transfer (${st.received}/${st.total} B)');
        }

        final data = buf.toBytes();
        // Stream-level CRC32 (device FINAL packet) covers the whole run file
        // (header+payload+trailer) — proves BLE-level integrity up front.
        // v1.50 (audit A9): mismatch now FAILS the attempt (resume loop
        // handles it) instead of warn-and-return corrupt bytes.
        if (st.deviceCrc != null && data.isNotEmpty) {
          final local = _crc32(data);
          if (local == st.deviceCrc) {
            debugPrint('[SGC] ✅ stream CRC32 OK (${data.length} B, '
                '${attempt > 1 ? "resumed ×${attempt - 1}" : "first attempt"})');
          } else {
            throw Exception('FT: stream CRC32 mismatch '
                'device=0x${st.deviceCrc!.toRadixString(16)} local=0x${local.toRadixString(16)}');
          }
        }
        return data;
      } catch (e) {
        debugPrint('[SGC] FT attempt $attempt/$maxAttempts failed at ${st.received} B: $e');
        if (attempt == maxAttempts) break;
        await Future.delayed(Duration(seconds: 2 * attempt)); // 2,4,6,8 s backoff
      }
    }
    return Uint8List(0);
  }

  /// One FT attempt. Streams from [offset] (0 = full run). Throws on
  /// timeout / disconnect / FT error — the caller resumes from st.received.
  Future<void> _ftAttempt(int runId, int offset, BytesBuilder buf, _FtState st,
      void Function(int received, int total)? onProgress) async {
    final streamChar = _findChar(charFtStream);
    final reqChar = _findChar(charFtRequest);
    if (streamChar == null || reqChar == null) throw Exception('FT characteristics missing');

    await streamChar.setNotifyValue(true);

    final completer = Completer<void>();
    // Fail fast on link drop instead of hanging until the timeout.
    final connSub = ble.connectionStream.listen((connected) {
      if (!connected && !completer.isCompleted) {
        completer.completeError(TimeoutException('FT: link lost'));
      }
    });

    final streamSub = streamChar.onValueReceived.listen((v) {
      if (v.isEmpty) return;
      switch (v[0]) {
        case 0x01: // START: [0x01, id_lo, id_hi, sz0, sz1, sz2, sz3]
          // V1.21 fix: size starts at v[3] (was read from payload[0] = id_lo).
          if (v.length >= 7) {
            st.total = v[3] | (v[4] << 8) | (v[5] << 16) | (v[6] << 24);
          }
          break;

        case 0x02: // DATA: [0x02, chunk_idx, payload…]
          // V1.21 fix: strip the chunk-index byte (was stored inline,
          // corrupting every 242-byte block of the reassembled run).
          if (v.length >= 2) {
            final data = v.sublist(2);
            buf.add(data);
            st.received += data.length;
            onProgress?.call(st.received, st.total);
          }
          break;

        case 0x03: // FINAL CRC: [0x03, crc0, crc1, crc2, crc3]
          debugPrint('[SGC] FT stream: finalization received (${st.received} B)');
          if (v.length >= 5) {
            st.deviceCrc = v[1] | (v[2] << 8) | (v[3] << 16) | (v[4] << 24);
          }
          if (!completer.isCompleted) completer.complete();
          break;

        case 0x04: // ERROR: [0x04, code]
          final errCode = v.length > 1 ? v[1] : 0;
          if (!completer.isCompleted) {
            completer.completeError('FT error 0x${errCode.toRadixString(16)}');
          }
          break;

        default:
          debugPrint('[SGC] FT stream: unknown packet 0x${v[0].toRadixString(16)}');
      }
    });

    try {
      // CMD_START: [0, runId_lo, runId_hi] + optional LE offset (FW ≥ 5.63).
      final startReq = Uint8List(offset > 0 ? 7 : 3);
      startReq[0] = 0;
      startReq[1] = runId & 0xFF;
      startReq[2] = (runId >> 8) & 0xFF;
      if (offset > 0) {
        startReq[3] = offset & 0xFF;
        startReq[4] = (offset >> 8) & 0xFF;
        startReq[5] = (offset >> 16) & 0xFF;
        startReq[6] = (offset >> 24) & 0xFF;
      }
      debugPrint('[SGC] FT CMD_START run=$runId offset=$offset');
      // Direct write (not _writeChar): failures must reach the resume loop.
      await reqChar.write(startReq, withoutResponse: false);

      await completer.future.timeout(
        const Duration(seconds: 90),
        onTimeout: () => throw TimeoutException('FT stream timeout'),
      );
    } finally {
      await connSub.cancel();
      await streamSub.cancel();
      try { await streamChar.setNotifyValue(false); } catch (_) {}
    }
  }

  /// zlib CRC-32 (init/xorout 0xFFFFFFFF, poly 0xEDB88320) — matches the
  /// device's RawRunStore CRC over the whole run file (header+payload+trailer).
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
}


/// v1.50: a characteristic access failure that MUST surface (audit A1/A2).
class CharAccessException implements Exception {
  final String uuid;   // short form, e.g. '5347ABC1'
  final String reason; // 'characteristic not found ...' / 'read failed: ...'
  CharAccessException(this.uuid, this.reason);
  @override
  String toString() => 'char $uuid: $reason';
}

/// Transfer state carried across FT resume attempts (V1.21).
class _FtState {
  int received = 0;
  int total = 0;
  int? deviceCrc;
}
