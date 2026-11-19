// lib/sync/auto_sync_worker.dart
// F69 - periodic auto-download for every paired arm, on any screen.
// Extracted from run_list_screen (the in-screen worker only ran while
// that screen was open). Same guards: sgc.autosync pref, app resumed,
// busy flag, 5-minute quiet window after each cycle, 45 s scan throttle.
// F70 CRC policy: retry once; identical bad payloads = deterministic
// store corruption -> delete-on-device via the F46 backlog; differing
// payloads = transient transfer corruption -> defer, never delete.
import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../ble/file_transfer.dart';
import '../ble/native_ble_downloader.dart';
import '../models/run.dart';
import '../processing/decompressor.dart';
import '../storage/device_delete_backlog.dart';
import '../storage/local_storage.dart';

DecompressResult _decompressForWorker(Uint8List data) {
  return Decompressor().decompressFull(data);
}

class AutoSyncWorker with WidgetsBindingObserver {
  static final AutoSyncWorker instance = AutoSyncWorker._();
  AutoSyncWorker._();

  static const _prefsAutoSync = 'sgc.autosync';
  static const _prefsPairedMacs = 'sgc.paired_macs';

  Timer? _timer;
  bool _busy = false;
  bool _resumed = true;
  DateTime? _quietUntil;
  DateTime? _lastScan;
  String? _lastSyncedMac;
  final Map<String, DateTime> _lastServed = {};
  List<String> _pairedMacs = const [];

  final ValueNotifier<String> status = ValueNotifier('');

  /// Set by main(): chains the cloud upload worker after each save (F43).
  void Function()? onSaved;

  /// 1.77: per-device name cache, filled once per cycle (see _syncDevice).
  final Map<String, String> _deviceNameCache = {};

  Future<void> init() async {
    final prefs = await SharedPreferences.getInstance();
    _pairedMacs = prefs.getStringList(_prefsPairedMacs) ?? const [];
    _resumed = WidgetsBinding.instance.lifecycleState !=
        AppLifecycleState.paused;
    WidgetsBinding.instance.addObserver(this);
    await DeviceDeleteBacklog.instance.load();
    _timer = Timer.periodic(
        const Duration(seconds: 15), (_) => _tick());
    debugPrint('[AUTOSYNC] worker armed: '
        + _pairedMacs.length.toString() + ' paired devices');
  }

  void didChangeAppLifecycleState(AppLifecycleState state) {
    _resumed = state != AppLifecycleState.paused;
    if (state == AppLifecycleState.paused) {
      // JP 2026-10-14: connections must be SHORT - defensive teardown.
      try {
        NativeBleDownloader().cancel();
        NativeBleDownloader().closeAll();
      } catch (_) {}
    }
  }

  Future<void> _tick() async {
    final prefs = await SharedPreferences.getInstance();
    final enabled = prefs.getBool(_prefsAutoSync) ?? true;
    if (!enabled || _busy || !_resumed) return;
    if (_quietUntil != null && DateTime.now().isBefore(_quietUntil!)) {
      return;
    }
    final now = DateTime.now();
    if (_lastScan != null &&
        now.difference(_lastScan!) < const Duration(seconds: 45)) {
      return;
    }
    if (_pairedMacs.isEmpty) {
      return;
    }
    _lastScan = now;
    _busy = true;
    try {
      await _scanAndSync().timeout(const Duration(minutes: 4), onTimeout: () => throw Exception('cycle watchdog (4 min)'));
    } catch (e) {
      debugPrint('[AUTOSYNC] cycle error: ' + e.toString());
      status.value = 'Auto: error - retry in 1 min';
      _quietUntil = DateTime.now().add(const Duration(minutes: 1));
    } finally {
      _busy = false;
    }
  }

