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
    // side may arrive as int (native listing) or String (Flutter listing).
    final rawSide = json['side'];
    final side = rawSide is int
        ? (rawSide == 1 ? 'right' : 'left')
        : (rawSide as String? ?? 'left');
    return RunMetadata(
      id: (json['id'] as num).toInt(),
      timestamp: (json['ts'] as num).toInt(),
      size: (json['size'] as num).toInt(),
      side: side,
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
  /// v1.50 (audit A5): NEVER returns a silent []. Empty string, short reads
  /// and parse failures all throw with context — this exact catch masked
  /// the 512-B truncation for days ("0 on device", 2026-10-06).
  Future<List<RunMetadata>> getRunList() async {
    final json = await service.getRunListJson();
    if (json.isEmpty) {
      throw Exception('run list: device returned 0 bytes');
    }
    try {
      final List<dynamic> list = jsonDecode(json);
      return list.map((e) => RunMetadata.fromJson(e as Map<String, dynamic>)).toList();
    } catch (e) {
      final head = json.length > 40 ? json.substring(0, 40) : json;
      final tail = json.length > 40 ? json.substring(json.length - 40) : '';
      throw Exception('run list parse failed ($e) — ${json.length} B, head="$head", tail="$tail"');
    }
  }

  /// Download a run's compressed data via BLE file transfer protocol.
  Future<TransferResult> download(int runId, {void Function(int received, int total)? onProgress}) async {
    status = TransferStatus.transferring;
    try {
      final data = await service.downloadRun(runId, onProgress: onProgress);
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
