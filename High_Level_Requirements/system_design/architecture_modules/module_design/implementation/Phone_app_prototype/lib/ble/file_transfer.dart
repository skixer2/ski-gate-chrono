import 'dart:typed_data';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'sgc_service.dart';

enum TransferStatus { idle, transferring, complete, error }

class RunMetadata {
  final int id;
  final int timestamp;
  final int size;
  final String side; // "left" or "right"
  const RunMetadata({required this.id, required this.timestamp, required this.size, required this.side});

  factory RunMetadata.fromJson(Map<String, dynamic> json) {
    return RunMetadata(
      id: json['id'] as int,
      timestamp: json['ts'] as int,
      size: json['size'] as int,
      side: json['side'] as String? ?? 'left',
    );
  }
}

class TransferResult {
  final Uint8List compressedData;
  final int runId;
  const TransferResult({required this.compressedData, required this.runId});
}

class FileTransfer {
  final SGCService service;
  TransferStatus status = TransferStatus.idle;

  FileTransfer(this.service);

  /// Fetch the list of runs stored on the device.
  Future<List<RunMetadata>> getRunList() async {
    try {
      final json = await service.getRunListJson();
      if (json.isEmpty) return [];
      final List<dynamic> list = jsonDecode(json);
      return list.map((e) => RunMetadata.fromJson(e as Map<String, dynamic>)).toList();
    } catch (e) {
      debugPrint('[FT] Failed to parse run list: $e');
      return [];
    }
  }

  /// Download a run's compressed data via BLE file transfer protocol.
  Future<TransferResult> download(int runId) async {
    status = TransferStatus.transferring;
    try {
      final data = await service.downloadRun(runId);
      if (data.isEmpty) {
        status = TransferStatus.error;
        return TransferResult(compressedData: Uint8List(0), runId: runId);
      }
      status = TransferStatus.complete;
      debugPrint('[FT] Downloaded run #$runId: ${data.length} bytes');
      return TransferResult(compressedData: data, runId: runId);
    } catch (e) {
      status = TransferStatus.error;
      debugPrint('[FT] Download error: $e');
      return TransferResult(compressedData: Uint8List(0), runId: runId);
    }
  }
}
