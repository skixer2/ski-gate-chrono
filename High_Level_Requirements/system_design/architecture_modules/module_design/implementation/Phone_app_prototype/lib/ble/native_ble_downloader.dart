import 'dart:typed_data';
import 'package:flutter/services.dart';

class NativeDownloadedRun {
  final int id;
  final int timestamp;
  final int size;
  final Uint8List data;

  const NativeDownloadedRun({
    required this.id,
    required this.timestamp,
    required this.size,
    required this.data,
  });
}

class NativeFailedRun {
  final int id;
  final String reason;

  const NativeFailedRun({required this.id, required this.reason});
}

class NativeBatchResult {
  final List<NativeDownloadedRun> runs;
  final List<NativeFailedRun> failed;
  final List<String> log;

  const NativeBatchResult({
    required this.runs,
    required this.failed,
    required this.log,
  });
}

/// Platform-channel bridge to the native Android BluetoothGatt downloader.
///
/// Flutter keeps UI/decompression/storage. The native side owns the BLE-
/// critical path with explicit serialized GATT operations.
class NativeBleDownloader {
  static const MethodChannel _channel = MethodChannel('sgc_native_ble');

  Future<NativeBatchResult> downloadRuns({
    required String address,
    required List<int> runIds,
  }) async {
    final res = await _channel.invokeMethod<Map<dynamic, dynamic>>(
      'downloadRuns',
      {'address': address, 'runIds': runIds},
    );

    final runsRaw = (res?['runs'] as List<dynamic>? ?? const []);
    final failedRaw = (res?['failed'] as List<dynamic>? ?? const []);
    final logRaw = (res?['log'] as List<dynamic>? ?? const []);

    return NativeBatchResult(
      runs: runsRaw.map((e) {
        final m = e as Map<dynamic, dynamic>;
        return NativeDownloadedRun(
          id: m['id'] as int,
          timestamp: m['timestamp'] as int,
          size: m['size'] as int,
          data: m['data'] as Uint8List,
        );
      }).toList(),
      failed: failedRaw.map((e) {
        final m = e as Map<dynamic, dynamic>;
        return NativeFailedRun(
          id: m['id'] as int,
          reason: m['reason'] as String? ?? 'unknown',
        );
      }).toList(),
      log: logRaw.map((e) => e.toString()).toList(),
    );
  }

  Future<String> readRunList({required String address}) async {
    final res = await _channel.invokeMethod<String>('readRunList', {'address': address});
    return res ?? '';
  }

  Future<void> cancel() => _channel.invokeMethod<void>('cancel');
}
