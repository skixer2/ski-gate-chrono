import 'dart:typed_data';
import 'sgc_service.dart';

enum TransferStatus { idle, transferring, complete, error }

class TransferResult {
  final Uint8List compressedData;
  final int runId;
  const TransferResult({required this.compressedData, required this.runId});
}

class FileTransfer {
  final SGCService service;
  TransferStatus status = TransferStatus.idle;
  int chunksReceived = 0;

  FileTransfer(this.service);

  Future<TransferResult> download(int runId) async {
    status = TransferStatus.transferring;
    chunksReceived = 0;
    try {
      final data = await service.downloadRun(runId);
      if (data.isEmpty) {
        status = TransferStatus.error;
        return TransferResult(compressedData: Uint8List(0), runId: runId);
      }
      status = TransferStatus.complete;
      return TransferResult(compressedData: data, runId: runId);
    } catch (_) {
      status = TransferStatus.error;
      return TransferResult(compressedData: Uint8List(0), runId: runId);
    }
  }
}
