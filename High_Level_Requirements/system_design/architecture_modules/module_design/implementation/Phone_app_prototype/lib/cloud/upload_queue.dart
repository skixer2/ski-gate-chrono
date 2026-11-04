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
///   DESYNCED  — 1.72: the user desynced the run (Synch switch OFF): the
///               cloud row was deleted, the local copy STAYS. The entry
///               remains as a DEAD marker — the sync worker's backfill
///               only enqueues runs with NO entry, so the dead entry is
///               exactly what prevents automatic re-upload — and it is
///               never picked up (dueEntries returns PENDING only).
///               Only an explicit Synch switch ON ([markPending]) revives
///               it (same client_run_uuid: the old row is gone, so the
///               server creates a NEW row on the next upload).
enum UploadStatus { pending, done, poisoned, desynced }

/// Monotonic-shrink guard: deliberately NOT applied to the queue — it is
/// allowed to shrink (DONE pruning, GDPR clear), unlike the merge index.
///
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
  // 1.70: server lock_version captured from the upload response — the
  // precondition for propagating a later rename (PUT + If-Match). Null for
  // entries uploaded before 1.70; the rename worker then GETs the run
  // fresh to learn the current lock_version.
  final int? lockVersion;

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
    this.lockVersion,
  });

  UploadEntry copyWith({
    UploadStatus? status,
    int? attempts,
    int? nextAttemptAtMs,
    int? runId,
    String? lastError,
    bool clearLastError = false,
    UploadRunType? runType,
    int? lockVersion,
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
        lockVersion: lockVersion ?? this.lockVersion,
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
        if (lockVersion != null) 'lockVersion': lockVersion,
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
        lockVersion: json['lockVersion'] as int?,
      );
}

/// 1.70: what a pending cloud rename points at — a plain device run
/// (localKey = run fileName) or a merged course (localKey =
/// [UploadQueue.mergedQueueKey] of its two source files).
enum RenameTarget { single, merged }

/// 1.70 rename propagation (F45 → cloud): one undelivered rename of a
/// cloud row. Entries exist ONLY while undelivered — success or a
/// permanent failure removes them, so an empty outbox is the steady
/// state. [lockVersion] is the caller's known cloud lock_version at
/// enqueue time (null only for runs uploaded before 1.70, where the
/// worker GETs the run fresh before the PUT).
class RenameEntry {
  final RenameTarget target;
  final String localKey;
  final int cloudRunId;
  final int? lockVersion;
  final String newName;
  final int attempts;
  final int nextAttemptAtMs; // epoch ms; 0 = due immediately
  final String? lastError;

  const RenameEntry({
    required this.target,
    required this.localKey,
    required this.cloudRunId,
    this.lockVersion,
    required this.newName,
    this.attempts = 0,
    this.nextAttemptAtMs = 0,
    this.lastError,
  });

  RenameEntry copyWith({
    int? attempts,
    int? nextAttemptAtMs,
    String? lastError,
  }) =>
      RenameEntry(
        target: target,
        localKey: localKey,
        cloudRunId: cloudRunId,
        lockVersion: lockVersion,
        newName: newName,
        attempts: attempts ?? this.attempts,
        nextAttemptAtMs: nextAttemptAtMs ?? this.nextAttemptAtMs,
        lastError: lastError ?? this.lastError,
      );

  Map<String, dynamic> toJson() => {
        'target': target.name,
        'localKey': localKey,
        'cloudRunId': cloudRunId,
        if (lockVersion != null) 'lockVersion': lockVersion,
        'newName': newName,
        'attempts': attempts,
        'nextAttemptAtMs': nextAttemptAtMs,
        if (lastError != null) 'lastError': lastError,
      };

