import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:intl/intl.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../../models/gate_side.dart';
import '../../ble/ble_manager.dart' hide ScanResult;
import '../../ble/sgc_service.dart';
import '../../ble/native_ble_downloader.dart';
import '../../ble/file_transfer.dart';
import '../../processing/decompressor.dart';
import '../../processing/run_merger.dart';
import '../../storage/local_storage.dart';
import '../../storage/device_delete_backlog.dart';
import '../../storage/device_registry.dart';
import '../../config/app_version.dart';
import '../../cloud/sync_worker.dart';
import '../../cloud/upload_queue.dart';
import '../../cloud/api_client.dart';
import '../../models/device_config.dart';
import 'run_detail_screen.dart';
import 'merged_run_screen.dart';
import 'cloud_runs_screen.dart';

/// Top-level decompression entry point for compute() — runs in a background
/// isolate so the BLE platform channel is not blocked during multi-run
/// downloads.  Without this, decompression of run N blocks the main thread,
/// FlutterBluePlus can't process BLE callbacks, and the next run's
/// setNotifyValue races into GATT_ERROR 133.
DecompressResult _decompressInIsolate(Uint8List data) {
  return Decompressor().decompressFull(data);
}

class RunListScreen extends StatefulWidget {
  const RunListScreen({super.key});
  @override
  State<RunListScreen> createState() => _RunListScreenState();
}

