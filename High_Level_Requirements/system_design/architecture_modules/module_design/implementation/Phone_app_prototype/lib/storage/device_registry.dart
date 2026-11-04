import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// JP naming scheme v2 (1.69): persistent device-letter registry.
///
/// Every distinct SGC device ever seen by this phone gets a stable letter:
/// the first MAC ever registered becomes "A", the next new one "B", and so
/// on (A..Z, rolling over to two-letter codes AA, AB, … if a 27th device
/// ever appears). The letter is the backbone of the default run name
/// ("A#32") so labels stay meaningful once multiple devices exist.
///
/// Storage: `device_registry.json` next to the other SGC files, atomic
/// temp+rename writes (same discipline as merged_runs.json / upload_queue.json).
/// Entry schema: {"letter": "A", "mac": "AA:BB:CC:DD:EE:FF",
/// "assignedAt": "<UTC ISO8601>"}.
///
/// Migration: when the file does not exist yet (first launch on 1.69), the
/// currently-paired MACs (SharedPreferences `sgc.paired_macs`, which every
/// manual pairing has fed in insertion order since 1.58) are assigned
/// letters in stored order starting at A. Because only one device exists
/// today, that single MAC becomes "A" — exactly what the pre-1.69 A-fallback
/// assumes for legacy runs.
///
/// Re-pairing a known MAC keeps its letter forever (registry is the source
/// of truth; paired_macs is only the migration seed). New MACs are
/// registered at pair time or save time — whichever happens first.
class DeviceRegistry {
  static final DeviceRegistry instance = DeviceRegistry._();
  DeviceRegistry._();

  static const _fileName = 'device_registry.json';
  static const _prefsPairedMacs = 'sgc.paired_macs';

  /// In-memory registry rows, insertion-ordered (A first). Values:
  /// letter / mac / assignedAt.
  final List<Map<String, String>> _devices = [];

  bool _loaded = false;

  /// Single-flight load: concurrent callers share one disk read + at most
  /// one migration (same pattern as SyncWorker's single-flight pass).
  Future<void>? _loading;

  /// Serialized assignment queue: two first-sight MACs in the same event
  /// loop drain can never race into the same letter (the check-then-act
  /// on [_devices] must not interleave).
  Future<void> _opQueue = Future<void>.value();

  Future<File> _file() async {
    final dir = await getApplicationDocumentsDirectory();
    return File('${dir.path}/$_fileName');
  }

  /// Letter for a MAC — returns the existing letter for a known device or
  /// assigns the next free one (persisted immediately).
  Future<String> letterFor(String mac) {
    final op = _opQueue.then((_) => _letterForLocked(mac));
    // Keep the chain alive even if this operation fails.
    _opQueue = op.then((_) {}, onError: (Object _) {});
    return op;
  }

  /// Letter with legacy fallback: null/empty MAC (runs saved before 1.69)
  /// resolves to "A" without touching the registry — all pre-1.69 runs
  /// came from the one device, which migrates to A.
  Future<String> letterForMac(String? mac) async {
    if (mac == null || mac.isEmpty) return 'A';
    return letterFor(mac);
  }

  Future<String> _letterForLocked(String mac) async {
    await _ensureLoaded();
    for (final d in _devices) {
      if (d['mac'] == mac) return d['letter']!;
    }
    final letter = _nextFreeLetter();
    _devices.add({
      'letter': letter,
      'mac': mac,
      'assignedAt': DateTime.now().toUtc().toIso8601String(),
    });
    try {
      await _persist();
    } catch (e) {
      // Letter stays valid in memory this session; a persistence failure
      // is loud so a letter shift after restart gets noticed, not guessed.
      debugPrint('[DeviceRegistry] ⚠️ persist failed for $mac → $letter: $e');
    }
    debugPrint('[DeviceRegistry] assigned letter $letter → $mac '
        '(${_devices.length} device(s) registered)');
    return letter;
  }

  Future<void> _ensureLoaded() {
    return _loading ??= _load();
  }

  Future<void> _load() async {
    try {
      final f = await _file();
      if (await f.exists()) {
        final obj = jsonDecode(await f.readAsString()) as Map<String, dynamic>;
        final list = obj['devices'] as List<dynamic>? ?? const [];
        for (final e in list) {
          if (e is! Map<String, dynamic>) continue;
          final letter = e['letter'] as String?;
          final mac = e['mac'] as String?;
          if (letter == null || letter.isEmpty || mac == null || mac.isEmpty) {
            continue;
          }
          _devices.add({
            'letter': letter,
            'mac': mac,
            'assignedAt': e['assignedAt'] as String? ?? '',
          });
        }
        debugPrint('[DeviceRegistry] loaded ${_devices.length} device(s) '
            'from $_fileName');
      } else {
        // First launch on 1.69: seed from the pairing history so the one
        // existing device lands on A deterministically.
        await _migrateFromPairedMacs();
      }
    } catch (e) {
      debugPrint('[DeviceRegistry] load failed: $e');
      // Never re-migrate over partially loaded rows (would double-assign).
      if (_devices.isEmpty) {
        try {
          await _migrateFromPairedMacs();
        } catch (e2) {
          debugPrint('[DeviceRegistry] migration failed too: $e2');
        }
      }
    }
    _loaded = true;
  }

  /// Assign letters to all currently-paired MACs in stored order, A first.
  /// `sgc.paired_macs` is a string list fed by every manual pairing since
  /// 1.58 in insertion order, so the oldest pairing becomes A.
  Future<void> _migrateFromPairedMacs() async {
    final prefs = await SharedPreferences.getInstance();
    final macs = prefs.getStringList(_prefsPairedMacs) ?? const <String>[];
    var assigned = 0;
    for (final mac in macs) {
      if (mac.isEmpty) continue;
      if (_devices.any((d) => d['mac'] == mac)) continue;
      _devices.add({
        'letter': _letterAt(assigned),
        'mac': mac,
        'assignedAt': DateTime.now().toUtc().toIso8601String(),
      });
      assigned++;
    }
    if (_devices.isNotEmpty) {
      await _persist();
      debugPrint('[DeviceRegistry] migrated ${_devices.length} paired '
          'mac(s): ${_devices.map((d) => '${d['letter']}=${d['mac']}').join(', ')}');
    }
  }

  /// First letter code not yet in use: A..Z, then AA, AB, … (rollover for
  /// the theoretical >26-device fleet).
  String _nextFreeLetter() {
    for (int i = 0; i < 26 * 26; i++) {
      final candidate = _letterAt(i);
      if (!_devices.any((d) => d['letter'] == candidate)) return candidate;
    }
    return 'ZZ'; // unreachable in practice: 702 devices
  }

  /// 0 → A, 25 → Z, 26 → AA, 27 → AB, …
  String _letterAt(int n) {
    if (n < 26) return String.fromCharCode(65 + n);
    final first = n ~/ 26 - 1;
    final second = n % 26;
    return '${String.fromCharCode(65 + first)}'
        '${String.fromCharCode(65 + second)}';
  }

  Future<void> _persist() async {
    final f = await _file();
    final tmp = File('${f.path}.tmp');
    await tmp.writeAsString(jsonEncode({'devices': _devices}), flush: true);
    await tmp.rename(f.path);
  }
}
