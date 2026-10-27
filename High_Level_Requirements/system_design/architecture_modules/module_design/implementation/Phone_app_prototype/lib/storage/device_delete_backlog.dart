import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// 1.71 device-delete backlog (F46 v2, JP 2026-10-27): durable record of
/// runs whose DEVICE copy still has to be deleted ("d <id>" console
/// command, FW 0.22.x) but couldn't be sent live — the device was not
/// connected, was mid-recording (E 5), or mid-transfer (E 4).
///
/// Applied on EVERY connect BEFORE any listing/download batch (order is
/// critical: a backlogged run is missing locally, so an unapplied entry
/// would make the very next auto-sync re-download it). 'd' is idempotent
/// on the device — an unknown run id answers "OK already gone" — so
/// applying an entry always resolves it: success OR already-gone removes
/// it; busy/transport failures keep it for the next connect.
///
/// Storage: one JSON file (`device_delete_backlog.json`), atomic
/// temp+rename writes, same discipline as upload_queue.json /
/// rename_queue.json. Entries are deduped on (mac, runId).
class DeviceDeleteEntry {
  final int runId; // stable device run id (= SavedRun.id)
  final String fileName; // local file the run had (diagnostics only)
  // BLE MAC of the device holding the copy (SavedRun.sourceMac). Null for
  // pre-1.69 runs — single-device era, the entry applies on any connect.
  final String? mac;

  const DeviceDeleteEntry({
    required this.runId,
    required this.fileName,
    this.mac,
  });

  Map<String, dynamic> toJson() => {
        'runId': runId,
        'fileName': fileName,
        if (mac != null) 'mac': mac,
      };

  factory DeviceDeleteEntry.fromJson(Map<String, dynamic> json) =>
      DeviceDeleteEntry(
        runId: (json['runId'] as num).toInt(),
        fileName: json['fileName'] as String? ?? '',
        mac: json['mac'] as String?,
      );
}

class DeviceDeleteBacklog {
  static final DeviceDeleteBacklog instance = DeviceDeleteBacklog._();
  DeviceDeleteBacklog._();

  static const _file = 'device_delete_backlog.json';

  final List<DeviceDeleteEntry> _entries = [];
  bool _loaded = false;

  /// Unmodifiable view for diagnostics.
  List<DeviceDeleteEntry> get entries => List.unmodifiable(_entries);
  int get count => _entries.length;
  bool get loaded => _loaded;

  /// Entries eligible for a connect to device [mac] (null = any device).
  List<DeviceDeleteEntry> forDevice(String? mac) => [
        for (final e in _entries)
          if (e.mac == null || e.mac == mac) e,
      ];

  Future<void> load() async {
    if (_loaded) return;
    _loaded = true;
    try {
      final dir = await getApplicationDocumentsDirectory();
      final f = File('${dir.path}/$_file');
      if (!await f.exists()) return;
      final list = jsonDecode(await f.readAsString()) as List<dynamic>;
      for (final e in list) {
        try {
          _entries.add(DeviceDeleteEntry.fromJson(e as Map<String, dynamic>));
        } catch (err) {
          debugPrint('[DeleteBacklog] skipping corrupt entry: $err');
        }
      }
    } catch (e) {
      debugPrint('[DeleteBacklog] load failed, starting empty: $e');
      _entries.clear();
    }
    if (_entries.isNotEmpty) {
      debugPrint('[DeleteBacklog] ${_entries.length} device deletion(s) pending');
    }
  }

  /// Queue a device deletion. Deduped on (mac, runId): deleting the same
  /// run twice (double-tap, retry after busy) never stacks entries.
  Future<void> add({
    required int runId,
    required String fileName,
    String? mac,
  }) async {
    final dup = _entries.any((e) => e.runId == runId && e.mac == mac);
    if (dup) return;
    _entries.add(DeviceDeleteEntry(runId: runId, fileName: fileName, mac: mac));
    await _persist();
    debugPrint('[DeleteBacklog] queued device delete #$runId '
        '(${_entries.length} pending)');
  }

  /// Remove the EXACT entry after the device confirmed the delete (or
  /// answered "already gone" — idempotent).
  Future<void> remove(DeviceDeleteEntry entry) async {
    if (_entries.remove(entry)) await _persist();
  }

  Future<void> _persist() async {
    final dir = await getApplicationDocumentsDirectory();
    final f = File('${dir.path}/$_file');
    final tmp = File('${f.path}.tmp');
    await tmp.writeAsString(
        jsonEncode(_entries.map((e) => e.toJson()).toList()),
        flush: true);
    await tmp.rename(f.path);
  }
}
