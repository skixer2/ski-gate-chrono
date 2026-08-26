import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'ble_manager.dart';
import '../models/device_config.dart';
import '../models/gate_side.dart';

class SGCService {
  final BLEManager ble;
  final List<BluetoothService> _services;

  SGCService(this.ble) : _services = ble.services ?? [];

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
  static const charFtChunk     = '5347ABCB-0000-1000-8000-00805F9B34FB';
  static const charFtCrc       = '5347ABCC-0000-1000-8000-00805F9B34FB';
  static const charFtStatus    = '5347ABCD-0000-1000-8000-00805F9B34FB';
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

  Future<Uint8List> _readChar(String uuid) async {
    final c = _findChar(uuid);
    if (c == null) { debugPrint('[SGC] char not found: $uuid'); return Uint8List(0); }
    try { return Uint8List.fromList(await c.read()); }
    catch (e) { debugPrint('[SGC] read fail: $e'); return Uint8List(0); }
  }

  Future<void> _writeChar(String uuid, Uint8List data) async {
    final c = _findChar(uuid);
    if (c == null) { debugPrint('[SGC] char not found: $uuid'); return; }
    try { await c.write(data, withoutResponse: false); } catch (_) {}
  }

  // ── Time ─────────────────────────────────────────────────────
  Future<void> syncTime() async {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final d = ByteData(4)..setUint32(0, now, Endian.little);
    await _writeChar(charCurrentTime, d.buffer.asUint8List());
  }

  // ── Config ───────────────────────────────────────────────────
  Future<DeviceConfig> readConfig() async {
    final nb = await _readChar(charDeviceName);
    final ab = await _readChar(charArmSide);
    final db = await _readChar(charDiscipline);
    final cb = await _readChar(charCalAccuracy);
    return DeviceConfig(deviceId: ble.device?.remoteId.str ?? '',
      deviceName: String.fromCharCodes(nb.where((b) => b != 0)),
      armSide: ab.isNotEmpty && ab[0] == 1 ? ArmSide.right : ArmSide.left,
      discipline: db.isNotEmpty ? Discipline.values[db[0].clamp(0, 3)] : Discipline.gs,
      calibrationAccuracy: cb.isNotEmpty ? cb[0] : 0);
  }

  Future<void> writeDeviceName(String name) async {
    await _writeChar(charDeviceName, Uint8List.fromList(name.codeUnits.take(20).toList()));
  }

  // ── Packed reads ─────────────────────────────────────────────
  Future<int> getRunCount() async {
    final b = await _readChar(charRunInfo);
    return b.length >= 2 ? b[0] | (b[1] << 8) : 0;
  }
  Future<int> getBattery() async {
    final b = await _readChar(charBattery);
    return b.isNotEmpty ? (b[0] & 0x7F) : -1;
  }
  Future<bool> isCharging() async {
    final b = await _readChar(charBattery);
    return b.isNotEmpty && ((b[0] & 0x80) != 0);
  }
  Future<int> getDeviceState() async {
    final b = await _readChar(charDeviceState);
    return b.isNotEmpty ? (b[0] & 0x1F) : 0;
  }
  Future<int> getSensorStatus() async {
    final b = await _readChar(charDeviceState);
    return b.isNotEmpty ? ((b[0] >> 5) & 0x07) : 0;
  }
  Future<int> getFlashUsed() async {
    final b = await _readChar(charFlashUsed);
    return b.isNotEmpty ? b[0] : 0;
  }

  Future<void> sendFtAck() async {
    await _writeChar(charFtStatus, Uint8List.fromList([0x01]));
  }
  Future<String> getRunListJson() async {
    final b = await _readChar(charRunList);
    return String.fromCharCodes(b.where((x) => x != 0));
  }

  Future<Uint8List> downloadRun(int runId, {int expectedSize = 0, void Function(int received, int total)? onProgress}) async {
    final chunkChar = _findChar(charFtChunk);
    final statusChar = _findChar(charFtStatus);
    final crcChar = _findChar(charFtCrc);
    if (chunkChar == null || statusChar == null) throw Exception('FT chars missing');

    // Subscribe to ABCB (chunk data) and ABCD (status)
    await chunkChar.setNotifyValue(true);
    await statusChar.setNotifyValue(true);

    final buf = BytesBuilder();
    var totalReceived = 0;
    var chunkCount = 0;

    // Listen for chunk notifications — device pushes, phone just collects
    final chunkSub = chunkChar.onValueReceived.listen((v) async {
      if (v.isEmpty) return;
      buf.add(Uint8List.fromList(v));
      totalReceived += v.length;
      chunkCount++;
      
      // V5.50: Send ACK back to device to signal readiness for the next chunk.
      // This prevents the nRF52 from blocking on writeValue if the S22 buffer is full.
      try {
        await _writeChar(charFtStatus, Uint8List.fromList([0x01]));
      } catch (e) {
        debugPrint('[SGC] ACK fail: $e');
      }

      if (onProgress != null && expectedSize > 0) {
        onProgress(totalReceived, expectedSize);
      }
    });

    // Listen for status — FT_DONE(2) or FT_ERROR(3)
    final statusCompleter = Completer<int>();
    final statusSub = statusChar.onValueReceived.listen((v) {
      if (v.isEmpty || statusCompleter.isCompleted) return;
      final st = v[0];
      if (st == 2 || st == 3) {
        statusCompleter.complete(st);
      }
    });

    try {
      // 1. CMD_START: open run, device begins pushing chunks
      final startReq = Uint8List(3)
        ..[0] = 0
        ..[1] = (runId & 0xFF)
        ..[2] = ((runId >> 8) & 0xFF);
      await _writeChar(charFtRequest, startReq);
      debugPrint('[SGC] FT CMD_START run=$runId (device-push, listening)');

      // 2. Wait for FT_DONE or FT_ERROR (timeout 60s)
      final status = await statusCompleter.future.timeout(
        const Duration(seconds: 60),
        onTimeout: () {
          debugPrint('[SGC] FT timeout at $totalReceived bytes, $chunkCount chunks');
          return 3;
        },
      );

      debugPrint('[SGC] FT status=$status, received=$totalReceived bytes, chunks=$chunkCount');

      // 3. Read CRC32 from ABCC
      int deviceCrc = 0;
      if (status == 2) {
        try {
          if (crcChar != null) {
            final crcBytes = await crcChar.read();
            if (crcBytes.length >= 4) {
              deviceCrc = crcBytes[0] |
                  (crcBytes[1] << 8) |
                  (crcBytes[2] << 16) |
                  (crcBytes[3] << 24);
            }
          }
        } catch (e) {
          debugPrint('[SGC] FT CRC read failed: $e');
        }
      }

      debugPrint('[SGC] FT complete: ${buf.length} bytes, chunks=$chunkCount, '
          'status=$status, crc=0x${deviceCrc.toRadixString(16)}');
      return buf.toBytes();
    } finally {
      await chunkSub.cancel();
      await statusSub.cancel();
      try { await chunkChar.setNotifyValue(false); } catch (_) {}
      try { await statusChar.setNotifyValue(false); } catch (_) {}
    }
  }
}