class _RunListScreenState extends State<RunListScreen>
    with WidgetsBindingObserver {
  final _ble = BLEManager();
  final _storage = LocalStorage();
  SGCService? _sgc;
  BluetoothDevice? _device;   // V1.22: kept for per-run reconnect (Phase 2)
  DeviceConfig? _config;
  int? _runCount;   // v1.50: null = read FAILED (never a fake 0)
  // ABC5 (F-snow-kit): null = unknown (0x7F / read failed).
  int? _batteryPct;
  bool _batteryCharging = false;

  // 1.64 (JP 2026-10-23): auto-disconnect after 1 min of idle connection.
  Timer? _idleDisconnectTimer;

  /// (Re)arm the 1-min idle auto-disconnect. Called on connect-complete,
  /// download-complete — any device activity. While a transfer is in
  /// flight the timer re-arms itself instead of firing.
  void _resetIdleDisconnectTimer() {
    _idleDisconnectTimer?.cancel();
    _idleDisconnectTimer = Timer(const Duration(minutes: 1), () {
      if (!_ble.isConnected) return;
      if (_isDownloading || _isConnecting) {
        debugPrint('[SGC] idle timer fired mid-activity — re-arming');
        _resetIdleDisconnectTimer();
        return;
      }
      debugPrint('[SGC] idle 60 s — auto-disconnect (battery discipline)');
      _disconnect();
    });
  }

  void _cancelIdleDisconnectTimer() {
    _idleDisconnectTimer?.cancel();
    _idleDisconnectTimer = null;
  }
  final List<String> _setupWarnings = [];   // v1.50 (audit A6): surfaced, never silent
  bool _isConnecting = false;
  bool _isScanning = false;
  bool _isDownloading = false;
  // 1.71 (F46 v2): device-delete backlog size — surfaced in the sync status
  // line ("N device deletions pending").
  int _backlogPending = 0;
  // 1.71: half-memory sync warning. ABC7 used-pct + device listing are the
  // inputs; computed on every listing refresh, dismissed per session.
  int? _flashUsedPct;      // null = unknown (read failed / FW too old)
  int _unsyncedRuns = 0;
  bool _syncWarningActive = false;
  bool _syncWarningDismissed = false;
  String _downloadStatus = '';  // progress text for UI
  int _downloadCurrent = 0;    // current run being downloaded
  int _downloadTotal = 0;      // total runs to download
  int _downloadBytes = 0;      // bytes received for current run
  int _downloadRunSize = 0;    // expected size of current run
  List<SavedRun> _localRuns = [];
  List<RunMetadata> _deviceRuns = []; // runs present on the connected device
  StreamSubscription<bool>? _connSub;

  // ── Auto-sync (JP directive 2026-10-13 16:39) ─────────────────────
  // Phone automatically: connect to paired SGC devices → download
  // missing runs → disconnect → merge corresponding runs → stay quiet
  // 5 minutes even if devices are still available.
  Timer? _autoTimer;
  DateTime? _autoQuietUntil;
  String _autoStatus = '';
  bool _autoBusy = false;
  bool _autoEnabled = true;
  bool _appResumed = true;
  DateTime? _lastAutoScan;   // Android scan-throttle guard
  Set<String> _pairedMacs = {};
  // 1.69 day pager: the ONE local day currently displayed (YYYY-MM-DD key).
  // null = nothing to show yet; the first load snaps to the newest day
  // having runs.
  String? _displayDay;
  // 1.69 display-only default names, recomputed whenever the run set
  // changes: fileName → "A#32"; merge key (aFile|bFile) → "A#32 + A#33".
  Map<String, String> _defaultNames = {};
  Map<String, String> _mergeDefaultNames = {};
  List<Map<String, dynamic>> _merges = [];
  final Map<String, List<Map<String, dynamic>>> _gateCache = {};
  static const _prefsAutoSync = 'sgc.autosync';
  static const _prefsPairedMacs = 'sgc.paired_macs';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _loadLocalRuns();
    _loadAutoPrefs();
    _autoTimer = Timer.periodic(
        const Duration(seconds: 15), (_) => _autoTick());
    // React to unexpected BLE drops so the UI is never stuck "connected".
    _connSub = _ble.connectionStream.listen(_onConnectionChanged);
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _appResumed = state == AppLifecycleState.resumed;
    if (state == AppLifecycleState.paused) {
      // JP 2026-10-14: connections must be SHORT. If the app is backgrounded
      // mid-cycle, Android can freeze the process and leak an open GATT
      // (device green LED held overnight). Tear down defensively.
      try {
        NativeBleDownloader().cancel();
        NativeBleDownloader().closeAll();
      } catch (_) {}
    }
  }

  void _onConnectionChanged(bool connected) {
    if (connected) return;
    // V1.23: during a download, disconnects are either INTENTIONAL (Phase-2
    // per-run reconnect) or handled by the transfer's own resume loop.
    // Clearing state here nuked the screen mid-download — runs were saved
    // but nothing displayed until app relaunch.
    if (_isDownloading) {
      debugPrint('[SGC] BLE disconnected during download — transfer flow manages it');
      return;
    }
    debugPrint('[SGC] BLE disconnected — clearing connected UI state');
    _cancelIdleDisconnectTimer();
    if (!mounted) return;
    setState(() {
      _sgc = null;
      _config = null;
      _runCount = null;
      _batteryPct = null;
      _batteryCharging = false;
      _flashUsedPct = null;
      _deviceRuns = [];
      _isConnecting = false;
      _isDownloading = false;
    });
  }

  @override
  void dispose() {
    _cancelIdleDisconnectTimer();
    _autoTimer?.cancel();
    WidgetsBinding.instance.removeObserver(this);
    _connSub?.cancel();
    FlutterBluePlus.stopScan();  // clean up if tab switched mid-scan
    _ble.dispose();
    super.dispose();
  }

  Future<void> _loadAutoPrefs() async {
    final prefs = await SharedPreferences.getInstance();
    _autoEnabled = prefs.getBool(_prefsAutoSync) ?? true;
    _pairedMacs = (prefs.getStringList(_prefsPairedMacs) ?? []).toSet();
    _merges = await _storage.loadMerges();
    // Runs may not be loaded yet (initState race) — refresh names only.
    await _refreshDerivedState(_localRuns, clampDay: false);
    if (mounted) setState(() {});
  }

  Future<void> _rememberPaired(String mac) async {
    if (_pairedMacs.add(mac)) {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setStringList(_prefsPairedMacs, _pairedMacs.toList());
      debugPrint('[AUTO] paired device: $mac (${_pairedMacs.length} total)');
    }
  }

  String _quietStatus() {
    final left = _autoQuietUntil!.difference(DateTime.now());
    final m = left.inMinutes;
    final s = left.inSeconds % 60;
    return 'Auto: quiet ${m}:${s.toString().padLeft(2, '0')}';
  }

  Future<void> _autoTick() async {
    if (!_autoEnabled || _autoBusy || _isDownloading || _isScanning ||
        _isConnecting || _ble.isConnected || !_appResumed) {
      return;
    }
    if (_autoQuietUntil != null &&
        DateTime.now().isBefore(_autoQuietUntil!)) {
      if (mounted && _autoStatus != _quietStatus()) {
        setState(() => _autoStatus = _quietStatus());
      }
      return;
    }
    // Android silently throttles BLE scans started >5×/30 s (results
    // come back empty forever if we keep scanning). Scan at most every
    // 45 s — and never while a manual scan is active.
    final now = DateTime.now();
    if (_lastAutoScan != null &&
        now.difference(_lastAutoScan!) < const Duration(seconds: 45)) {
      return;
    }
    _lastAutoScan = now;
    _autoBusy = true;
    try {
      final dev = await _autoQuickScan();
      if (dev == null) {
        if (mounted) setState(() => _autoStatus = 'Auto: scanning…');
        return;
      }
      await _autoCycle(dev);
    } catch (e) {
      debugPrint('[AUTO] cycle error: $e');
      if (mounted) setState(() => _autoStatus = 'Auto: error — retry in 1 min');
      // Back off after errors instead of hammering a sick link.
      _autoQuietUntil = DateTime.now().add(const Duration(minutes: 1));
    } finally {
      _autoBusy = false;
    }
  }

  /// 10-second scan; returns the first PAIRED device seen (MAC match).
  Future<BluetoothDevice?> _autoQuickScan() async {
    try { await FlutterBluePlus.stopScan(); } catch (_) {}
    BluetoothDevice? found;
    int seen = 0;
    final sub = FlutterBluePlus.scanResults.listen((results) {
      seen = results.length;
      for (final r in results) {
        debugPrint('[AUTO] adv ${r.device.remoteId.str} '
            'paired=${_pairedMacs.contains(r.device.remoteId.str)}');
        if (_pairedMacs.contains(r.device.remoteId.str)) {
          found ??= r.device;
        }
      }
    });
    try {
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 10),
      );
      // FBP 1.32: startScan's future completes when the scan STARTS, and
      // isScanning races (may still read false). Hold the window open for
      // the scan duration — the listener accumulates matches meanwhile;
      // the scan stops itself at its timeout.
      await Future.delayed(const Duration(seconds: 10));
    } catch (e) {
      debugPrint('[AUTO] startScan error: $e');
    } finally {
      debugPrint('[AUTO] scan done: seen=$seen '
          'matched=${found != null} paired=[${_pairedMacs.join(',')}]');
      await sub.cancel();
      try { await FlutterBluePlus.stopScan(); } catch (_) {}
    }
    return found;
  }

  /// One full auto cycle: read listing → download missing → merge → quiet.
  Future<void> _autoCycle(BluetoothDevice dev) async {
    final address = dev.remoteId.str;
    // 1.71 (F46 v2): apply the device-delete backlog BEFORE the listing —
    // a backlogged run is missing locally and would otherwise be
    // re-downloaded by this very cycle. ORDER IS CRITICAL.
    await _applyBacklogNative(address);
    if (mounted) setState(() => _autoStatus = 'Auto: reading $address…');
    final raw = await NativeBleDownloader().readRunList(address: address);
    if (raw.isEmpty) throw Exception('device returned 0-byte run list');
    final List<dynamic> list = jsonDecode(raw) as List<dynamic>;
    final deviceRuns = list
        .map((e) => RunMetadata.fromJson(e as Map<String, dynamic>))
        .toList();
    await _loadLocalRuns();
    final localKeys = _localRuns.map((r) => (r.id, r.timestamp)).toSet();
    final missing = deviceRuns
        .where((r) => !localKeys.contains((r.id, r.timestamp)))
        .toList();
    debugPrint('[AUTO] $address: ${deviceRuns.length} runs on device, '
        '${missing.length} missing');

    if (missing.isNotEmpty) {
      if (mounted) {
        setState(() {
          _isDownloading = true;
          _downloadStatus = 'Auto: downloading ${missing.length} runs…';
          _downloadCurrent = 0;
          _downloadTotal = missing.length;
        });
      }
      int saved = 0;
      try {
        NativeBleDownloader.setListener(
          onLog: (msg) {
            if (mounted) setState(() => _downloadStatus = msg);
          },
          onProgress: (runId, bytes, expected) {
            if (mounted) {
              setState(() {
                _downloadStatus = 'Auto FT run #$runId';
                _downloadBytes = bytes;
                _downloadRunSize = expected;
              });
            }
          },
          onRunComplete: (runId, timestamp, data) async {
            if (!Decompressor().validateCRC(data)) {
              debugPrint('[AUTO] ⚠️ CRC FAILED run #$runId');
              return;
            }
            try {
              final decoded = await compute(_decompressInIsolate, data);
              await _storage.save(
                runId: runId,
                compressedData: data,
                result: decoded,
                deviceName: 'SGC',
                sourceMac: address, // 1.69: stamp recording device
              );
              saved++;
              await _loadLocalRuns();
              // P1 cloud: run landed locally → queue it for upload.
              // ignore: unawaited_futures
              SyncWorker.instance.kick('auto-sync run saved');
            } catch (e) {
              debugPrint('[AUTO] save error run #$runId: $e');
            }
          },
        );
        final result = await NativeBleDownloader().downloadRuns(
          address: address,
          runIds: missing.map((r) => r.id).toList(),
        );
        for (final line in result.log) {
          debugPrint('[AUTO-NATIVE] $line');
        }
      } finally {
        NativeBleDownloader.setListener();
        if (mounted) {
          setState(() {
            _isDownloading = false;
            _downloadStatus = '';
          });
        }
      }
      debugPrint('[AUTO] saved $saved/${missing.length} runs');
    }
    _resetIdleDisconnectTimer();   // 1.64: transfer done -> idle clock restarts

    await _runMergePass();

    // JP 2026-10-14: no GATT client may outlive the cycle — clears any
    // straggler managers (leaked holders kept the device connected).
    try { await NativeBleDownloader().closeAll(); } catch (_) {}

    // JP: quiet 5 minutes — even if devices are still available.
    _autoQuietUntil = DateTime.now().add(const Duration(minutes: 5));
    if (mounted) setState(() => _autoStatus = _quietStatus());
  }

  /// Pair complementary saved runs and persist the merged courses.
  Future<void> _runMergePass() async {
    // Race guard (2026-10-14 diagnosis): if the in-memory list is empty
    // but a valid file exists on disk, reload it before pairing —
    // otherwise this pass pairs against an empty baseline (the exact
    // state that made the Merged Courses section vanish) and could
    // duplicate already-known merges.
    if (_merges.isEmpty) {
      final onDisk = await _storage.loadMerges();
      if (onDisk.isNotEmpty) {
        _merges = onDisk;
        debugPrint('[AUTO] race guard: reloaded ${onDisk.length} '
            'merges from disk before pairing');
        if (mounted) setState(() {});
      }
    }
    final merged = [..._merges];
    final inMerge = <String>{};
    for (final m in merged) {
      inMerge.add(m['aFile'] as String);
      inMerge.add(m['bFile'] as String);
    }
    final cand =
        _localRuns.where((r) => !inMerge.contains(r.fileName)).toList();
    if (cand.length < 2) return;

    for (int i = 0; i < cand.length; i++) {
      for (int j = i + 1; j < cand.length; j++) {
        final a = cand[i], b = cand[j];
        final maxF =
            a.frameCount > b.frameCount ? a.frameCount : b.frameCount;
        if (maxF == 0 ||
            (a.frameCount - b.frameCount).abs() / maxF > 0.02) {
          continue;
        }
        final ga = await _gatesFor(a.fileName);
        final gb = await _gatesFor(b.fileName);
        if (ga == null || gb == null) continue;
        if (!RunMerger.isComplementaryPair(
            framesA: a.frameCount,
            framesB: b.frameCount,
            gatesA: ga,
            gatesB: gb)) {
          continue;
        }
        // Full merge in an isolate: confirmed-gate union + averaged baro
        // profile for the charts (JP 2026-10-14).
        final dataA = await _storage.load(a.fileName);
        final dataB = await _storage.load(b.fileName);
        if (dataA == null || dataB == null) continue;
        final mergedPair = await compute(RunMerger.mergePairForIsolate, {
          'aData': dataA,
          'bData': dataB,
        });
        if (mergedPair['ok'] != true) continue;
        final gates = (mergedPair['gates'] as List).cast<Map<String, dynamic>>();
        if (gates.length < 6) continue;
        debugPrint('[AUTO] merged ${a.fileName} + ${b.fileName}: '
            '${gates.length} gates');
        merged.add({
          'aFile': a.fileName,
          'bFile': b.fileName,
          'aId': a.id,
          'bId': b.id,
          'gates': gates,
          'baro': mergedPair['baro'],
          'created': DateTime.now().toIso8601String(),
        });
        inMerge.add(a.fileName);
        inMerge.add(b.fileName);
      }
    }
    bool newMerges = false;
    if (merged.length != _merges.length) {
      await _storage.saveMerges(merged);
      _merges = merged;
      newMerges = true;
      // 1.69: recompute the composed merge labels ("A#32 + A#33") so the
      // new entries never flash the legacy "Run #a + #b" fallback.
      await _refreshDerivedState(_localRuns, clampDay: false);
      if (mounted) setState(() {});
    }
    // Backfill: entries created before 1.59 lack the averaged baro profile.
    final baroBackfilled = await _backfillMergedBaro();

    // 1.68: new (or baro-backfilled) merges go to the cloud too — upload
    // everything, singles AND merged, as distinct types (JP 2026-10-26
    // 19:06). The kick is idempotent: the worker's merged backfill keys
    // entries by the merge identity, so existing merges never re-enqueue.
    if (newMerges || baroBackfilled) {
      // ignore: unawaited_futures
      SyncWorker.instance.kick('merge pass saved new merges');
    }
  }

  /// Returns true when at least one merge gained its baro profile.
  Future<bool> _backfillMergedBaro() async {
    final byFile = {for (final r in _localRuns) r.fileName: r};
    var changed = false;
    for (final m in _merges) {
      if (m['baro'] != null) continue;
      final a = byFile[m['aFile'] as String?];
      final b = byFile[m['bFile'] as String?];
      if (a == null || b == null) continue;
      final dataA = await _storage.load(a.fileName);
      final dataB = await _storage.load(b.fileName);
      if (dataA == null || dataB == null) continue;
      try {
        final res = await compute(RunMerger.mergePairForIsolate, {
          'aData': dataA,
          'bData': dataB,
        });
        if (res['ok'] == true) {
          m['baro'] = res['baro'];
          changed = true;
        }
      } catch (e) {
        debugPrint('[AUTO] baro backfill failed for ${m['aFile']}: $e');
      }
    }
    if (changed) {
      await _storage.saveMerges(_merges);
      if (mounted) setState(() {});
    }
    return changed;
  }

  /// Gate estimate for a saved run (cached; computed in an isolate).
  Future<List<Map<String, dynamic>>?> _gatesFor(String fileName) async {
    final cached = _gateCache[fileName];
    if (cached != null) return cached;
    final data = await _storage.load(fileName);
    if (data == null) return null;
    try {
      final gates = await compute(RunMerger.estimateGatesForIsolate, data);
      _gateCache[fileName] = gates;
      return gates;
    } catch (e) {
      debugPrint('[AUTO] gate estimate failed for $fileName: $e');
      return null;
    }
  }

  Future<void> _scanDevices() async {
    if (_isScanning) return;
    _isScanning = true;

    // Clean any prior scan + held connection before starting fresh (T-008).
    try { await FlutterBluePlus.stopScan(); } catch (_) {}
    if (_ble.isConnected) {
      debugPrint('[SGC] scan: disconnecting previous device first');
      await _ble.disconnect();
      if (mounted) {
        setState(() {
          _sgc = null;
          _config = null;
          _runCount = null;
          _batteryPct = null;
          _batteryCharging = false;
          _flashUsedPct = null;
          _deviceRuns = [];
        });
      }
    }

    final permStatus = await _ble.checkPermissions();
    if (!mounted) { _isScanning = false; return; }
    if (permStatus == BLEPermissionStatus.bluetoothOff) {
      _isScanning = false;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Bluetooth is off')),
      );
      return;
    }

    // Start scan BEFORE showing modal (builder can run multiple times)
    final devices = <BluetoothDevice>[];
    final sub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        if (!devices.any((d) => d.remoteId == r.device.remoteId)) {
          devices.add(r.device);
        }
      }
    });

    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 15));

    void stop() {
      sub.cancel();
      FlutterBluePlus.stopScan();
      _isScanning = false;
    }

    Timer(const Duration(seconds: 15), stop);

    if (!mounted) { stop(); return; }

    final selected = await showModalBottomSheet<BluetoothDevice>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Theme.of(context).colorScheme.surface,
      builder: (_) => _ScanSheet(devices: devices),
    );

    stop();

    if (selected != null && mounted) {
      _selectDevice(selected);
    }
  }

  Future<void> _selectDevice(BluetoothDevice device) async {
    setState(() => _isConnecting = true);
    _setupWarnings.clear();

    try {
      await _ble.connectToDevice(device);
      _device = device;  // V1.22: keep for per-run reconnect
      _rememberPaired(device.remoteId.str);  // v1.58: auto-sync pairing
      // 1.69: pair-time letter assignment — a newly paired device gets its
      // registry letter here, even before its first run is downloaded.
      // ignore: unawaited_futures
      DeviceRegistry.instance.letterFor(device.remoteId.str);
      final sgc = SGCService(_ble);

      // 1.71 (F46 v2): apply the device-delete backlog BEFORE ANYTHING
      // else — certainly before the run-list read below — so a backlogged
      // run (missing locally, waiting for its device 'd') can never be
      // re-downloaded or re-appear in this session's listing. ORDER IS
      // CRITICAL (JP spec 2026-10-27).
      await _applyDeviceDeleteBacklog(sgc, device.remoteId.str);

      // Dump discovered services so we can see what the device actually exposes
      sgc.debugLogServices();

      try {
        debugPrint('[SGC] syncing time…');
        final syncErr = await sgc.syncTime();
        if (syncErr != null) {
          debugPrint('[SGC] ⚠️ time sync: $syncErr');
          _setupWarnings.add('time: $syncErr');
        }
        debugPrint('[SGC] reading config…');
        _config = await sgc.readConfig();
        _setupWarnings.addAll(_config?.warnings ?? const []);
        debugPrint('[SGC] config: ${_config?.deviceName ?? 'UNKNOWN'} / '
            '${_config?.armSide?.label ?? 'UNKNOWN'} / ${_config?.discipline?.label ?? 'UNKNOWN'}'
            '${(_config?.warnings.isNotEmpty ?? false) ? ' (warnings: ${_config!.warnings.length})' : ''}');
        debugPrint('[SGC] reading run count…');
        _runCount = await sgc.getRunCount();
        debugPrint('[SGC] run count = $_runCount');
        // ABC5 battery (bit7 = charging, 0x7F = unknown) — separate try
        // so a battery read failure never masks the run list.
        try {
          _batteryPct = await sgc.getBattery();
          _batteryCharging = await sgc.isCharging();
          debugPrint('[SGC] battery = $_batteryPct%'
              '${_batteryCharging ? ' (charging)' : ''}');
        } catch (e) {
          debugPrint('[SGC] battery read failed: $e');
          _batteryPct = null;
          _batteryCharging = false;
        }
        // 1.71: ABC7 used-pct — input for the half-memory sync warning.
        // Never DISPLAYED (JP: no remaining-memory readouts on the card);
        // own try so a failed read never masks the run list (warning just
        // stays off — unknown capacity).
        try {
          _flashUsedPct = await sgc.getFlashUsed();
        } catch (e) {
          debugPrint('[SGC] flash-used read failed: $e');
          _flashUsedPct = null;
        }
        final ft = FileTransfer(sgc);
        _deviceRuns = await ft.getRunList();
        _recomputeSyncWarning();
        debugPrint('[SGC] device runs: ${_deviceRuns.length}');
      } catch (e, stack) {
        // v1.50 (audit A6): setup failures SURFACE — orange banner + card
        // state. The old green 'Connected' regardless masked '0 on device'.
        debugPrint('[SGC] ERROR during setup: $e');
        debugPrint('[SGC] stack: $stack');
        _setupWarnings.add('setup: $e');
        _runCount = null;
        _batteryPct = null;
        _batteryCharging = false;
        _flashUsedPct = null;
        _deviceRuns = [];
      }

      setState(() {
        _sgc = sgc;
        _isConnecting = false;
      });
      _resetIdleDisconnectTimer();   // 1.64: 1-min idle auto-disconnect

      if (mounted) {
        final warn = _setupWarnings;
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(warn.isEmpty
                ? 'Connected to ${device.platformName}'
                : 'Connected — ${warn.length} warning(s): ${warn.first}${warn.length > 1 ? ' (+${warn.length - 1})' : ''}'),
            backgroundColor: warn.isEmpty ? Colors.green : Colors.orange,
            duration: Duration(seconds: warn.isEmpty ? 3 : 8),
          ),
        );
      }
    } catch (e, stack) {
      // V1.33: full error visibility — the first line alone hid the real
      // failure stage (connect vs MTU vs service discovery).
      debugPrint('[SGC] connection failed: $e');
      debugPrint('[SGC] $stack');
      setState(() => _isConnecting = false);
      if (mounted) {
        final msg = e.toString();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              'Connection failed: ${msg.length > 220 ? '${msg.substring(0, 220)}…' : msg}',
            ),
            backgroundColor: Colors.red,
            duration: const Duration(seconds: 8),
          ),
        );
      }
    }
  }

  /// V1.22/V1.24: drop and re-establish the BLE connection so the next
  /// download starts with a fresh GATT session (one run per connection,
  /// Phase 2). Retry a few times: after a transfer the S22 may need a longer
  /// settle window before it can open the next link.
  Future<bool> _freshConnection() async {
    final dev = _device;
    if (dev == null) return false;
    try { await _ble.disconnect(); } catch (_) {}
    for (int attempt = 1; attempt <= 3; attempt++) {
      await Future.delayed(Duration(milliseconds: 600 * attempt));
      try {
        await _ble.connectToDevice(dev);
        if (mounted) setState(() => _sgc = SGCService(_ble));
        debugPrint('[SGC] freshConnection OK on attempt $attempt (mtu=${_ble.mtu})');
        return true;
      } catch (e) {
        debugPrint('[SGC] freshConnection attempt $attempt/3 failed: $e');
      }
    }
    // Keep a live SGCService around even on failure — downloadRun() has its
    // own reconnect/resume attempts and self-heals from ble.services.
    if (mounted) setState(() => _sgc = SGCService(_ble));
    return false;
  }

  Future<void> _disconnect() async {
    _cancelIdleDisconnectTimer();
    _isDownloading = false;  // V1.23: cancel any in-flight batch first
    await _ble.disconnect();
    if (mounted) {
      setState(() {
        _sgc = null;
        _device = null;
        _config = null;
        _runCount = null;
        _batteryPct = null;
        _batteryCharging = false;
        _flashUsedPct = null;
        _deviceRuns = [];
      });
    }
  }

  Future<void> _loadLocalRuns() async {
    final runs = await _storage.listAll();
    await _refreshDerivedState(runs);
    if (mounted) setState(() => _localRuns = runs);
  }

  /// 1.69: recompute everything derived from the run set — pager day
  /// clamp, per-run default names ("A#32"), per-merge default names
  /// ("A#32 + A#33"). Runs whenever the local index changes.
  /// [clampDay] false when [runs] is a possibly-stale snapshot (auto-prefs
  /// load can race ahead of the first index read) — names refresh, but a
  /// valid displayed day is never clobbered by an empty list.
  Future<void> _refreshDerivedState(List<SavedRun> runs,
      {bool clampDay = true}) async {
    final options = _dayOptionsOf(runs);
    if (options.isNotEmpty) {
      if (_displayDay == null || !options.contains(_displayDay)) {
        // First load / displayed day deleted → snap to the newest day.
        _displayDay = options.first;
      }
    } else if (clampDay) {
      _displayDay = null;
    }
    final byFile = {for (final r in runs) r.fileName: r};
    final names = <String, String>{};
    for (final r in runs) {
      names[r.fileName] = await _storage.defaultRunName(r);
    }
    _defaultNames = names;
    final mnames = <String, String>{};
    for (final m in _merges) {
      mnames[_mergeKeyOf(m)] =
          await _storage.defaultMergeName(m, byFile: byFile);
    }
    _mergeDefaultNames = mnames;
  }

  /// Device runs not yet present in local storage.
  ///
  /// Composite identity: a device run is missing unless a local SavedRun has
  /// BOTH the same id AND the same timestamp. Run ids are recycled after a
  /// device reset, so id alone would falsely mark new runs as downloaded.
  List<RunMetadata> get _missingRuns {
    final localKeys = _localRuns.map((r) => (r.id, r.timestamp)).toSet();
    return _deviceRuns.where((r) => !localKeys.contains((r.id, r.timestamp))).toList();
  }

  // ── 1.71 (F46 v2): device-delete legs + backlog ──────────────────

  void _refreshBacklogCount() {
    _backlogPending = DeviceDeleteBacklog.instance.count;
    if (mounted) setState(() {});
  }

  /// Apply the device-delete backlog over the LIVE Flutter BLE connection
  /// (manual connect path). MUST run before any listing/download batch —
  /// a backlogged run is missing locally, so a listing taken before the
  /// backlog is applied would offer it for re-download. 'd' is idempotent
  /// (unknown id = "OK already gone" = entry resolved); busy (E 5/E 4)
  /// and transport failures keep the entry for the next connect.
  Future<void> _applyDeviceDeleteBacklog(SGCService sgc, String mac) async {
    await DeviceDeleteBacklog.instance.load();
    final todo = DeviceDeleteBacklog.instance.forDevice(mac);
    for (final e in todo) {
      try {
        final res = await sgc.deleteRun(e.runId);
        if (res == DeviceDeleteResult.deleted ||
            res == DeviceDeleteResult.alreadyGone) {
          await DeviceDeleteBacklog.instance.remove(e);
          debugPrint('[SGC] backlog applied: device delete #${e.runId}');
        } else {
          debugPrint('[SGC] backlog deferred: device delete #${e.runId} ($res)');
        }
      } catch (err) {
        debugPrint('[SGC] backlog apply failed for #${e.runId}: $err');
      }
    }
    _refreshBacklogCount();
  }

  /// Apply the device-delete backlog over the NATIVE engine (auto-sync
  /// path — the radio is Kotlin's there, the Flutter SGCService has no
  /// connection). Same ordering contract as the live path: runs before
  /// the listing. One connection for the whole batch.
  Future<void> _applyBacklogNative(String address) async {
    await DeviceDeleteBacklog.instance.load();
    final todo = DeviceDeleteBacklog.instance.forDevice(address);
    if (todo.isEmpty) return;
    try {
      final ok = await NativeBleDownloader().deleteRuns(
        address: address,
        runIds: todo.map((e) => e.runId).toList(),
      );
      for (final e in todo) {
        if (ok.contains(e.runId)) {
          await DeviceDeleteBacklog.instance.remove(e);
          debugPrint('[AUTO] backlog applied: device delete #${e.runId}');
        }
      }
    } catch (e) {
      debugPrint('[AUTO] backlog apply failed (kept for next connect): $e');
      return;
    }
    _refreshBacklogCount();
  }

  /// Remove this run's copy from the DEVICE — ALWAYS attempted after a
  /// successful phone deletion (1.71, JP: phone-delete ⇒ device-delete).
  /// Live 'd <id>' when the recording device is connected; the sourceMac
  /// guard (run came from THIS device; null = pre-1.69 single-device era)
  /// prevents deleting the wrong device's run on a recycled id. Everything
  /// else — device offline, busy, write failure — lands in the durable
  /// device-delete backlog, which every connect applies before its listing.
  /// Returns true when the device confirmed the delete NOW.
  Future<bool> _deleteFromDevice(SavedRun run) async {
    final connectedMac = _device?.remoteId.str;
    final sgc = _sgc;
    final live = sgc != null &&
        connectedMac != null &&
        (run.sourceMac == null || run.sourceMac == connectedMac);
    if (live) {
      try {
        final res = await sgc.deleteRun(run.id);
        if (res == DeviceDeleteResult.deleted ||
            res == DeviceDeleteResult.alreadyGone) {
          // Keep the connected-card listing honest without re-reading it.
          _deviceRuns.removeWhere(
              (r) => r.id == run.id && r.timestamp == run.timestamp);
          return true;
        }
        debugPrint('[SGC] device delete #${run.id} deferred ($res) — backlogging');
      } catch (e) {
        debugPrint('[SGC] device delete #${run.id} failed: $e — backlogging');
      }
    }
    try {
      await DeviceDeleteBacklog.instance.add(
        runId: run.id,
        fileName: run.fileName,
        mac: run.sourceMac,
      );
    } catch (e) {
      debugPrint('[SGC] backlog persist failed for #${run.id}: $e');
    }
    _refreshBacklogCount();
    return false;
  }

  // ── 1.71: half-memory sync warning ─────────────────────────────

  /// A device run is safe when its local copy is CONFIRMED uploaded
  /// (DONE queue entry). Match by composite identity (id + timestamp),
  /// then the queue entry via the local fileName.
  bool _deviceRunUploaded(RunMetadata r) {
    for (final s in _localRuns) {
      if (s.id == r.id && s.timestamp == r.timestamp) {
        final e = UploadQueue.instance.forFile(s.fileName);
        return e != null && e.status == UploadStatus.done;
      }
    }
    return false;
  }

  /// Half-memory sync warning (JP spec 2026-10-27).
  ///
  /// Capacity formula, derived ONLY from data the app already has:
  ///   usedPct   = ABC7 store_used_pct() — % of the device's log flash
  ///               region occupied by live runs.
  ///   liveBytes ≈ Σ listing payload sizes (the listing covers every live
  ///               run; the ~22 B/run blob overhead is ignored).
  ///   capacity  ≈ liveBytes / (usedPct / 100)
  ///   unsynced  = Σ payload sizes of listing runs with no DONE cloud
  ///               upload (see [_deviceRunUploaded])
  ///   warn when unsynced ≥ 0.5 × capacity
  /// usedPct ≤ 0 or unreadable → capacity unknown → warning stays off.
  void _recomputeSyncWarning() {
    final pct = _flashUsedPct;
    if (pct == null || pct <= 0 || _deviceRuns.isEmpty) {
      _unsyncedRuns = 0;
      _syncWarningActive = false;
      return;
    }
    var totalBytes = 0;
    var unsyncedBytes = 0;
    var unsynced = 0;
    for (final r in _deviceRuns) {
      totalBytes += r.size;
      if (!_deviceRunUploaded(r)) {
        unsyncedBytes += r.size;
        unsynced++;
      }
    }
    _unsyncedRuns = unsynced;
    final capacity = totalBytes * 100 / pct;
    _syncWarningActive = capacity > 0 && unsyncedBytes >= capacity / 2;
    if (_syncWarningActive) {
      debugPrint('[SGC] ⚠ sync warning ON: $unsynced runs not synced, '
          'span $unsyncedBytes B ≥ 50% of ~${capacity.toStringAsFixed(0)} B');
    }
  }

  /// Persistent amber banner (Runs screen, both views). Dismissable per
  /// session only — it must come back next launch until the runs sync.
  Widget _syncWarningBanner() {
    if (!_syncWarningActive || _syncWarningDismissed || _unsyncedRuns <= 0) {
      return const SizedBox.shrink();
    }
    return Card(
      color: Colors.orange.shade100,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 0, 4, 0),
        child: Row(children: [
          Icon(Icons.warning_amber_rounded,
              color: Colors.orange.shade900, size: 20),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              '⚠ $_unsyncedRuns runs not synced — sync to avoid losses',
              style: TextStyle(
                  color: Colors.orange.shade900,
                  fontSize: 13,
                  fontWeight: FontWeight.w600),
            ),
          ),
          IconButton(
            icon: const Icon(Icons.close, size: 18),
            tooltip: 'Dismiss (until next app start)',
            onPressed: () =>
                setState(() => _syncWarningDismissed = true),
          ),
        ]),
      ),
    );
  }

  Future<void> _openLocalRun(SavedRun run) async {
    final data = await _storage.load(run.fileName);
    if (data == null || !mounted) return;

    // V1.12: decompress in a background isolate so the UI stays responsive
    // (and BLE callbacks are never blocked when opening runs while connected).
    final crcOk = Decompressor().validateCRC(data);
    debugPrint('[SGC] ${crcOk ? "✅" : "⚠️"} Local CRC for ${run.fileName}: ${crcOk ? "OK" : "FAILED"}');
    final decoded = await compute(_decompressRunBackground, data);

    if (mounted) {
      Navigator.of(context).push(
        MaterialPageRoute(
          builder: (_) => RunDetailScreen(
            result: decoded,
            deviceName: run.deviceName,
            runName: run.name,
          ),
        ),
      );
    }
  }

  Future<void> _downloadRuns() async {
    if (_sgc == null || _isDownloading) return;
    setState(() {
      _isDownloading = true;
      _downloadStatus = 'Preparing…';
      _downloadCurrent = 0;
      _downloadTotal = 0;
      _downloadBytes = 0;
      _downloadRunSize = 0;
    });

    try {
      final ft = FileTransfer(_sgc!);

      // Refresh device run list + local index, then download missing runs.
      _deviceRuns = await ft.getRunList();
      await _loadLocalRuns();
      final missing = _missingRuns;
      String metaKey(RunMetadata r) => '#${r.id}@${r.timestamp}';
      String savedKey(SavedRun r) => '#${r.id}@${r.timestamp}';
      debugPrint('[SGC] Device runs (${_deviceRuns.length}): ${_deviceRuns.map(metaKey).join(', ')}');
      debugPrint('[SGC] Local runs (${_localRuns.length}): ${_localRuns.map(savedKey).join(', ')}');
      debugPrint('[SGC] Missing runs (${missing.length}): ${missing.map(metaKey).join(', ')}');

      if (missing.isEmpty) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text(_deviceRuns.isEmpty
                ? 'No runs on device'
                : 'All ${_deviceRuns.length} runs already downloaded')),
          );
        }
        return;
      }

      int ok = 0, failed = 0;
      final failedRuns = <RunMetadata>[];
      _downloadTotal = missing.length;
      for (int i = 0; i < missing.length; i++) {
        if (!_isDownloading) break;  // V1.23: user cancelled mid-batch
        final run = missing[i];
        _downloadCurrent++;
        _downloadBytes = 0;
        _downloadRunSize = run.size;
        // V1.22 (Phase 2): ONE RUN PER CONNECTION — reconnect BEFORE each
        // run after the first so every run gets a fresh GATT session.
        // V1.24: retry the reconnect; multi-run benches showed the batch can
        // otherwise stop after ~2 runs when the S22 is slow to reopen BLE.
        if (i > 0) {
          setState(() => _downloadStatus = 'Reconnecting…');
          final reconnected = await _freshConnection();
          if (!reconnected) {
            debugPrint('[SGC] reconnect before run ${metaKey(run)} failed; transfer layer will retry');
          }
          setState(() => _downloadStatus =
              'Run $_downloadCurrent/$_downloadTotal (${run.size ~/ 1024} KB)');
        }
        setState(() => _downloadStatus =
            'Run $_downloadCurrent/$_downloadTotal (${run.size ~/ 1024} KB)');
        debugPrint('[SGC] Downloading run ${metaKey(run)} (${run.size} bytes, side=${run.side})');
        final data = await _downloadOne(FileTransfer(_sgc ?? SGCService(_ble)), run.id);
        if (data == null) {
          failed++;
          failedRuns.add(run);
        } else {
          final decoded = await compute(_decompressRunBackground, data);
          await _storage.save(
            runId: run.id,
            compressedData: data,
            result: decoded,
            deviceName: _config?.deviceName ?? 'SGC',
            sourceMac: _device?.remoteId.str, // 1.69: stamp recording device
          );
          ok++;
          // Keep the missing-run identity current while the batch is still
          // running; also makes a later cancel leave an accurate screen.
          await _loadLocalRuns();
          // P1 cloud: run landed locally → queue it for upload.
          // ignore: unawaited_futures
          SyncWorker.instance.kick('run saved');
        }
      }
      await _loadLocalRuns();
      // V1.23: refresh the device run list + service after the batch so the
      // screen reflects reality (Phase-2 reconnects cleared them).
      try {
        _deviceRuns = await FileTransfer(_sgc ?? SGCService(_ble)).getRunList();
        if (mounted && _ble.isConnected && _sgc == null) {
          setState(() => _sgc = SGCService(_ble));
        }
      } catch (_) {}
      if (mounted) {
        final failedIds = failedRuns.map((r) => '#${r.id}').join(', ');
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(failed == 0
                ? 'Downloaded $ok run(s)'
                : 'Downloaded $ok/${missing.length} · failed: $failedIds'),
            backgroundColor: failed > 0 ? Colors.orange : Colors.green,
          ),
        );
      }

    } catch (e, stack) {
      debugPrint('[SGC] Download error: $e');
      debugPrint('[SGC] $stack');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Download error: $e')),
        );
      }
    } finally {
      _resetIdleDisconnectTimer();   // 1.64: batch done -> idle clock restarts
      if (mounted) {
        setState(() {
          _isDownloading = false;
          _downloadStatus = '';
          _downloadBytes = 0;
          _downloadRunSize = 0;
        });
      }
    }
  }

  /// Download one run with CRC retry. Returns compressed bytes or null on failure.
  Future<Uint8List?> _downloadOne(FileTransfer ft, int runId) async {
    const maxAttempts = 2;
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
      final result = await ft.download(runId, onProgress: (received, total) {
        setState(() {
          _downloadBytes = received;
          if (total > _downloadRunSize) _downloadRunSize = total;
        });
      });
      if (result.compressedData.isEmpty) {
        debugPrint('[SGC] run #$runId: empty data (attempt $attempt)');
        if (attempt < maxAttempts) {
          debugPrint('[SGC] run #$runId: retrying on a fresh GATT session');
          await _freshConnection();
        }
        continue;
      }
      final decompressor = Decompressor();
      if (decompressor.validateCRC(result.compressedData)) {
        debugPrint('[SGC] ✅ CRC valid for run #$runId (attempt $attempt)');
        return result.compressedData;
      }
      debugPrint('[SGC] ⚠️ CRC FAILED for run #$runId (attempt $attempt/$maxAttempts)');
      if (attempt < maxAttempts) {
        debugPrint('[SGC] run #$runId: CRC retry on a fresh GATT session');
        await _freshConnection();
      }
    }
    return null;
  }

  /// V1.25: native Android BluetoothGatt downloader (beta spike).
  ///
  /// Flutter disconnects and hands the radio to the Kotlin engine. Native code
  /// downloads only the runs currently missing locally, then Flutter resumes
  /// its normal role: decompression, payload CRC validation, local storage,
  /// and UI refresh.
  Future<void> _downloadRunsNative() async {
    final dev = _device;
    if (dev == null || _isDownloading) return;

    await _loadLocalRuns();
    final missing = _missingRuns;
    if (missing.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('All device runs already downloaded')),
      );
      return;
    }

    setState(() {
      _isDownloading = true;
      _downloadStatus = 'Native BLE preparing…';
      _downloadCurrent = 0;
      _downloadTotal = missing.length;
      _downloadBytes = 0;
      _downloadRunSize = 0;
    });

    int saved = 0;
    final payloadFailed = <int>[];
    final nativeFailed = <String>[];

    // V1.32: live native progress — connect attempts, FT bytes, resumes.
    // V1.41: Incremental save from streamed run complete events on a SINGLE connection!
    NativeBleDownloader.setListener(
      onLog: (msg) {
        debugPrint('[SGC-NATIVE] $msg');
        if (mounted) setState(() => _downloadStatus = msg);
      },
      onProgress: (runId, bytes, expected) {
        if (mounted) {
          setState(() {
            _downloadStatus = 'Native FT run #$runId';
            _downloadBytes = bytes;
            _downloadRunSize = expected;
          });
        }
      },
      onRunComplete: (runId, timestamp, data) async {
        debugPrint('[SGC-NATIVE] incremental save event for run #$runId');
        _downloadCurrent++;
        if (!Decompressor().validateCRC(data)) {
          debugPrint('[SGC-NATIVE] ⚠️ payload CRC FAILED for run #$runId');
          payloadFailed.add(runId);
          return;
        }
        try {
          final decoded = await compute(_decompressRunBackground, data);
          await _storage.save(
            runId: runId,
            compressedData: data,
            result: decoded,
            deviceName: _config?.deviceName ?? 'SGC',
            sourceMac: dev.remoteId.str, // 1.69: stamp recording device
          );
          saved++;
          await _loadLocalRuns();
          // P1 cloud: run landed locally → queue it for upload.
          // ignore: unawaited_futures
          SyncWorker.instance.kick('native run saved');
        } catch (e) {
          debugPrint('[SGC-NATIVE] decompress/save error for run #$runId: $e');
          payloadFailed.add(runId);
        }
      },
    );

    try {
      final address = dev.remoteId.str;
      final ids = missing.map((r) => r.id).toList();
      debugPrint('[SGC-NATIVE] batch start: ${ids.map((id) => '#$id').join(', ')} via $address');

      // Native must own the GATT session; release the FlutterBluePlus link.
      await _ble.disconnect();
      await Future.delayed(const Duration(milliseconds: 700));

      // V1.41: SINGLE NATIVE CALL for the entire batch.
      // 1.36 called downloadRuns() in a loop, disconnecting and reconnecting
      // for every single run, which caused GATT client leakage (clientIf pile-up)
      // and S22 link collapse. Now Kotlin connects once, downloads all, streams
      // completed runs via onRunComplete, and disconnects once at the end!
      final result = await NativeBleDownloader().downloadRuns(
        address: address,
        runIds: ids,
      );
      for (final line in result.log) {
        debugPrint('[SGC-NATIVE] $line');
      }
      for (final f in result.failed) {
        nativeFailed.add('#${f.id} (${f.reason})');
      }

      if (mounted) {
        final crcFailed = payloadFailed.map((id) => '#$id').join(', ');
        final parts = <String>['Native downloaded $saved/${missing.length}'];
        if (nativeFailed.isNotEmpty) parts.add('BLE failed: ${nativeFailed.join(', ')}');
        if (crcFailed.isNotEmpty) parts.add('CRC failed: $crcFailed');
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(parts.join(' · ')),
            backgroundColor: (nativeFailed.isEmpty && payloadFailed.isEmpty)
                ? Colors.green
                : Colors.orange,
          ),
        );
      }
    } catch (e, stack) {
      debugPrint('[SGC-NATIVE] error: $e');
      debugPrint('[SGC-NATIVE] $stack');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Native download error: $e')),
        );
      }
    } finally {
      NativeBleDownloader.setListener();
      _resetIdleDisconnectTimer();   // 1.64: batch done -> idle clock restarts
      if (mounted) {
        setState(() {
          _isDownloading = false;
          _downloadStatus = '';
          _downloadBytes = 0;
          _downloadRunSize = 0;
        });
      }
      // Reconnect the normal Flutter path so the screen reflects reality.
      try {
        await _ble.connectToDevice(dev);
        if (mounted) setState(() => _sgc = SGCService(_ble));
        _deviceRuns = await FileTransfer(_sgc!).getRunList();
        await _loadLocalRuns();
      } catch (e) {
        debugPrint('[SGC-NATIVE] Flutter reconnect after native batch failed: $e');
        if (mounted) {
          setState(() {
            _sgc = null;
            _deviceRuns = [];
          });
        }
      }
    }
  }

  /// P1 cloud section: live upload status + entry to the F48 cloud-runs
  /// browser. Shown in both the connected and saved-runs views so users
  /// always see the sync state where they think about their runs.
  /// 1.71: the sync status line also surfaces the device-delete backlog
  /// ("N device deletions pending") — those fire on the next connect.
  Widget _buildCloudCard() {
    final backlogNote = _backlogPending > 0
        ? ' · $_backlogPending device deletion'
            '${_backlogPending == 1 ? '' : 's'} pending'
        : '';
    return Card(
      child: Column(children: [
        ValueListenableBuilder<SyncStatus>(
          valueListenable: SyncWorker.instance.status,
          builder: (context, sync, _) => ListTile(
            leading: sync.busy
                ? const SizedBox(
                    width: 24,
                    height: 24,
                    child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.cloud_sync),
            title: const Text('Cloud Sync'),
            subtitle: Text((sync.text.isEmpty
                    ? 'Runs upload while signed in (Settings → Cloud Account)'
                    : sync.text)
                + backlogNote),
            trailing: IconButton(
              icon: const Icon(Icons.refresh),
              tooltip: 'Sync now',
              onPressed: () => SyncWorker.instance.kick('manual'),
            ),
          ),
        ),
        ListTile(
          leading: const Icon(Icons.folder_shared),
          title: const Text('Cloud Runs'),
          subtitle: const Text('Browse your uploaded runs (read-only)'),
          trailing: const Icon(Icons.chevron_right, color: Colors.grey),
          onTap: () => Navigator.of(context).push(MaterialPageRoute(
              builder: (_) => const CloudRunsScreen())),
        ),
      ]),
    );
  }

  @override
  Widget build(BuildContext context) {
    final isConnected = _sgc != null;

    return Scaffold(
      appBar: AppBar(
        title: Row(
          children: [
            const Text('Runs'),
            const Spacer(),
            Text('v$APP_VERSION',
                style: TextStyle(fontSize: 12, color: Colors.grey.shade400, fontWeight: FontWeight.normal)),
          ],
        ),
        actions: [
          // 1.69 day pager (JP): one day at a time — ◀ older · label · ▶
          // newer. Replaces the 1.66 date-filter popup. Disabled at the
          // ends of the available day range.
          if (_localRuns.isNotEmpty)
            Row(mainAxisSize: MainAxisSize.min, children: [
              IconButton(
                icon: const Icon(Icons.chevron_left),
                tooltip: 'Older day',
                onPressed: _canGoOlder ? _goOlderDay : null,
              ),
              Text(_displayDay == null ? '' : _dayLabel(_displayDay!),
                  style: const TextStyle(
                      fontSize: 14, fontWeight: FontWeight.w600)),
              IconButton(
                icon: const Icon(Icons.chevron_right),
                tooltip: 'Newer day',
                onPressed: _canGoNewer ? _goNewerDay : null,
              ),
            ]),
          if (isConnected)
            IconButton(
              icon: const Icon(Icons.bluetooth_connected, color: Colors.green),
              tooltip: 'Disconnect',
              onPressed: _disconnect,
            ),
        ],
      ),
      body: isConnected
          ? _buildConnectedView()
          : _localRuns.isEmpty
              ? const Center(
                  child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                    Icon(Icons.timer_off, size: 64, color: Colors.grey),
                    SizedBox(height: 16),
                    Text('No runs yet', style: TextStyle(fontSize: 18, color: Colors.grey)),
                    SizedBox(height: 8),
                    Text('Connect a device via BLE to download runs',
                        style: TextStyle(color: Colors.grey)),
                  ]),
                )
              : ListView(
                  padding: const EdgeInsets.all(16),
                  children: [
                    _syncWarningBanner(),
                    Card(
                      color: Colors.blue.shade50,
                      child: const Padding(
                        padding: EdgeInsets.all(12),
                        child: Row(children: [
                          Icon(Icons.info_outline, size: 18, color: Colors.blue),
                          SizedBox(width: 8),
                          Expanded(child: Text('Connect a device to download more runs',
                              style: TextStyle(color: Colors.blue, fontSize: 13))),
                        ]),
                      ),
                    ),
                    _buildCloudCard(),
                    if (_autoEnabled) ...[
                      const SizedBox(height: 8),
                      Text(
                        _autoStatus.isEmpty
                            ? 'Auto-sync armed · paired devices: ${_pairedMacs.length}'
                            : _autoStatus,
                        style: TextStyle(
                            fontSize: 12, color: Colors.grey.shade600),
                        textAlign: TextAlign.center,
                      ),
                    ],
                    const SizedBox(height: 12),
                    if (_dayMerges.isNotEmpty) ...[
                      Padding(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 4, vertical: 4),
                        child: Text(
                            'Merged Courses (${_dayMerges.length}'
                            '${_dayMerges.length == _merges.length ? '' : ' of ${_merges.length}'})',
                            style: TextStyle(
                                fontSize: 14,
                                fontWeight: FontWeight.w600,
                                color: Colors.grey.shade700)),
                      ),
                      for (final m in _dayMerges)
                        Card(
                          child: ListTile(
                            leading: const Icon(Icons.merge_type),
                            title: Text(_mergeDisplayName(m)),
                            subtitle: Text(
                                '${_mergeHasName(m) ? 'Run #${m['aId']} + #${m['bId']} · ' : ''}'
                                '${(m['gates'] as List).length} gates impact-confirmed · dual device'),
                            trailing: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                IconButton(
                                  icon: const Icon(Icons.edit,
                                      size: 20, color: Colors.grey),
                                  tooltip: 'Rename merged course',
                                  onPressed: () => _promptRenameMerge(m),
                                ),
                                const Icon(Icons.chevron_right,
                                    color: Colors.grey),
                              ],
                            ),
                            onTap: () =>
                                Navigator.of(context).push(MaterialPageRoute(
                              builder: (_) => MergedRunScreen(
                                title: _mergeDisplayName(m),
                                gates: (m['gates'] as List)
                                    .cast<Map<String, dynamic>>(),
                                baro: (m['baro'] as List?)
                                        ?.cast<Map<String, dynamic>>() ??
                                    const [],
                              ),
                            )),
                            onLongPress: () => _showMergeActions(m),
                          ),
                        ),
                      const SizedBox(height: 12),
                    ],
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
                      child: Text('Saved Runs (${_dayRuns.length}${_dayRuns.length == _localRuns.length ? '' : ' of ${_localRuns.length}'})',
                          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey.shade700)),
                    ),
                    ...[
                      for (int i = 0; i < _dayRuns.length; i++)
                        _buildLocalRunTile(_dayRuns[i], i + 1),
                    ],
                  ],
                ),
      floatingActionButton: _isConnecting
          ? const FloatingActionButton.extended(
              onPressed: null,
              icon: SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white)),
              label: Text('Connecting…'),
            )
          : FloatingActionButton.extended(
              onPressed: _scanDevices,
              icon: const Icon(Icons.bluetooth),
              label: const Text('Scan Devices'),
            ),
    );
  }

  Widget _buildConnectedView() {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _syncWarningBanner(),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(Icons.bluetooth_connected, color: Colors.green, size: 28),
                    const SizedBox(width: 12),
                    Text(_config?.deviceName ?? 'SGC Device',
                        style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                  ],
                ),
                const SizedBox(height: 16),
                // F21 (2026-10-23): device params editable over BLE.
                // Tap row → dialog → write → re-read config (device
                // re-advertises with a new name immediately).
                InkWell(
                  onTap: _editDeviceName,
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Row(children: [
                      SizedBox(width: 120, child: Text('Device Name', style: const TextStyle(color: Colors.grey))),
                      Expanded(child: Text(_config?.deviceName ?? 'Unknown', style: const TextStyle(fontWeight: FontWeight.w500))),
                      const Icon(Icons.edit, size: 16, color: Colors.grey),
                    ]),
                  ),
                ),
                InkWell(
                  onTap: _editArmSide,
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Row(children: [
                      SizedBox(width: 120, child: Text('Arm Side', style: const TextStyle(color: Colors.grey))),
                      Expanded(child: Text(_config?.armSide?.label ?? 'Unknown', style: const TextStyle(fontWeight: FontWeight.w500))),
                      const Icon(Icons.edit, size: 16, color: Colors.grey),
                    ]),
                  ),
                ),
                InkWell(
                  onTap: _editDiscipline,
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Row(children: [
                      SizedBox(width: 120, child: Text('Discipline', style: const TextStyle(color: Colors.grey))),
                      Expanded(child: Text(_config?.discipline?.label ?? 'Unknown', style: const TextStyle(fontWeight: FontWeight.w500))),
                      const Icon(Icons.edit, size: 16, color: Colors.grey),
                    ]),
                  ),
                ),
                if (_setupWarnings.isNotEmpty)
                  Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: Text(
                      '⚠ ${_setupWarnings.length} warning(s): ${_setupWarnings.first}',
                      style: TextStyle(fontSize: 12, color: Colors.orange.shade800),
                    ),
                  ),
                // 1.70 (JP, counter labels): the two counters below are
                // DIFFERENT sources for device-stored runs — the flash-slot
                // count (sgc.getRunCount(), ABC characteristic) and the FT
                // run list (_deviceRuns.length, shown as "On device:" in the
                // download tile). They should agree; a mismatch means one
                // read is stale. "Downloaded (phone)" is the local index.
                _infoRow('On device (flash slots)',
                    _runCount?.toString() ?? '? (read failed)'),
                _infoRow('Downloaded (phone)', _localRuns.length.toString()),
                _infoRow('Not Downloaded', _missingRuns.length.toString()),
                _infoRow('Battery', _batteryPct == null
                    ? '— (unknown)'
                    : '$_batteryPct%${_batteryCharging ? ' · charging ⚡' : ''}'),
                _infoRow('BLE MTU', _ble.isConnected ? '${_ble.mtu} bytes' : '— (disconnected)'),
                const SizedBox(height: 8),
                // 1.64 (JP 2026-10-23): manual disconnect — the connected
                // card was a dead end before. Auto: 1 min idle also drops.
                SizedBox(
                  width: double.infinity,
                  child: OutlinedButton.icon(
                    icon: const Icon(Icons.link_off, size: 18),
                    label: const Text('Disconnect'),
                    onPressed: _isDownloading ? null : _disconnect,
                  ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        Card(
          child: ListTile(
            leading: _isDownloading
                ? const SizedBox(width: 24, height: 24, child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.download),
            title: Text(_isDownloading
                ? 'Downloading…'
                : 'Download Missing Runs (${_missingRuns.length})'),
            subtitle: _isDownloading
                ? Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(_downloadStatus),
                      if (_downloadRunSize > 0)
                        Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: LinearProgressIndicator(
                            value: (_downloadBytes / _downloadRunSize).clamp(0.0, 1.0),
                            backgroundColor: Colors.grey.shade300,
                          ),
                        ),
                      if (_downloadRunSize > 0)
                        Text('${_downloadBytes ~/ 1024} / ${_downloadRunSize ~/ 1024} KB',
                            style: const TextStyle(fontSize: 12)),
                    ],
                  )
                : Text('On device: ${_deviceRuns.length} · '
                    'Downloaded: ${_localRuns.length} · '
                    'Not downloaded: ${_missingRuns.length}'),
            trailing: const Icon(Icons.chevron_right),
            // onTap: (_missingRuns.isNotEmpty && !_isDownloading) ? _downloadRuns : null,
            onTap: (_missingRuns.isNotEmpty && !_isDownloading) ? _downloadRunsNative : null,

          ),
        ),
        const SizedBox(height: 12),
        Card(
          child: ListTile(
            leading: _isDownloading
                ? const SizedBox(width: 24, height: 24, child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.memory),
            title: Text(_isDownloading
                ? 'Downloading…'
                : 'Native Download Missing (${_missingRuns.length})'),
            subtitle: Text(_isDownloading
                ? _downloadStatus
                : 'Android BluetoothGatt engine (beta)'),
            trailing: const Icon(Icons.chevron_right),
            onTap: (_missingRuns.isNotEmpty && !_isDownloading) ? _downloadRunsNative : null,
          ),
        ),
        // ── Cloud section (P1: sync status + F48 browser entry) ──
        const SizedBox(height: 12),
        _buildCloudCard(),
        // ── Local runs section ──
        if (_localRuns.isNotEmpty) ...[
          const SizedBox(height: 16),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
            child: Text('Saved Runs (${_dayRuns.length}${_dayRuns.length == _localRuns.length ? '' : ' of ${_localRuns.length}'})',
                style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey.shade700)),
          ),
          ...[
            for (int i = 0; i < _dayRuns.length; i++)
              _buildLocalRunTile(_dayRuns[i], i + 1),
          ],
        ],
      ],
    );
  }

  Widget _buildLocalRunTile(SavedRun run, int dayNumber) {
    final dateStr = DateTime.fromMillisecondsSinceEpoch(run.timestamp * 1000, isUtc: true)
        .toLocal().toString().substring(0, 16);
    final durStr = '${(run.durationSec ~/ 60).toString().padLeft(2, '0')}:'
        '${(run.durationSec % 60).toStringAsFixed(1).padLeft(4, '0')}';
    // 1.69 (JP naming v2): default name = "<deviceLetter>#<device run id>"
    // (computed from the source device's registry letter); a custom F45
    // name always wins.
    final hasName = run.name != null && run.name!.isNotEmpty;
    final defaultName = _defaultNames[run.fileName] ?? 'A#${run.id}';
    final title = hasName ? run.name! : defaultName;
    final subtitle = 'Run #${run.id} · $durStr · '
        '${run.side == 'right' ? 'Right' : 'Left'} · $dateStr · '
        '${run.frameCount} frames · ${run.deviceName}';

    return Card(
      child: ListTile(
        // 1.69 day pager: per-day ordinal 1..N — DISPLAY-ONLY (circle
        // avatar), never stored or uploaded.
        leading: CircleAvatar(
          backgroundColor: Colors.blue.shade100,
          child: Text('$dayNumber',
              style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: Colors.blue.shade800)),
        ),
        title: Text(title,
            style: const TextStyle(fontWeight: FontWeight.w500)),
        subtitle: Text(subtitle),
        trailing: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            IconButton(
              icon: const Icon(Icons.edit, size: 20, color: Colors.grey),
              tooltip: 'Rename run',
              onPressed: () => _promptRename(run),
            ),
            const Icon(Icons.chevron_right, color: Colors.grey),
          ],
        ),
        onTap: () => _openLocalRun(run),
        // 1.70 F46: long-press opens the action menu (rename OR delete);
        // the edit icon keeps going straight to rename.
        onLongPress: () => _showRunActions(run),
      ),
    );
  }

  /// Local calendar-day key (YYYY-MM-DD) of a run's start time.
  String _dayKeyOf(int timestampSec) {
    final d = DateTime.fromMillisecondsSinceEpoch(timestampSec * 1000,
            isUtc: true)
        .toLocal();
    final y = d.year.toString().padLeft(4, '0');
    final m = d.month.toString().padLeft(2, '0');
    final day = d.day.toString().padLeft(2, '0');
    return '$y-$m-$day';
  }

  /// Distinct local days present in [runs] (newest first). Backs the
  /// day pager; [_dayOptions] is the current-run-set view of it.
  List<String> _dayOptionsOf(List<SavedRun> runs) => runs
      .map((r) => _dayKeyOf(r.timestamp))
      .toSet()
      .toList()
      ..sort((a, b) => b.compareTo(a));

  List<String> get _dayOptions => _dayOptionsOf(_localRuns);

  // ── 1.69 day pager (JP): one day at a time ───────────────────────

  int get _dayIndex =>
      _displayDay == null ? -1 : _dayOptions.indexOf(_displayDay!);

  /// ▶ enabled unless already showing the newest day with runs.
  bool get _canGoNewer => _dayIndex > 0;

  /// ◀ enabled unless already showing the oldest day with runs.
  bool get _canGoOlder => _dayIndex >= 0 && _dayIndex < _dayOptions.length - 1;

  void _goNewerDay() {
    final i = _dayIndex;
    if (i <= 0) return;
    setState(() => _displayDay = _dayOptions[i - 1]);
  }

  void _goOlderDay() {
    final i = _dayIndex;
    if (i < 0 || i >= _dayOptions.length - 1) return;
    setState(() => _displayDay = _dayOptions[i + 1]);
  }

  /// "Sun 26 Oct"-style label for a YYYY-MM-DD key (device-local day).
  String _dayLabel(String key) => DateFormat('EEE d MMM').format(
      DateTime.parse(key));

  /// Runs of the displayed day in numbered order:
  /// (timestamp, R-before-L tiebreak, id). The 1-based ordinal is applied
  /// by the tile builder and is DISPLAY-ONLY — never stored or uploaded.
  List<SavedRun> get _dayRuns {
    final day = _displayDay;
    if (day == null) return const [];
    return _localRuns
        .where((r) => _dayKeyOf(r.timestamp) == day)
        .toList()
      ..sort(_runDayOrder);
  }

  int _runDayOrder(SavedRun a, SavedRun b) {
    final c = a.timestamp.compareTo(b.timestamp);
    if (c != 0) return c;
    final aR = a.side == 'right';
    final bR = b.side == 'right';
    if (aR != bR) return aR ? -1 : 1; // R before L on identical starts
    return a.id.compareTo(b.id);
  }

  /// Merged courses follow the pager's day — a merge's day = the day of
  /// its first source run (aFile lookup). If the source run is no longer
  /// on the phone, never hide the merge (JP 2026-10-26 15:13 rule).
  bool _mergeMatchesDay(Map<String, dynamic> m) {
    final day = _displayDay;
    if (day == null) return false;
    final src = _localRuns.where((r) => r.fileName == m['aFile']).toList();
    if (src.isEmpty) return true;
    return _dayKeyOf(src.first.timestamp) == day;
  }

  List<Map<String, dynamic>> get _dayMerges =>
      _merges.where(_mergeMatchesDay).toList();

  /// Stable identity of a merge entry (same identity the merged uuid and
  /// queue key derive from).
  String _mergeKeyOf(Map<String, dynamic> m) =>
      '${m['aFile']}|${m['bFile']}';

  /// F45 (merged extension, JP 2026-10-23): user-editable name for merged
  /// courses. 1.69 default = the two sources' default names joined
  /// ("A#32 + A#33"; production dual-device will read "A#12 + B#7"); a
  /// set name takes the title, the pair identity moves to the subtitle.
  bool _mergeHasName(Map<String, dynamic> m) =>
      (m['name'] as String?)?.isNotEmpty == true;

  String _mergeDefaultNameOf(Map<String, dynamic> m) =>
      _mergeDefaultNames[_mergeKeyOf(m)] ??
      'Run #${m['aId']} + #${m['bId']}';

  String _mergeDisplayName(Map<String, dynamic> m) => _mergeHasName(m)
      ? m['name'] as String
      : _mergeDefaultNameOf(m);

  Future<void> _promptRenameMerge(Map<String, dynamic> m) async {
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => _RenameDialog(
        initialName: m['name'] as String?,
        defaultHint: _mergeDefaultNameOf(m),
      ),
    );
    if (result == null) return;
    final name = result.trim();
    if (name.isEmpty) {
      m.remove('name');
    } else {
      m['name'] = name;
    }
    // Same entry count — passes the monotonic shrink guard by design.
    await _storage.saveMerges(_merges);
    if (mounted) setState(() {});
    // 1.70: propagate to the cloud when this merge is already uploaded.
    // Clearing the label propagates the recomputed default
    // ("<A#x> + <A#y>"), mirroring what the phone now displays.
    final key = UploadQueue.mergedQueueKey(
        m['aFile'] as String, m['bFile'] as String);
    final entry = UploadQueue.instance.forFile(key);
    if (entry == null ||
        entry.status != UploadStatus.done ||
        entry.runId == null) {
      return; // not uploaded (yet) — the next upload carries the name
    }
    final newName = name.isEmpty
        ? await _storage.defaultMergeName(m)
        : name;
    await UploadQueue.instance.enqueueRename(
      target: RenameTarget.merged,
      localKey: key,
      cloudRunId: entry.runId!,
      lockVersion: entry.lockVersion,
      newName: newName,
    );
    // ignore: unawaited_futures
    SyncWorker.instance.kick('merged course renamed');
  }

  // ── F46 delete (1.70) / 1.72 sync-state helpers ──────────────────

  /// Synch state of a single run (1.72: a PER-RUN STATE, not a delete
  /// option): its DONE queue entry with a cloud id, or null when the run
  /// is NOT on the cloud. Drives the long-press switch, the delete
  /// dialog's warning level, and the half-memory sync warning.
  UploadEntry? _cloudEntryForRun(SavedRun run) {
    final e = UploadQueue.instance.forFile(run.fileName);
    if (e == null || e.status != UploadStatus.done || e.runId == null) {
      return null;
    }
    return e;
  }

  /// Synch state of a merged course (queue key = merge identity).
  UploadEntry? _cloudEntryForMerge(Map<String, dynamic> m) {
    final key = UploadQueue.mergedQueueKey(
        m['aFile'] as String, m['bFile'] as String);
    final e = UploadQueue.instance.forFile(key);
    if (e == null || e.status != UploadStatus.done || e.runId == null) {
      return null;
    }
    return e;
  }

  /// Light confirm used by every DESYNC (Synch switch ON→OFF, both
  /// surfaces). Deliberately lightweight: nothing irreversible happens —
  /// the cloud row goes, phone + device copies stay, and the switch can
  /// always be flipped back on (new cloud row).
  Future<bool> _confirmDesync(String what) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Remove from cloud?'),
        content: Text('Phone and device copies stay.'
            '${what.isEmpty ? '' : '\n\n$what'}'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Remove'),
          ),
        ],
      ),
    );
    return ok == true;
  }

  /// 1.72: Synch switch OFF on a local run — DESYNC. Deletes the cloud
  /// row (404/410 "already gone" = desynced all the same), marks the
  /// queue entry DESYNCED (dead: the sync worker's backfill can never
  /// re-upload it automatically) and drops pending rename outbox entries
  /// (nothing left to rename). Phone + device copies untouched.
  Future<void> _desyncRun(SavedRun run) async {
    final entry = _cloudEntryForRun(run);
    if (entry == null) return; // not synced — nothing to remove
    try {
      await ApiClient.instance.deleteRun(entry.runId!);
    } on CloudException catch (e) {
      if (e.statusCode != 404 && e.statusCode != 410) {
        debugPrint('[SGC] desync cloud delete failed for ${run.fileName}: $e');
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text('Cloud delete failed — run stays synced. ($e)'),
              backgroundColor: Colors.red,
            ),
          );
        }
        return;
      }
      debugPrint('[SGC] cloud row already gone (HTTP ${e.statusCode}) '
          '— marking desynced');
    }
    await UploadQueue.instance.markDesynced(entry.clientRunUuid);
    await UploadQueue.instance.removeRenamesForKey(run.fileName);
    if (mounted) setState(() {});
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
            content: Text('Removed from cloud — phone and device copies stay')),
      );
    }
  }

  /// 1.72: Synch switch ON on an unsynced local run — upload NOW. This is
  /// EXPLICIT user intent: the queue entry is (re)activated and the sync
  /// worker is kicked with explicitUuids so this pass uploads exactly this
  /// run and IGNORES the global push_to_cloud preference (the F20 master
  /// toggle must not veto a direct per-run command). A previously
  /// desynced/cloud-deleted run gets a NEW cloud row (same
  /// client_run_uuid is fine — the old row is gone).
  Future<void> _syncRunNow(SavedRun run) async {
    await UploadQueue.instance.load();
    final existing = UploadQueue.instance.forFile(run.fileName);
    final entry = existing ?? await UploadQueue.instance.enqueue(run.fileName);
    if (entry.status != UploadStatus.done) {
      await UploadQueue.instance.markPending(entry.clientRunUuid);
    }
    // ignore: unawaited_futures
    SyncWorker.instance.kick('synch switch on',
        explicitUuids: {entry.clientRunUuid});
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Queued for upload — syncing now')),
      );
    }
  }

  /// 1.72: merged-course variants of the switch legs (desync = cloud
  /// delete of the merged row ONLY — the two source runs keep their own
  /// cloud rows; re-sync = new merged row from the same merge identity).
  Future<void> _desyncMerge(Map<String, dynamic> m) async {
    final key = UploadQueue.mergedQueueKey(
        m['aFile'] as String, m['bFile'] as String);
    final entry = _cloudEntryForMerge(m);
    if (entry == null) return;
    try {
      await ApiClient.instance.deleteRun(entry.runId!);
    } on CloudException catch (e) {
      if (e.statusCode != 404 && e.statusCode != 410) {
        debugPrint('[SGC] desync cloud delete failed for merge: $e');
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text(
                  'Cloud delete failed — merged course stays synced. ($e)'),
              backgroundColor: Colors.red,
            ),
          );
        }
        return;
      }
      debugPrint('[SGC] merged cloud row already gone '
          '(HTTP ${e.statusCode}) — marking desynced');
    }
    await UploadQueue.instance.markDesynced(entry.clientRunUuid);
    await UploadQueue.instance.removeRenamesForKey(key);
    if (mounted) setState(() {});
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
            content: Text('Removed from cloud — the merged course stays on '
                'the phone, its source runs keep their cloud rows')),
      );
    }
  }

  Future<void> _syncMergeNow(Map<String, dynamic> m) async {
    final key = UploadQueue.mergedQueueKey(
        m['aFile'] as String, m['bFile'] as String);
    await UploadQueue.instance.load();
    var entry = UploadQueue.instance.forFile(key);
    if (entry == null) {
      final aId = (m['aId'] as num).toInt();
      final bId = (m['bId'] as num).toInt();
      entry = await UploadQueue.instance.enqueueMerged(
        key,
        mergedRunUuid(
            aId: aId,
            bId: bId,
            aFile: m['aFile'] as String,
            bFile: m['bFile'] as String),
      );
    }
    if (entry.status != UploadStatus.done) {
      await UploadQueue.instance.markPending(entry.clientRunUuid);
    }
    // ignore: unawaited_futures
    SyncWorker.instance.kick('synch switch on (merged)',
        explicitUuids: {entry.clientRunUuid});
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Queued for upload — syncing now')),
      );
    }
  }

  /// Long-press menu for a local run tile — 1.72 (JP redesign): THREE
  /// items. Rename / Synch to cloud [switch] / Delete. The switch IS the
  /// run's sync state: checked = the run IS on the cloud. ON→OFF desyncs
  /// (light confirm → cloud delete; phone + device stay). OFF→ON uploads
  /// now (explicit intent, ignores the global push preference).
  Future<void> _showRunActions(SavedRun run) async {
    final isSynced = _cloudEntryForRun(run) != null;
    final action = await showModalBottomSheet<String>(
      context: context,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.edit_outlined),
              title: const Text('Rename'),
              onTap: () => Navigator.pop(ctx, 'rename'),
            ),
            SwitchListTile(
              secondary: const Icon(Icons.cloud_sync_outlined),
              title: const Text('Synch to cloud'),
              subtitle: Text(isSynced
                  ? 'Checked: this run is on the cloud'
                  : 'Unchecked: this run is not on the cloud'),
              value: isSynced,
              onChanged: (v) => Navigator.pop(ctx, v ? 'syncon' : 'syncoff'),
            ),
            ListTile(
              leading: const Icon(Icons.delete_outline),
              title: const Text('Delete…'),
              onTap: () => Navigator.pop(ctx, 'delete'),
            ),
          ],
        ),
      ),
    );
    if (!mounted || action == null) return;
    if (action == 'rename') {
      await _promptRename(run);
    } else if (action == 'syncon') {
      await _syncRunNow(run);
    } else if (action == 'syncoff') {
      if (await _confirmDesync('The run stays on this phone and on the '
          'device. Flipping the switch back on re-uploads it.')) {
        await _desyncRun(run);
      }
    } else if (action == 'delete') {
      await _confirmDeleteRun(run);
    }
  }

  /// Long-press menu for a merged-course tile — same three-item 1.72
  /// structure. Merged desync removes ONLY the merged cloud row; the two
  /// source runs keep their own cloud rows.
  Future<void> _showMergeActions(Map<String, dynamic> m) async {
    final isSynced = _cloudEntryForMerge(m) != null;
    final action = await showModalBottomSheet<String>(
      context: context,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.edit_outlined),
              title: const Text('Rename'),
              onTap: () => Navigator.pop(ctx, 'rename'),
            ),
            SwitchListTile(
              secondary: const Icon(Icons.cloud_sync_outlined),
              title: const Text('Synch to cloud'),
              subtitle: Text(isSynced
                  ? 'Checked: this merged course is on the cloud'
                  : 'Unchecked: not on the cloud'),
              value: isSynced,
              onChanged: (v) => Navigator.pop(ctx, v ? 'syncon' : 'syncoff'),
            ),
            ListTile(
              leading: const Icon(Icons.delete_outline),
              title: const Text('Delete…'),
              onTap: () => Navigator.pop(ctx, 'delete'),
            ),
          ],
        ),
      ),
    );
    if (!mounted || action == null) return;
    if (action == 'rename') {
      await _promptRenameMerge(m);
    } else if (action == 'syncon') {
      await _syncMergeNow(m);
    } else if (action == 'syncoff') {
      if (await _confirmDesync('Only the merged cloud row is removed — the '
          'merged course stays on the phone and its two source runs keep '
          'their cloud rows.')) {
        await _desyncMerge(m);
      }
    } else if (action == 'delete') {
      await _confirmDeleteMerge(m);
    }
  }

  /// F46 delete confirmation v3 (1.72, JP redesign 2026-10-27 13:45).
  /// The 1.71 Synch checkbox is GONE — delete never touches the cloud;
  /// desync (the Synch switch) is the ONLY cloud-removal path.
  ///  * Run IS synced → regular confirm, honestly scoped: phone + device
  ///    go, the cloud copy is kept.
  ///  * Run NOT synced → STRONG definitive warning + double confirm: with
  ///    no cloud copy this deletion removes the run EVERYWHERE and cannot
  ///    be undone.
  /// Deleting a run that sources a merge stays allowed: the merge keeps
  /// its stored gates/baro (and its cloud row).
  Future<void> _confirmDeleteRun(SavedRun run) async {
    final isSynced = _cloudEntryForRun(run) != null;
    final isMergeSource = _merges.any((m) =>
        m['aFile'] == run.fileName || m['bFile'] == run.fileName);
    final hasName = run.name != null && run.name!.isNotEmpty;
    final displayName = hasName
        ? run.name!
        : (_defaultNames[run.fileName] ?? 'A#${run.id}');
    final mergeNote = isMergeSource
        ? '\n\nMerged courses using this run keep their data.'
        : '';
    if (isSynced) {
      final confirmed = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: Text('Delete $displayName?'),
          content: Text(
            'Deletes the phone copy. The device copy is removed too '
            '(queued for the next connection if offline).\n\n'
            'The cloud copy is kept — use Synch to cloud to remove it.'
            '$mergeNote',
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Cancel'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(ctx, true),
              style: TextButton.styleFrom(foregroundColor: Colors.red),
              child: const Text('Delete'),
            ),
          ],
        ),
      );
      if (confirmed != true || !mounted) return;
      await _executeRunDelete(run);
      return;
    }
    // NOT synced → this deletion is definitive. STRONG warning, then a
    // second confirmation (double-confirm pattern).
    final first = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Delete $displayName?'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              '⚠ This deletes the run completely — no copy will remain '
              'anywhere (not on this phone, not on the device, not in the '
              'cloud). This cannot be undone.',
              style: TextStyle(fontWeight: FontWeight.w600),
            ),
            if (isMergeSource)
              const Padding(
                padding: EdgeInsets.only(top: 8),
                child: Text(
                  'Merged courses using this run keep their data.',
                  style: TextStyle(fontSize: 12),
                ),
              ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: TextButton.styleFrom(foregroundColor: Colors.red),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (first != true || !mounted) return;
    final second = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Really delete $displayName?'),
        content: const Text(
          'Final confirmation: the run will be gone forever. '
          'This cannot be undone.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: TextButton.styleFrom(foregroundColor: Colors.red),
            child: const Text('Delete forever'),
          ),
        ],
      ),
    );
    if (second != true || !mounted) return;
    await _executeRunDelete(run);
  }

  /// Delete = PHONE + DEVICE legs only (1.72: the cloud is NEVER touched
  /// by delete — desync via the Synch switch is the only cloud-removal
  /// path). Phone leg: index + file + queue entry (dead or alive — the
  /// run is gone, nothing may resurrect it) + rename outbox entries.
  /// Device leg (1.71): ALWAYS — live 'd' when connected, durable backlog
  /// otherwise, applied before any listing on the next connect.
  Future<void> _executeRunDelete(SavedRun run) async {
    final wasSynced = _cloudEntryForRun(run) != null;
    await _storage.delete(run);
    await UploadQueue.instance.removeFile(run.fileName);
    await UploadQueue.instance.removeRenamesForKey(run.fileName);
    _gateCache.remove(run.fileName);
    final deviceDeleted = await _deleteFromDevice(run);
    await _loadLocalRuns();
    if (mounted) setState(() {});
    if (mounted) {
      final deviceNote = deviceDeleted
          ? 'device copy removed'
          : 'device copy queued for removal';
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Run deleted from phone '
            '${wasSynced ? '· cloud copy kept' : '· no cloud copy existed'} '
            '· $deviceNote')),
      );
    }
  }

  /// F46 confirmation for a merged course, v3 (1.72): the Synch checkbox
  /// is gone — delete removes ONLY the phone merge entry (its two source
  /// runs stay) + the device note never applies (a merge has no device
  /// copy). Synced → regular confirm (cloud row kept); NOT synced →
  /// STRONG definitive warning + double confirm.
  Future<void> _confirmDeleteMerge(Map<String, dynamic> m) async {
    final isSynced = _cloudEntryForMerge(m) != null;
    final displayName = _mergeDisplayName(m);
    if (isSynced) {
      final confirmed = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: Text('Delete "$displayName"?'),
          content: const Text(
            'Only the merged course is removed — its two source runs stay '
            'on the phone. The cloud copy of the merged course is kept — '
            'use Synch to cloud to remove it.',
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Cancel'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(ctx, true),
              style: TextButton.styleFrom(foregroundColor: Colors.red),
              child: const Text('Delete'),
            ),
          ],
        ),
      );
      if (confirmed != true || !mounted) return;
      await _executeMergeDelete(m);
      return;
    }
    final first = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Delete "$displayName"?'),
        content: const Text(
          '⚠ This deletes the merged course completely — no copy will '
          'remain anywhere (not on this phone, not in the cloud). Its two '
          'source runs stay. This cannot be undone.',
          style: TextStyle(fontWeight: FontWeight.w600),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: TextButton.styleFrom(foregroundColor: Colors.red),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (first != true || !mounted) return;
    final second = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Really delete this merged course?'),
        content: const Text(
          'Final confirmation: the merged course will be gone forever. '
          'This cannot be undone.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: TextButton.styleFrom(foregroundColor: Colors.red),
            child: const Text('Delete forever'),
          ),
        ],
      ),
    );
    if (second != true || !mounted) return;
    await _executeMergeDelete(m);
  }

  /// Phone-only leg for a merged course — removed from merged_runs.json
  /// with allowShrink: true (the reserved explicit-deletion path). The
  /// merge's queue entry + rename outbox entries are dropped so the sync
  /// worker's merged backfill cannot resurrect it. 1.72: NO cloud leg —
  /// delete never touches the cloud.
  Future<void> _executeMergeDelete(Map<String, dynamic> m) async {
    final wasSynced = _cloudEntryForMerge(m) != null;
    // Reload from disk before shrinking: the in-memory list can be
    // stale (auto-sync merge pass) — never persist a blind shrink.
    final merges = await _storage.loadMerges();
    final aFile = m['aFile'] as String;
    final bFile = m['bFile'] as String;
    merges.removeWhere(
        (e) => e['aFile'] == aFile && e['bFile'] == bFile);
    await _storage.saveMerges(merges, allowShrink: true);
    _merges = merges;
    final key = UploadQueue.mergedQueueKey(aFile, bFile);
    await UploadQueue.instance.removeFile(key);
    await UploadQueue.instance.removeRenamesForKey(key);
    await _refreshDerivedState(_localRuns);
    if (mounted) setState(() {});
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Merged course deleted from phone '
            '${wasSynced ? '· cloud copy kept' : '· no cloud copy existed'}')),
      );
    }
  }

  /// F45: rename dialog. Empty input = back to default label.
  /// 1.61.1 fix: the dialog owns its TextEditingController in a
  /// StatefulWidget — disposing it in the caller right after pop crashed
  /// the app on Cancel (exit animation still had the TextField attached:
  /// use-after-dispose). Route widgets are disposed only AFTER the exit
  /// animation completes, so the lifecycle is now correct by construction.
  /// 1.70: when the run is confirmed uploaded (DONE queue entry with a
  /// cloud id), the rename is propagated via the persistent rename
  /// outbox + sync worker; clearing the label propagates the COMPUTED
  /// default name so the cloud row keeps matching the phone.
  Future<void> _promptRename(SavedRun run) async {
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => _RenameDialog(
        initialName: run.name,
        defaultHint: _defaultNames[run.fileName] ?? 'A#${run.id}',
      ),
    );
    if (result != null) {
      await _storage.renameRun(run.fileName, result);
      await _loadLocalRuns();
      await _propagateRunRename(run, result.trim());
    }
  }

  /// 1.70: queue a cloud rename when this run already lives in the cloud.
  /// [trimmedName] empty = the user cleared the custom label — the cloud
  /// then receives the computed default ("A#32"), mirroring the phone.
  Future<void> _propagateRunRename(SavedRun run, String trimmedName) async {
    final entry = UploadQueue.instance.forFile(run.fileName);
    if (entry == null ||
        entry.status != UploadStatus.done ||
        entry.runId == null) {
      return; // not uploaded (yet) — the next upload carries the name
    }
    final newName = trimmedName.isEmpty
        ? await _storage.defaultRunName(run)
        : trimmedName;
    await UploadQueue.instance.enqueueRename(
      target: RenameTarget.single,
      localKey: run.fileName,
      cloudRunId: entry.runId!,
      lockVersion: entry.lockVersion,
      newName: newName,
    );
    // ignore: unawaited_futures
    SyncWorker.instance.kick('run renamed');
  }

  // ── F21: device param editors (write via BLE, re-read, confirm) ──

  Future<void> _afterParamWrite(String what) async {
    _resetIdleDisconnectTimer();   // param write = activity
    try {
      _config = await _sgc?.readConfig();
    } catch (e) {
      debugPrint('[SGC] config re-read after $what write failed: $e');
    }
    if (mounted) setState(() {});
  }

  Future<void> _editDeviceName() async {
    final sgc = _sgc;
    if (sgc == null) return;
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => _RenameDialog(
        initialName: _config?.deviceName,
        defaultHint: '',
        hintOverride: 'Device name (≤ 20 chars) — used in Bluetooth advertising',
      ),
    );
    final name = result?.trim();
    if (name == null || name.isEmpty || name == _config?.deviceName) return;
    try {
      await sgc.writeDeviceName(name);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Device renamed to "$name" — advertising follows')));
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Rename failed: $e')));
      }
      return;
    }
    await _afterParamWrite('name');
  }

  Future<void> _editArmSide() async {
    final sgc = _sgc;
    if (sgc == null) return;
    final side = await showDialog<ArmSide>(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('Arm Side'),
        children: [
          for (final s in ArmSide.values)
            SimpleDialogOption(
              onPressed: () => Navigator.pop(ctx, s),
              child: Row(children: [
                Icon(s == ArmSide.right ? Icons.arrow_forward : Icons.arrow_back,
                    color: s == ArmSide.right ? Colors.red : Colors.blue),
                const SizedBox(width: 12),
                Text(s.label,
                    style: s == _config?.armSide
                        ? const TextStyle(fontWeight: FontWeight.bold)
                        : null),
              ]),
            ),
        ],
      ),
    );
    if (side == null || side == _config?.armSide) return;
    try {
      await sgc.writeArmSide(side);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Arm side write failed: $e')));
      }
      return;
    }
    await _afterParamWrite('arm side');
  }

  Future<void> _editDiscipline() async {
    final sgc = _sgc;
    if (sgc == null) return;
    final d = await showDialog<Discipline>(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('Discipline'),
        children: [
          for (final v in Discipline.values)
            SimpleDialogOption(
              onPressed: () => Navigator.pop(ctx, v),
              child: Text(v.label,
                  style: v == _config?.discipline
                      ? const TextStyle(fontWeight: FontWeight.bold)
                      : null),
            ),
        ],
      ),
    );
    if (d == null || d == _config?.discipline) return;
    try {
      await sgc.writeDiscipline(d);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Discipline write failed: $e')));
      }
      return;
    }
    await _afterParamWrite('discipline');
  }

  Widget _infoRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          // 1.70: widened from 120 — labels now distinguish storage
          // locations ("On device (flash slots)" / "Downloaded (phone)").
          SizedBox(width: 160, child: Text(label, style: const TextStyle(color: Colors.grey))),
          Expanded(child: Text(value, style: const TextStyle(fontWeight: FontWeight.w500))),
        ],
      ),
    );
  }
}