  /// 10-second scan; round-robins paired arms so both get served
  /// (F69: one device per cycle, the other arm next cycle).
  Future<BluetoothDevice?> _quickScan() async {
    try { await FlutterBluePlus.stopScan(); } catch (_) {}
    final matched = <String, BluetoothDevice>{};
    final sub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        final id = r.device.remoteId.str;
        if (_pairedMacs.contains(id) && !matched.containsKey(id)) {
          matched[id] = r.device;
        }
      }
    });
    BluetoothDevice? dev;
    try {
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
      await Future.delayed(const Duration(seconds: 10));
    } catch (e) {
      debugPrint('[AUTOSYNC] startScan error: ' + e.toString());
    } finally {
      await sub.cancel();
      try { await FlutterBluePlus.stopScan(); } catch (_) {}
      if (matched.isNotEmpty) {
        // Starvation fix (DS consult 2026-11-19): serve the matched paired
        // arm with the oldest last-served time - no same-scan requirement,
        // so single-visible-arm scans can no longer starve the other arm.
        DateTime? oldest;
        for (final k in matched.keys) {
          final t = _lastServed[k];
          if (oldest == null || t == null || t.isBefore(oldest)) {
            oldest = t;
            dev = matched[k];
          }
        }
        final mac = dev!.remoteId.str;
        _lastServed[mac] = DateTime.now();
        _lastSyncedMac = mac;
      }
    }
    return dev;
  }

  Future<void> _scanAndSync() async {
    final dev = await _quickScan();
    if (dev == null) {
      status.value = 'Auto: no arm in range';
      return;
    }
    final mac = dev.remoteId.str;
    debugPrint('[AUTOSYNC] cycle target: ' + mac);
    status.value = 'Auto: syncing ' + mac + '...';
    await _syncDevice(mac);
    _quietUntil = DateTime.now().add(const Duration(minutes: 5));
    status.value = 'Auto: quiet 5 min';
  }

  Future<void> _syncDevice(String address) async {
    await DeviceDeleteBacklog.instance.load();
    final todo = DeviceDeleteBacklog.instance.forDevice(address);
    if (todo.isNotEmpty) {
      final ok = await NativeBleDownloader().deleteRuns(
        address: address,
        runIds: todo.map((e) => e.runId).toList(),
      );
      for (final e in todo) {
        if (ok.contains(e.runId)) {
          await DeviceDeleteBacklog.instance.remove(e);
        }
      }
    }
    // 1.77 (JP 2026-11-19): set the device clock BEFORE listing, so a run
    // recorded since the last manual connect is stamped with the real date
    // instead of inheriting a frozen timestamp (which filed new runs under
    // the wrong day). Failure is non-fatal - the download still proceeds.
    try {
      final epoch = await NativeBleDownloader().syncTime(address: address);
      debugPrint('[AUTOSYNC] time sync: device epoch=' + epoch.toString());
    } catch (e) {
      debugPrint('[AUTOSYNC] time sync failed: ' + e.toString());
    }
    // 1.77 (JP 2026-11-19): fetch the unit's real name once per cycle so
    // every run saved from autosync carries the recording device's name
    // (the save path used to hardcode 'SGC', losing the identity).
    try {
      final nm = await NativeBleDownloader().readDeviceName(address: address);
      if (nm.isNotEmpty) {
        _deviceNameCache[address] = nm;
        debugPrint('[AUTOSYNC] device name for ' + address + ': ' + nm);
      }
    } catch (e) {
      debugPrint('[AUTOSYNC] device name read failed: ' + e.toString());
    }
    status.value = 'Auto: reading ' + address + '...';
    final raw = await NativeBleDownloader().readRunList(address: address);
    if (raw.isEmpty) throw Exception('device returned 0-byte run list');
    final List<dynamic> list = jsonDecode(raw) as List<dynamic>;
    final deviceRuns = list
        .map((e) => RunMetadata.fromJson(e as Map<String, dynamic>))
        .toList();
    final local = await LocalStorage().listAll();
    // 1.77 (JP 2026-11-19): identity is PER DEVICE. Run ids restart at 0 on
    // every unit and both clocks can collide, so a device-blind key
    // (id, timestamp) can match a run downloaded from ANOTHER unit and
    // silently skip a genuine one (or re-download forever). Key on the
    // recording unit's MAC as well. Runs saved before 1.69 have a null
    // sourceMac and are matched on (id, timestamp) alone.
    final keys = local
        .map((r) => (r.sourceMac ?? '', r.id, r.timestamp))
        .toSet();
    final legacyKeys = local
        .where((r) => r.sourceMac == null)
        .map((r) => (r.id, r.timestamp))
        .toSet();
    final missing = deviceRuns
        .where((r) => !keys.contains((address, r.id, r.timestamp)) &&
            !legacyKeys.contains((r.id, r.timestamp)))
        .toList();
    debugPrint('[AUTOSYNC] ' + address + ': ' + deviceRuns.length.toString()
        + ' runs on device, ' + missing.length.toString() + ' missing');
    for (final run in missing) {
      await _downloadWithCrcPolicy(address, run);
    }
    try { await NativeBleDownloader().closeAll(); } catch (_) {}
  }

  /// F70: one download attempt; returns the run bytes when the CRC
  /// validates, null otherwise (transfer or store corruption).
  Future<Uint8List?> _attempt(String address, int runId, List<String> logTail) async {
    Uint8List? got;
    NativeBleDownloader.setListener(
      onLog: (m) => debugPrint('[AUTOSYNC][native] ' + m),
      onRunComplete: (id, timestamp, data) {
        if (id == runId) got = data;
      },
    );
    try {
      final res = await NativeBleDownloader().downloadRuns(
        address: address,
        runIds: [runId],
      );
      logTail.addAll(res.log);
      if (got == null && res.runs.isNotEmpty) got = res.runs.first.data;
    } finally {
      NativeBleDownloader.setListener(onLog: (m) => debugPrint('[AUTOSYNC][native] ' + m));
    }
    if (got == null) return null;
    if (!Decompressor().validateCRC(got!)) return null;
    return got;
  }

  /// F70 policy: attempt 1 fails -> attempt 2 (fresh transfer).
  /// Identical bad payloads = deterministic store corruption
  /// -> delete-on-device. Differing = transient transfer corruption
  /// -> defer, never delete.
  Future<void> _downloadWithCrcPolicy(String address, RunMetadata run) async {
    status.value = 'Auto: downloading run #' + run.id.toString() + '...';
    final logs1 = <String>[];
    final p1 = await _attempt(address, run.id, logs1);
    if (p1 == null) debugPrint('[AUTOSYNC] attempt1 failed, native log: ' + logs1.join(' | '));
    if (p1 != null) {
      await _saveRun(address, run.id, p1);
      return;
    }
    status.value = 'Auto: run #' + run.id.toString()
        + ' CRC failed - retry once';
    final logs2 = <String>[];
    final p2 = await _attempt(address, run.id, logs2);
    if (p2 == null) debugPrint('[AUTOSYNC] attempt2 failed, native log: ' + logs2.join(' | '));
    if (p2 != null) {
      await _saveRun(address, run.id, p2);
      return;
    }
    if (p1 != null && p2 != null) {
      final same = _bytesEqual(p1, p2);
      if (same) {
        debugPrint('[AUTOSYNC] run #' + run.id.toString()
            + ': identical bad payloads - deterministic store corruption,'
            + ' delete-on-device');
        await DeviceDeleteBacklog.instance.add(
          runId: run.id,
          fileName: 'run#' + run.id.toString() + '@' + address,
          mac: address,
        );
        status.value = 'Auto: run #' + run.id.toString()
            + ' corrupt on device - delete queued';
      } else {
        debugPrint('[AUTOSYNC] run #' + run.id.toString()
            + ': differing bad payloads - transient, deferred');
        status.value = 'Auto: run #' + run.id.toString()
            + ' transfer retry later';
      }
    }
  }

  Future<void> _saveRun(String address, int id, Uint8List data) async {
    debugPrint('[AUTOSYNC] save: decompressing run #' + id.toString() + ' (' + data.length.toString() + ' B)...');
    final decoded = await compute(_decompressForWorker, data)
        .timeout(const Duration(seconds: 20), onTimeout: () => throw Exception('decompress isolate timeout'));
    debugPrint('[AUTOSYNC] save: decompressed, storing...');
    await LocalStorage().save(
      runId: id,
      compressedData: data,
      result: decoded,
      // 1.77 (JP 2026-11-19): record WHO recorded the run - the unit's own
      // name plus its BLE MAC (sourceMac). 'SGC' is only a last-resort
      // fallback when the name characteristic could not be read.
      deviceName: _deviceNameCache[address] ?? 'SGC',
      sourceMac: address,
    );
    debugPrint('[AUTOSYNC] saved run #' + id.toString() + ' from ' + address);
    // P1 cloud: run landed locally - queue it for upload (F43).
    onSaved?.call();
  }

  bool _bytesEqual(Uint8List a, Uint8List b) {
    if (a.length != b.length) return false;
    for (int i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
