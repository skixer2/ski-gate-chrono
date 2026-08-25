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

  // ── File transfer ────────────────────────────────────────────
  Future<String> getRunListJson() async {
    final b = await _readChar(charRunList);
    return String.fromCharCodes(b.where((x) => x != 0));
  }

  Future<Uint8List> downloadRun(int runId, {int expectedSize = 0}) async {
    final chunkChar = _findChar(charFtChunk);
    final statusChar = _findChar(charFtStatus);
    final crcChar = _findChar(charFtCrc);
    if (chunkChar == null || statusChar == null) throw Exception('FT chars missing');

    // Subscribe to ABCB (chunk data) and ABCD (status)
    await chunkChar.setNotifyValue(true);
    await statusChar.setNotifyValue(true);

    // Completer for chunk notifications (one at a time)
    Completer<Uint8List>? chunkCompleter;
    final chunkSub = chunkChar.onValueReceived.listen((v) {
      chunkCompleter?.complete(Uint8List.fromList(v));
    });

    // Status listener — completes on FT_DONE(2) or FT_ERROR(3)
    final statusCompleter = Completer<int>();
    StreamSubscription<List<int>>? statusSub;
    statusSub = statusChar.onValueReceived.listen((v) {
      if (v.isEmpty || statusCompleter.isCompleted) return;
      final st = v[0];
      if (st == 2 || st == 3) {
        statusCompleter.complete(st);
      }
      // st == 1 (FT_READY) — implicitly handled by the 200ms delay below
    });

    try {
      // 1. CMD_START: open run, init transfer
      final startReq = Uint8List(3)
        ..[0] = 0
        ..[1] = (runId & 0xFF)
        ..[2] = ((runId >> 8) & 0xFF);
      await _writeChar(charFtRequest, startReq);
      debugPrint('[SGC] FT CMD_START run=$runId');

      // 2. Wait for FT_READY (ABCD = 1) — 200ms for device to prepare
      await Future.delayed(const Duration(milliseconds: 200));

      // 3. Pull loop — phone requests one chunk at a time
      final buf = BytesBuilder();
      var offset = 0;
      final chunkSize = _ble.mtu > 3 ? _ble.mtu - 3 : 20;
      var chunkCount = 0;
      debugPrint('[SGC] FT pull loop start (mtu=${_ble.mtu}, chunkSize=$chunkSize)');

      while (true) {
        // Request chunk at current offset
        chunkCompleter = Completer<Uint8List>();
        final chunkReq = Uint8List(5)
          ..[0] = 1
          ..[1] = (offset & 0xFF)
          ..[2] = ((offset >> 8) & 0xFF)
          ..[3] = ((offset >> 16) & 0xFF)
          ..[4] = ((offset >> 24) & 0xFF);
        await _writeChar(charFtRequest, chunkReq);

        // Wait for chunk notification (timeout 15s — S22 can stall on GATT)
        final chunk = await chunkCompleter!.future.timeout(
          const Duration(seconds: 15),
          onTimeout: () {
            debugPrint('[SGC] FT chunk timeout at offset=$offset chunk=$chunkCount');
            return Uint8List(0);
          },
        );

        if (chunk.isEmpty) {
          // Could be FT_DONE or error — check status
          if (statusCompleter.isCompleted) {
            final st = await statusCompleter.future;
            debugPrint('[SGC] FT status=$st at offset=$offset');
          }
          break;
        }

        buf.add(chunk);
        offset += chunk.length;
        chunkCount++;

        // Log progress every 20 chunks
        if (chunkCount % 20 == 0) {
          debugPrint('[SGC] FT recv $offset bytes…');
        }

        // If chunk is smaller than chunkSize, this was the last chunk
        if (chunk.length < chunkSize) {
          debugPrint('[SGC] FT last chunk ($chunkCount chunks, $offset bytes)');
          break;
        }

        // Check if status says done (device may have sent FT_DONE)
        if (statusCompleter.isCompleted) {
          final st = await statusCompleter.future;
          if (st == 2) {
            debugPrint('[SGC] FT_DONE at $offset bytes');
            break;
          } else if (st == 3) {
            debugPrint('[SGC] FT_ERROR at $offset bytes');
            break;
          }
        }
      }

      // 4. Read CRC32 from ABCC
      int deviceCrc = 0;
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

      // 5. Wait for FT_DONE status if not already received
      int finalStatus = 2; // assume done
      if (!statusCompleter.isCompleted) {
        finalStatus = await statusCompleter.future.timeout(
          const Duration(seconds: 5),
          onTimeout: () => 2,
        );
      } else {
        finalStatus = await statusCompleter.future;
      }

      debugPrint('[SGC] FT complete: ${buf.length} bytes, chunks=$chunkCount, '
          'status=$finalStatus, crc=0x${deviceCrc.toRadixString(16)}');
      return buf.toBytes();
    } finally {
      await chunkSub.cancel();
      await statusSub?.cancel();
      try { await chunkChar.setNotifyValue(false); } catch (_) {}
      try { await statusChar.setNotifyValue(false); } catch (_) {}
    }
  }
}