DecompressResult _decompressRunBackground(Uint8List data) {
  return Decompressor().decompressFull(data);
}

/// Bottom sheet that shows BLE scan results and updates reactively.
class _ScanSheet extends StatefulWidget {
  final List<BluetoothDevice> devices;
  const _ScanSheet({required this.devices});

  @override
  State<_ScanSheet> createState() => _ScanSheetState();
}

class _ScanSheetState extends State<_ScanSheet> {
  StreamSubscription<List<ScanResult>>? _sub;

  @override
  void initState() {
    super.initState();
    _sub = FlutterBluePlus.scanResults.listen((_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final list = widget.devices.toList();
    list.sort((a, b) {
      final aIsSgc = a.platformName.toLowerCase().contains('sgc');
      final bIsSgc = b.platformName.toLowerCase().contains('sgc');
      if (aIsSgc && !bIsSgc) return -1;
      if (!aIsSgc && bIsSgc) return 1;
      return 0;
    });

    return DraggableScrollableSheet(
      initialChildSize: 0.45,
      minChildSize: 0.25,
      maxChildSize: 0.8,
      expand: false,
      builder: (ctx, scrollController) {
        return Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(children: [
                const Text('BLE Devices',
                    style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                const Spacer(),
                const SizedBox(width: 20, height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2)),
                const SizedBox(width: 8),
                IconButton(
                    icon: const Icon(Icons.close),
                    onPressed: () => Navigator.pop(ctx)),
              ]),
              const SizedBox(height: 8),
              Text('Found ${list.length} device(s)',
                  style: const TextStyle(color: Colors.grey)),
              const Divider(),
              Expanded(
                child: list.isEmpty
                    ? const Center(
                        child: Text('No devices found yet',
                            style: TextStyle(color: Colors.grey, fontSize: 16)))
                    : ListView.builder(
                        controller: scrollController,
                        itemCount: list.length,
                        itemBuilder: (_, i) {
                          final d = list[i];
                          return ListTile(
                            leading: const Icon(Icons.devices),
                            title: Text(d.platformName.isNotEmpty
                                ? d.platformName
                                : 'Unknown Device'),
                            subtitle: Text(d.remoteId.str),
                            onTap: () => Navigator.pop(ctx, d),
                          );
                        },
                      ),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// F45 rename dialog (1.61.1): StatefulWidget so the TextEditingController
/// lives and dies with the route widget — disposed only after the exit
/// animation completes. Pops with the typed text (Save/Enter) or null
/// (Cancel / back).
class _RenameDialog extends StatefulWidget {
  final String? initialName;
  final String defaultHint;
  final String? hintOverride;   // F21: full custom hint (device name)

  const _RenameDialog({
    required this.initialName,
    required this.defaultHint,
    this.hintOverride,
  });

  @override
  State<_RenameDialog> createState() => _RenameDialogState();
}

class _RenameDialogState extends State<_RenameDialog> {
  late final TextEditingController _ctrl;

  @override
  void initState() {
    super.initState();
    _ctrl = TextEditingController(text: widget.initialName ?? '');
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Name this run'),
      content: TextField(
        controller: _ctrl,
        autofocus: true,
        maxLength: 40,
        decoration: InputDecoration(
          hintText: widget.hintOverride ??
              'e.g. Race simulation · empty = ${widget.defaultHint}',
        ),
        onSubmitted: (_) => Navigator.pop(context, _ctrl.text),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context, _ctrl.text),
          child: const Text('Save'),
        ),
      ],
    );
  }
}
