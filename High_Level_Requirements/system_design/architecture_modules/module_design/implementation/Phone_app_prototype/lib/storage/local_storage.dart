import 'dart:io';
import 'dart:convert';
import 'dart:typed_data';
import 'package:path_provider/path_provider.dart';
import 'package:flutter/foundation.dart';
import '../processing/decompressor.dart';
import '../models/run.dart';

/// Metadata for a locally saved run.
class SavedRun {
  final int id;
  final int timestamp;      // UTC unixtime from header
  final int frameCount;
  final double durationSec;
  final String side;        // "left" or "right"
  final String deviceName;
  final String fileName;    // e.g. "run_3_1719000000.bin"
  final DateTime savedAt;

  const SavedRun({
    required this.id,
    required this.timestamp,
    required this.frameCount,
    required this.durationSec,
    required this.side,
    required this.deviceName,
    required this.fileName,
    required this.savedAt,
  });

  Map<String, dynamic> toJson() => {
    'id': id,
    'timestamp': timestamp,
    'frameCount': frameCount,
    'durationSec': durationSec,
    'side': side,
    'deviceName': deviceName,
    'fileName': fileName,
    'savedAt': savedAt.toIso8601String(),
  };

  factory SavedRun.fromJson(Map<String, dynamic> json) => SavedRun(
    id: json['id'] as int,
    timestamp: json['timestamp'] as int,
    frameCount: json['frameCount'] as int,
    durationSec: (json['durationSec'] as num).toDouble(),
    side: json['side'] as String? ?? 'left',
    deviceName: json['deviceName'] as String? ?? 'SGC',
    fileName: json['fileName'] as String,
    savedAt: DateTime.parse(json['savedAt'] as String),
  );
}

/// Local storage for downloaded runs.
///
/// Saves raw compressed data to files + metadata to a JSON index.
class LocalStorage {
  static const _runsDir = 'sgc_runs';
  static const _indexFile = 'runs.json';

  Future<Directory> get _dir async {
    final appDir = await getApplicationDocumentsDirectory();
    final dir = Directory('${appDir.path}/$_runsDir');
    if (!await dir.exists()) await dir.create(recursive: true);
    return dir;
  }

  /// Save a downloaded run's raw compressed data + metadata.
  Future<SavedRun> save({
    required int runId,
    required Uint8List compressedData,
    required DecompressResult result,
    required String deviceName,
  }) async {
    final dir = await _dir;
    debugPrint('[LocalStorage] Storage dir: ${dir.path}');
    final fileName = 'run_${runId}_${result.header.startTimestamp}.bin';
    final file = File('${dir.path}/$fileName');
    await file.writeAsBytes(compressedData);

    final saved = SavedRun(
      id: runId,
      timestamp: result.header.startTimestamp,
      frameCount: result.frameCount,
      durationSec: result.totalDurationSec,
      side: result.header.armSide == 1 ? 'right' : 'left',
      deviceName: deviceName,
      fileName: fileName,
      savedAt: DateTime.now(),
    );

    // Update index.
    // Composite identity: a run is uniquely identified by (id, timestamp).
    // After a device HW reset, run ids are recycled but timestamps differ,
    // so replace only when BOTH id and timestamp match — otherwise keep the
    // older entry as a distinct physical run.
    final runs = await listAll();
    runs.removeWhere((r) => r.id == runId && r.timestamp == result.header.startTimestamp);
    runs.add(saved);
    runs.sort((a, b) => b.savedAt.compareTo(a.savedAt)); // newest first
    await _writeIndex(runs);

    debugPrint('[LocalStorage] Saved run #$runId → $fileName (${result.frameCount} frames, ${result.totalDurationSec.toStringAsFixed(1)}s)');
    return saved;
  }

  // ── Merged runs (dual-device blend, JP 2026-10-13) ──────────────

