import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// F43 offline upload queue — durable, survives app restarts.
///
/// One JSON file (`upload_queue.json`, atomic temp+rename writes, same
/// discipline as merged_runs.json). Entries are keyed by client_run_uuid,
/// which doubles as the server idempotency key: a retry after a lost ACK
/// can NEVER create a duplicate run server-side (POST /v1/runs returns
/// 200 already_exists=true for a repeat uuid).
///
/// Statuses:
///   PENDING   — waiting to upload (backoff via nextAttemptAtMs)
///   DONE      — confirmed by the server (cloud run id kept for reference)
///   POISONED  — the server rejected the payload definitively (413 /
///               validation): a retry with the same bytes can never
///               succeed, so we stop retrying and surface it in the UI.
///
/// Monotonic-shrink guard: deliberately NOT applied here — the queue is
/// allowed to shrink (DONE pruning, GDPR clear), unlike the merge index.
enum UploadStatus { pending, done, poisoned }

class UploadEntry {
  final String clientRunUuid;
  final String fileName; // local run file (run_<id>_<ts>.bin)
  final UploadStatus status;
  final int attempts;
  final int nextAttemptAtMs; // epoch ms; 0 = due immediately
  final int? runId; // cloud run id once the server confirmed
  final String? lastError;
  final int queuedAtMs;

  const UploadEntry({
    required this.clientRunUuid,
    required this.fileName,
    this.status = UploadStatus.pending,
    this.attempts = 0,
    this.nextAttemptAtMs = 0,
    this.runId,
    this.lastError,
    required this.queuedAtMs,
  });

  UploadEntry copyWith({
    UploadStatus? status,
    int? attempts,
    int? nextAttemptAtMs,
    int? runId,
    String? lastError,
    bool clearLastError = false,
  }) =>
      UploadEntry(
        clientRunUuid: clientRunUuid,
        fileName: fileName,
        status: status ?? this.status,
        attempts: attempts ?? this.attempts,
        nextAttemptAtMs: nextAttemptAtMs ?? this.nextAttemptAtMs,
        runId: runId ?? this.runId,
        lastError: clearLastError ? null : (lastError ?? this.lastError),
        queuedAtMs: queuedAtMs,
      );

  Map<String, dynamic> toJson() => {
        'clientRunUuid': clientRunUuid,
        'fileName': fileName,
        'status': status.name,
        'attempts': attempts,
        'nextAttemptAtMs': nextAttemptAtMs,
        if (runId != null) 'runId': runId,
        if (lastError != null) 'lastError': lastError,
        'queuedAtMs': queuedAtMs,
      };

  factory UploadEntry.fromJson(Map<String, dynamic> json) => UploadEntry(
        clientRunUuid: json['clientRunUuid'] as String,
        fileName: json['fileName'] as String,
        status: UploadStatus.values.firstWhere(
          (s) => s.name == json['status'],
          orElse: () => UploadStatus.pending,
        ),
        attempts: json['attempts'] as int? ?? 0,
        nextAttemptAtMs: json['nextAttemptAtMs'] as int? ?? 0,
        runId: json['runId'] as int?,
        lastError: json['lastError'] as String?,
        queuedAtMs: json['queuedAtMs'] as int? ?? 0,
      );
}

class UploadQueue {
  static final UploadQueue instance = UploadQueue._();
  UploadQueue._();

  static const _queueFile = 'upload_queue.json';

  /// Exponential backoff: 1m → 2m → 4m → 8m → 16m → 32m → 1h cap.
  /// No attempt ceiling: transient outages (weeks on a mountain) must not
  /// permanently lose a run — only 413/validation poisons an entry.
  static Duration _backoffFor(int attempts) {
    // int.clamp returns num — cast before the shift or it won't compile.
    final exp = attempts < 0 ? 0 : (attempts > 6 ? 6 : attempts);
    final minutes = 1 << exp;
    return Duration(minutes: minutes > 60 ? 60 : minutes);
  }

  List<UploadEntry> _entries = [];

  /// Unmodifiable view for the UI (queue depth indicators etc.).
  List<UploadEntry> get entries => List.unmodifiable(_entries);

  int get pendingCount => _entries
      .where((e) => e.status == UploadStatus.pending)
      .length;
  int get doneCount =>
      _entries.where((e) => e.status == UploadStatus.done).length;
  int get poisonedCount => _entries
      .where((e) => e.status == UploadStatus.poisoned)
      .length;

  UploadEntry? forFile(String fileName) {
    for (final e in _entries) {
      if (e.fileName == fileName) return e;
    }
    return null;
  }

  UploadEntry? forUuid(String uuid) {
    for (final e in _entries) {
      if (e.clientRunUuid == uuid) return e;
    }
    return null;
  }

