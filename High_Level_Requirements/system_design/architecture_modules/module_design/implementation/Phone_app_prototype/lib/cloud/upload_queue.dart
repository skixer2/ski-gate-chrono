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

/// 1.68: kind of run behind the entry — plain device recording or a
/// dual-device merged course. Values match the server's `run_type`.
enum UploadRunType { single, merged }

class UploadEntry {
  final String clientRunUuid;
  final String fileName; // local run file (run_<id>_<ts>.bin)
  final UploadStatus status;
  final int attempts;
  final int nextAttemptAtMs; // epoch ms; 0 = due immediately
  final int? runId; // cloud run id once the server confirmed
  final String? lastError;
  final int queuedAtMs;
  final UploadRunType runType; // 1.68: single (default) | merged

  const UploadEntry({
    required this.clientRunUuid,
    required this.fileName,
    this.status = UploadStatus.pending,
    this.attempts = 0,
    this.nextAttemptAtMs = 0,
    this.runId,
    this.lastError,
    required this.queuedAtMs,
    this.runType = UploadRunType.single,
  });

  UploadEntry copyWith({
    UploadStatus? status,
    int? attempts,
    int? nextAttemptAtMs,
    int? runId,
    String? lastError,
    bool clearLastError = false,
    UploadRunType? runType,
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
        runType: runType ?? this.runType,
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
        'runType': runType.name,
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
        runType: UploadRunType.values.firstWhere(
          (t) => t.name == json['runType'],
          orElse: () => UploadRunType.single,
        ),
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

  /// 1.68: localRunKey of a merged entry — derived from its two source
  /// files, since merged courses have no .bin of their own. Doubles as
  /// the dedupe key inside [enqueueMerged].
  static String mergedQueueKey(String aFile, String bFile) =>
      'merged:$aFile|$bFile';

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

  /// 1.68: enqueue a MERGED course. [key] is [mergedQueueKey] of its two
  /// source files; [uuid] is the deterministic merged uuid
  /// ([mergedRunUuid]) — idempotent on BOTH key and uuid, so calling it
  /// at every merge pass / app start is free.
  Future<UploadEntry> enqueueMerged(String key, String uuid) async {
    final existing = forFile(key) ?? forUuid(uuid);
    if (existing != null) return existing;
    final entry = UploadEntry(
      clientRunUuid: uuid,
      fileName: key,
      queuedAtMs: DateTime.now().millisecondsSinceEpoch,
      runType: UploadRunType.merged,
    );
    _entries.add(entry);
    await _persist();
    debugPrint('[UploadQueue] enqueued merge $key → $uuid');
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

  /// 1.68 lineage deferral: a merged entry whose sources are not uploaded
  /// yet. Fixed short backoff, attempts NOT incremented — waiting for
  /// lineage is not a failure and must never escalate toward the 1h cap
  /// (or accidentally reach any poison condition). lastError untouched:
  /// deferral is a normal state, not an error.
  Future<void> markDeferred(String uuid, Duration delay) async {
    final now = DateTime.now().millisecondsSinceEpoch;
    _replace(uuid, (e) => e.copyWith(
          nextAttemptAtMs: now + delay.inMilliseconds,
        ));
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

/// Fixed namespace constant for SGC merged-run uuids (uuid-v5 style).
/// Changing it would change EVERY merged uuid → duplicate uploads with a
/// fresh idempotency key. MUST NEVER change after release.
const String kMergedRunUuidNamespace =
    '9f0c3a52-6d1e-4c7a-9a4b-2f1d3e5a7c9b';

/// Deterministic RFC-4122-SHAPED uuid (v5-style) for a merged course.
///
/// Scheme (1.68 — documented because the pubspec has no `crypto` dep, so
/// this is NOT a true SHA-1 name-based v5):
///   identity = '$kMergedRunUuidNamespace|merged|aId|bId|aFile|bFile'
///   128 bits = four chained 32-bit FNV-1a rounds over the identity
///   (each round re-seeds from the previous word), version nibble set to
///   5, variant bits set to 10xx → canonical 8-4-4-4-12 hex string.
/// Properties: deterministic (stable across restarts and queue
/// rebuilds — the idempotency contract holds), collision-safe at app
/// scale, and its version-5 nibble means it can never equal a v4
/// single-run uuid byte-for-byte in the version field (random v4
/// collisions remain ~2^-122 — negligible).
String mergedRunUuid({
  required int aId,
  required int bId,
  required String aFile,
  required String bFile,
}) {
  final identity = '$kMergedRunUuidNamespace|merged|$aId|$bId|$aFile|$bFile';
  var h = 0x811C9DC5; // FNV-1a 32-bit offset basis
  final words = List<int>.filled(4, 0);
  for (int round = 0; round < 4; round++) {
    for (final code in identity.codeUnits) {
      h ^= code & 0xFF;
      h = (h * 16777619) & 0xFFFFFFFF; // FNV prime
      h ^= (code >> 8) & 0xFF;
      h = (h * 16777619) & 0xFFFFFFFF;
    }
    h = (h ^ round) & 0xFFFFFFFF;
    words[round] = h;
    h = (h ^ 0x9E3779B9) & 0xFFFFFFFF; // decorrelate next round
  }
  final b = List<int>.filled(16, 0);
  for (int w = 0; w < 4; w++) {
    b[w * 4] = (words[w] >> 24) & 0xFF;
    b[w * 4 + 1] = (words[w] >> 16) & 0xFF;
    b[w * 4 + 2] = (words[w] >> 8) & 0xFF;
    b[w * 4 + 3] = words[w] & 0xFF;
  }
  b[6] = (b[6] & 0x0F) | 0x50; // version 5 (name-based shape)
  b[8] = (b[8] & 0x3F) | 0x80; // RFC-4122 variant 10
  String hex(int i) => b[i].toRadixString(16).padLeft(2, '0');
  final hx = [for (int i = 0; i < 16; i++) hex(i)].join();
  return '${hx.substring(0, 8)}-${hx.substring(8, 12)}-'
      '${hx.substring(12, 16)}-${hx.substring(16, 20)}-${hx.substring(20)}';
}