  factory RenameEntry.fromJson(Map<String, dynamic> json) => RenameEntry(
        target: RenameTarget.values.firstWhere(
          (t) => t.name == json['target'],
          orElse: () => RenameTarget.single,
        ),
        localKey: json['localKey'] as String,
        cloudRunId: json['cloudRunId'] as int,
        lockVersion: json['lockVersion'] as int?,
        newName: json['newName'] as String,
        attempts: json['attempts'] as int? ?? 0,
        nextAttemptAtMs: json['nextAttemptAtMs'] as int? ?? 0,
        lastError: json['lastError'] as String?,
      );
}

class UploadQueue {
  static final UploadQueue instance = UploadQueue._();
  UploadQueue._();

  static const _queueFile = 'upload_queue.json';
  // 1.70: rename outbox lives as its own atomic file (same store
  // discipline). Kept separate from upload_queue.json so the existing
  // bare-list format stays compatible with entries written by 1.67–1.69.
  static const _renamesFile = 'rename_queue.json';

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

  /// 1.70: pending cloud renames (F45 propagation outbox).
  List<RenameEntry> _renames = [];

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
    // 1.70 rename outbox: a corrupt/missing file starts empty — the UI
    // source of truth (runs.json / merged_runs.json) always holds the
    // NEWEST name, so losing pending entries can at worst leave stale
    // names in the cloud; it can never corrupt local data.
    try {
      final dir = await getApplicationDocumentsDirectory();
      final f = File('${dir.path}/$_renamesFile');
      if (!await f.exists()) {
        _renames = [];
        return;
      }
      final list = jsonDecode(await f.readAsString()) as List<dynamic>;
      final out = <RenameEntry>[];
      for (final e in list) {
        try {
          out.add(RenameEntry.fromJson(e as Map<String, dynamic>));
        } catch (err) {
          debugPrint('[UploadQueue] skipping corrupt rename entry: $err');
        }
      }
      _renames = out;
    } catch (e) {
      debugPrint('[UploadQueue] rename queue load failed, starting empty: $e');
      _renames = [];
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

  /// 1.70: atomic persist of the rename outbox (temp + rename, same
  /// discipline as every other store file).
  Future<void> _persistRenames() async {
    final dir = await getApplicationDocumentsDirectory();
    final f = File('${dir.path}/$_renamesFile');
    final tmp = File('${f.path}.tmp');
    await tmp.writeAsString(
        jsonEncode(_renames.map((e) => e.toJson()).toList()),
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
  /// [lockVersion] (1.70) captures the server-owned lock_version from the
  /// upload response so a later rename can build its If-Match precondition
  /// without a fresh GET.
  Future<void> markDone(String uuid, int cloudRunId, {int? lockVersion}) async {
    _replace(uuid, (e) => e.copyWith(
          status: UploadStatus.done,
          runId: cloudRunId,
          lockVersion: lockVersion,
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

  /// 1.72: the run was explicitly DESYNCED (Synch switch OFF → cloud row
  /// deleted, local + device copies kept). The entry becomes a DEAD marker:
  /// kept so the worker's backfill never re-enqueues the run, but excluded
  /// from every upload pass. All upload progress (attempts / backoff /
  /// cloud id / lock_version) is dropped — the cloud relationship is over.
  Future<void> markDesynced(String uuid) async {
    String fileName = '';
    _replace(uuid, (e) {
      fileName = e.fileName;
      return UploadEntry(
        clientRunUuid: e.clientRunUuid,
        fileName: e.fileName,
        status: UploadStatus.desynced,
        queuedAtMs: e.queuedAtMs,
        runType: e.runType,
      );
    });
    await _persist();
    debugPrint('[UploadQueue] desynced $uuid ($fileName)');
  }

  /// 1.72: explicit re-sync (Synch switch OFF→ON) — reactivate a desynced
  /// / poisoned / backoff-scheduled entry for upload NOW. The client_run_uuid
  /// is deliberately KEPT: a desynced run's cloud row no longer exists, so
  /// the idempotency key is free again and POST /v1/runs creates a NEW row
  /// (JP: "same client_run_uuid is fine — the old row is gone").
  /// Call only for non-DONE entries — a DONE entry already IS synced.
  Future<void> markPending(String uuid) async {
    _replace(uuid, (e) => e.copyWith(
          status: UploadStatus.pending,
          attempts: 0,
          nextAttemptAtMs: 0,
          clearLastError: true,
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

  // ── 1.70 rename outbox (F45 → cloud propagation) ─────────────────

  /// Unmodifiable view for diagnostics.
  List<RenameEntry> get renames => List.unmodifiable(_renames);

  int get pendingRenameCount => _renames.length;

  /// Queue a cloud rename. A newer rename for the same localKey SUPERSEDES
  /// a still-pending one (the outbox always carries the freshest target
  /// name). Call only when the run/merge is confirmed uploaded (DONE
  /// queue entry with a cloud id) — otherwise the next upload carries
  /// the new name anyway.
  Future<void> enqueueRename({
    required RenameTarget target,
    required String localKey,
    required int cloudRunId,
    required String newName,
    int? lockVersion,
  }) async {
    _renames.removeWhere((r) => r.localKey == localKey);
    _renames.add(RenameEntry(
      target: target,
      localKey: localKey,
      cloudRunId: cloudRunId,
      lockVersion: lockVersion,
      newName: newName,
    ));
    await _persistRenames();
    debugPrint('[UploadQueue] rename queued for $localKey → "$newName" '
            '(cloud #$cloudRunId)');
  }

  /// Due renames (backoff elapsed). Entries have no status enum — an
  /// entry EXISTS only while its rename is undelivered.
  List<RenameEntry> dueRenames(DateTime now) => [
        for (final r in _renames)
          if (r.nextAttemptAtMs <= now.millisecondsSinceEpoch) r,
      ];

  /// Transient rename failure (network / 5xx / 403 activation):
  /// exponential backoff, same curve as uploads. [newName] guards against
  /// a supersede race — if the pending entry was replaced while this
  /// delivery was in flight, the retry bookkeeping is skipped (the new
  /// entry keeps its own fresh schedule).
  Future<void> markRenameRetry(String localKey, String newName,
      String error) async {
    for (int i = 0; i < _renames.length; i++) {
      final r = _renames[i];
      if (r.localKey != localKey || r.newName != newName) continue;
      final attempts = r.attempts + 1;
      _renames[i] = r.copyWith(
        attempts: attempts,
        nextAttemptAtMs: DateTime.now().millisecondsSinceEpoch +
            _backoffFor(attempts - 1).inMilliseconds,
        lastError: error.length > 300 ? error.substring(0, 300) : error,
      );
      await _persistRenames();
      return;
    }
  }

  /// Remove the EXACT delivered entry (identity comparison, not localKey):
  /// a rename queued mid-flight for the same run must survive the older
  /// entry's completion.
  Future<void> removeRenameEntry(RenameEntry entry) async {
    _renames.remove(entry);
    await _persistRenames();
  }

  /// Drop every pending rename for [localKey] — the delete leg of F46
  /// (nothing left to rename) and permanent rejection cleanup.
  Future<void> removeRenamesForKey(String localKey) async {
    _renames.removeWhere((r) => r.localKey == localKey);
    await _persistRenames();
  }

  /// Earliest scheduled rename retry (null = nothing waiting) — feeds the
  /// sync worker's retry timer alongside the upload schedule.
  DateTime? get nextRenameRetryAt {
    DateTime? out;
    for (final r in _renames) {
      if (r.nextAttemptAtMs == 0) continue;
      final t = DateTime.fromMillisecondsSinceEpoch(r.nextAttemptAtMs);
      if (out == null || t.isBefore(out)) out = t;
    }
    return out;
  }

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
  /// server-side; pending entries must not resurrect it). 1.70: pending
  /// renames die with it — the rows they point at no longer exist.
  Future<void> clear() async {
    _entries = [];
    _renames = [];
    await _persist();
    await _persistRenames();
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