  /// Load from disk. A corrupt file starts from an empty queue (uploads
  /// are idempotent server-side, so re-enqueueing everything is safe —
  /// at worst already_exists=true responses).
  Future<void> load() async {
    try {
      final dir = await getApplicationDocumentsDirectory();
      final f = File('${dir.path}/$_queueFile');
      if (!await f.exists()) {
        _entries = [];
        return;
      }
      final list = jsonDecode(await f.readAsString()) as List<dynamic>;
      final out = <UploadEntry>[];
      for (final e in list) {
        try {
          out.add(UploadEntry.fromJson(e as Map<String, dynamic>));
        } catch (err) {
          debugPrint('[UploadQueue] skipping corrupt entry: $err');
        }
      }
      _entries = out;
    } catch (e) {
      debugPrint('[UploadQueue] load failed, starting empty: $e');
      _entries = [];
    }
  }

  Future<void> _persist() async {
    final dir = await getApplicationDocumentsDirectory();
    final f = File('${dir.path}/$_queueFile');
    final tmp = File('${f.path}.tmp');
    await tmp.writeAsString(jsonEncode(_entries.map((e) => e.toJson()).toList()),
        flush: true);
    await tmp.rename(f.path);
  }

  /// Enqueue a run by local file name. Stable uuid minted ONCE and kept in
  /// the entry — retries reuse it, that's the whole idempotency contract.
  Future<UploadEntry> enqueue(String fileName) async {
    final existing = forFile(fileName);
    if (existing != null) return existing;
    final entry = UploadEntry(
      clientRunUuid: newUuidV4(),
      fileName: fileName,
      queuedAtMs: DateTime.now().millisecondsSinceEpoch,
    );
    _entries.add(entry);
    await _persist();
    debugPrint('[UploadQueue] enqueued $fileName → ${entry.clientRunUuid}');
    return entry;
  }

  /// 2xx (including already_exists=true) — confirmed on the server.
  Future<void> markDone(String uuid, int cloudRunId) async {
    _replace(uuid, (e) => e.copyWith(
          status: UploadStatus.done,
          runId: cloudRunId,
          clearLastError: true,
        ));
    await _persist();
  }

  /// Definitive rejection (413 / validation): never retried.
  Future<void> markPoisoned(String uuid, String error) async {
    _replace(uuid, (e) => e.copyWith(
          status: UploadStatus.poisoned,
          lastError: error.length > 300 ? error.substring(0, 300) : error,
        ));
    await _persist();
  }

  /// Transient failure (network / 5xx / 403 activation): stays PENDING,
  /// next attempt scheduled with exponential backoff.
  Future<void> markRetry(String uuid, String error) async {
    final now = DateTime.now().millisecondsSinceEpoch;
    _replace(uuid, (e) {
      final attempts = e.attempts + 1;
      return e.copyWith(
        attempts: attempts,
        nextAttemptAtMs:
            now + _backoffFor(attempts - 1).inMilliseconds,
        lastError: error.length > 300 ? error.substring(0, 300) : error,
      );
    });
    await _persist();
  }

  /// Due = PENDING and backoff elapsed (nextAttemptAtMs == 0 = immediately).
  List<UploadEntry> dueEntries(DateTime now) => [
        for (final e in _entries)
          if (e.status == UploadStatus.pending &&
              e.nextAttemptAtMs <= now.millisecondsSinceEpoch)
            e,
      ];

  /// Earliest scheduled retry among PENDING entries (null = nothing
  /// waiting) — the sync worker arms its retry timer from this.
  DateTime? get nextRetryAt {
    DateTime? out;
    for (final e in _entries) {
      if (e.status != UploadStatus.pending || e.nextAttemptAtMs == 0) continue;
      final t = DateTime.fromMillisecondsSinceEpoch(e.nextAttemptAtMs);
      if (out == null || t.isBefore(out)) out = t;
    }
    return out;
  }

  void _replace(String uuid, UploadEntry Function(UploadEntry) fn) {
    for (int i = 0; i < _entries.length; i++) {
      if (_entries[i].clientRunUuid == uuid) {
        _entries[i] = fn(_entries[i]);
        return;
      }
    }
  }

  /// Explicit allow-shrink: local run deleted → drop its queue entry.
  Future<void> removeFile(String fileName) async {
    _entries.removeWhere((e) => e.fileName == fileName);
    await _persist();
  }

  /// GDPR: wipe the whole queue (uploaded history is hard-deleted
  /// server-side; pending entries must not resurrect it).
  Future<void> clear() async {
    _entries = [];
    await _persist();
  }
}

/// RFC-4122-shaped v4 UUID from a secure RNG. No dependency: the server
/// only needs a unique string (8..64 chars), but we keep the canonical
/// format so it reads as a uuid in the DB.
String newUuidV4() {
  final rng = Random.secure();
  final b = List<int>.generate(16, (_) => rng.nextInt(256));
  b[6] = (b[6] & 0x0F) | 0x40; // version 4
  b[8] = (b[8] & 0x3F) | 0x80; // variant 10
  String hex(int i) => b[i].toRadixString(16).padLeft(2, '0');
  final h = [for (int i = 0; i < 16; i++) hex(i)].join();
  return '${h.substring(0, 8)}-${h.substring(8, 12)}-'
      '${h.substring(12, 16)}-${h.substring(16, 20)}-${h.substring(20)}';
}
