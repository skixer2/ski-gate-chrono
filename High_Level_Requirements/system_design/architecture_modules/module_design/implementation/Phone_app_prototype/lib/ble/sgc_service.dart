import 'dart:async';
import 'dart:typed_data';
import 'ble_manager.dart';
import '../models/device_config.dart';
import '../models/gate_side.dart';

class SGCService {
  final BLEManager ble;
  SGCService(this.ble);

  static const serviceUuid = '53470000-0000-1000-8000-00805F9B34FB';
  static const charCurrentTime = '5347ABC0-0000-1000-8000-00805F9B34FB';
  static const charDeviceName = '5347ABC1-0000-1000-8000-00805F9B34FB';
  static const charArmSide = '5347ABC2-0000-1000-8000-00805F9B34FB';
  static const charDiscipline = '5347ABC3-0000-1000-8000-00805F9B34FB';
  static const charDeviceState = '5347ABC4-0000-1000-8000-00805F9B34FB';
  static const charBattery = '5347ABC5-0000-1000-8000-00805F9B34FB';
  static const charCharging = '5347ABCF-0000-1000-8000-00805F9B34FB';
  static const charRunCount = '5347ABC6-0000-1000-8000-00805F9B34FB';
  static const charRunList = '5347ABC9-0000-1000-8000-00805F9B34FB';
  static const charFtRequest = '5347ABCA-0000-1000-8000-00805F9B34FB';
  static const charFtChunk = '5347ABCB-0000-1000-8000-00805F9B34FB';
  static const charFtCrc = '5347ABCC-0000-1000-8000-00805F9B34FB';
  static const charFtStatus = '5347ABCD-0000-1000-8000-00805F9B34FB';
  static const charCalAccuracy = '5347ABD0-0000-1000-8000-00805F9B34FB';

  Future<void> syncTime() async {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final data = ByteData(4)..setUint32(0, now, Endian.little);
    await _writeChar(charCurrentTime, data.buffer.asUint8List());
  }

  Future<DeviceConfig> readConfig() async {
    final nameBytes = await _readChar(charDeviceName);
    final armSideByte = await _readChar(charArmSide);
    final discByte = await _readChar(charDiscipline);
    final accByte = await _readChar(charCalAccuracy);
    return DeviceConfig(
      deviceId: ble.device?.remoteId.str ?? '',
      deviceName: String.fromCharCodes(nameBytes),
      armSide: armSideByte.isNotEmpty && armSideByte[0] == 1 ? ArmSide.right : ArmSide.left,
      discipline: discByte.isNotEmpty ? Discipline.values[discByte[0].clamp(0, 3)] : Discipline.gs,
      calibrationAccuracy: accByte.isNotEmpty ? accByte[0] : 0,
    );
  }

  Future<int> getRunCount() async {
    final bytes = await _readChar(charRunCount);
    return bytes.isNotEmpty && bytes.length >= 2
        ? (bytes[0] | (bytes[1] << 8)) : 0;
  }

  Future<Uint8List> downloadRun(int runId) async {
    final req = ByteData(2)..setUint16(0, runId, Endian.little);
    await _writeChar(charFtRequest, req.buffer.asUint8List());
    final buffer = BytesBuilder();
    // Listen for chunks (simplified — production uses notifications)
    for (int i = 0; i < 500; i++) {
      final chunk = await _readChar(charFtChunk);
      if (chunk.isEmpty) break;
      buffer.add(chunk);
      await Future.delayed(const Duration(milliseconds: 10));
    }
    return buffer.toBytes();
  }

  Future<Uint8List> _readChar(String uuid) async {
    if (ble.device == null) return Uint8List(0);
    final services = await ble.device!.discoverServices();
    for (final s in services) {
      for (final c in s.characteristics) {
        if (c.uuid.toString().toUpperCase() == uuid.toUpperCase()) {
          final bytes = await c.read();
          return Uint8List.fromList(bytes);
        }
      }
    }
    return Uint8List(0);
  }

  Future<void> _writeChar(String uuid, Uint8List data) async {
    if (ble.device == null) return;
    final services = await ble.device!.discoverServices();
    for (final s in services) {
      for (final c in s.characteristics) {
        if (c.uuid.toString().toUpperCase() == uuid.toUpperCase()) {
          await c.write(data, withoutResponse: false);
          return;
        }
      }
    }
  }
}