  /// Load merged-course entries (merged_runs.json).
  ///
  /// 2026-10-14 diagnosis: the old silent `catch (_) { return []; }` made a
  /// single transient read failure wipe the in-memory merge list for the
  /// whole instance life ("Merged Courses disappeared" bug). Now:
  /// - file missing → [] (normal first launch);
  /// - decode/read failure → quarantine the file as
  ///   merged_runs.corrupt.json (timestamped side-copy kept if the name
  ///   is already taken) + loud log, so on-disk history is never lost and
  ///   the failure is visible in logcat.
  Future<List<Map<String, dynamic>>> loadMerges() async {
    final dir = await _dir;
    final f = File('${dir.parent.path}/$_mergesFile');
    if (!await f.exists()) return [];
    try {
      final List<dynamic> list = jsonDecode(await f.readAsString());
      return list.cast<Map<String, dynamic>>();
    } catch (e, st) {
      final stamp = DateTime.now().millisecondsSinceEpoch;
      final corrupt = File('${dir.parent.path}/merged_runs.corrupt.json');
      try {
        if (await corrupt.exists()) {
          await f.copy('${dir.parent.path}/merged_runs.corrupt.$stamp.json');
        } else {
          await f.copy(corrupt.path);
        }
      } catch (_) {}
      debugPrint('[LocalStorage] MERGES FILE CORRUPT — quarantined '
          '(${corrupt.path}); starting from empty list. Error: $e\n$st');
      return [];
    }
  }

  /// Persist merged-course entries.
  ///
  /// Hardening 2026-10: atomic write (temp + rename) — a kill/crash
  /// mid-write can no longer truncate merged_runs.json. Monotonic guard:
  /// a save that would persist FEWER entries than currently on disk is
  /// rejected loudly (protects against in-memory races silently deleting
  /// known merges). Pass [allowShrink: true] only for explicit user deletion.
  Future<void> saveMerges(List<Map<String, dynamic>> merges,
      {bool allowShrink = false}) async {
    final dir = await _dir;
    final f = File('${dir.parent.path}/$_mergesFile');
    if (!allowShrink) {
      try {
        if (await f.exists()) {
          final List<dynamic> onDisk = jsonDecode(await f.readAsString());
          if (merges.length < onDisk.length) {
            debugPrint('[LocalStorage] saveMerges REJECTED: would shrink '
                '${onDisk.length} → ${merges.length} on-disk merges '
                '(use allowShrink for explicit deletion)');
            return;
          }
        }
      } catch (_) {
        // Disk unreadable/corrupt: proceed — the write replaces it.
      }
    }
    final tmp = File('${f.path}.tmp');
    await tmp.writeAsString(jsonEncode(merges), flush: true);
    await tmp.rename(f.path);
  }

  static const _mergesFile = 'merged_runs.json';

  /// List all saved runs (newest first).
  Future<List<SavedRun>> listAll() async {
    try {
      final dir = await _dir;
      final indexFile = File('${dir.path}/$_indexFile');
      if (!await indexFile.exists()) return [];
      final json = await indexFile.readAsString();
      final list = jsonDecode(json) as List<dynamic>;
      // V1.36: per-entry guard — one corrupt/legacy entry must not nuke
      // the whole list (previously a single fromJson throw returned []).
      final out = <SavedRun>[];
      for (final e in list) {
        try {
          out.add(SavedRun.fromJson(e as Map<String, dynamic>));
        } catch (err) {
          debugPrint('[LocalStorage] skipping corrupt index entry: $err');
        }
      }
      return out;
    } catch (e) {
      debugPrint('[LocalStorage] Error reading index: $e');
      return [];
    }
  }

  /// Load raw compressed data for a saved run.
  Future<Uint8List?> load(String fileName) async {
    try {
      final dir = await _dir;
      final file = File('${dir.path}/$fileName');
      if (!await file.exists()) return null;
      return await file.readAsBytes();
    } catch (e) {
      debugPrint('[LocalStorage] Error loading $fileName: $e');
      return null;
    }
  }

  /// Delete a saved run.
  Future<void> delete(SavedRun run) async {
    final dir = await _dir;
    final file = File('${dir.path}/${run.fileName}');
    if (await file.exists()) await file.delete();
    final runs = await listAll();
    runs.removeWhere((r) => r.fileName == run.fileName);
    await _writeIndex(runs);
  }

  Future<void> _writeIndex(List<SavedRun> runs) async {
    final dir = await _dir;
    final indexFile = File('${dir.path}/$_indexFile');
    await indexFile.writeAsString(jsonEncode(runs.map((r) => r.toJson()).toList()));
  }
}
