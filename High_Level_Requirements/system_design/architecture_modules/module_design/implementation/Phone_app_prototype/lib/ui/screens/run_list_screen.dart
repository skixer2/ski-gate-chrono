import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../../models/gate_side.dart';
import '../../ble/ble_manager.dart' hide ScanResult;
import '../../ble/sgc_service.dart';
import '../../ble/native_ble_downloader.dart';
import '../../ble/file_transfer.dart';
import '../../processing/decompressor.dart';
import '../../processing/run_merger.dart';
import '../../storage/local_storage.dart';
import '../../config/app_version.dart';
import '../../models/device_config.dart';
import 'run_detail_screen.dart';
import 'merged_run_screen.dart';

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
  String? _dateFilter;   // JP 2026-10-26 #8: filter runs by date
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
              );
              saved++;
              await _loadLocalRuns();
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
    if (merged.length != _merges.length) {
      await _storage.saveMerges(merged);
      _merges = merged;
      if (mounted) setState(() {});
    }
    // Backfill: entries created before 1.59 lack the averaged baro profile.
    await _backfillMergedBaro();
  }

  Future<void> _backfillMergedBaro() async {
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
      final sgc = SGCService(_ble);

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
        final ft = FileTransfer(sgc);
        _deviceRuns = await ft.getRunList();
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
        _deviceRuns = [];
      });
    }
  }

  Future<void> _loadLocalRuns() async {
    final runs = await _storage.listAll();
    if (mounted) setState(() => _localRuns = runs);
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
          );
          ok++;
          // Keep the missing-run identity current while the batch is still
          // running; also makes a later cancel leave an accurate screen.
          await _loadLocalRuns();
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
          );
          saved++;
          await _loadLocalRuns();
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
          if (_localRuns.isNotEmpty)
            PopupMenuButton<String?>(
              tooltip: 'Filter by date',
              icon: Icon(Icons.filter_list,
                  color: _dateFilter == null
                      ? null
                      : Theme.of(context).colorScheme.primary),
              initialValue: _dateFilter,
              onSelected: (v) => setState(() =>
                  _dateFilter = (v == null || v.isEmpty) ? null : v),
              itemBuilder: (_) => [
                const PopupMenuItem(value: null, child: Text('All dates')),
                ..._dateOptions.map((d) =>
                    PopupMenuItem(value: d, child: Text(d))),
              ],
            ),
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
                    if (_merges.isNotEmpty) ...[
                      Padding(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 4, vertical: 4),
                        child: Text('Merged Courses (${_merges.length})',
                            style: TextStyle(
                                fontSize: 14,
                                fontWeight: FontWeight.w600,
                                color: Colors.grey.shade700)),
                      ),
                      for (final m in _merges)
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
                            onLongPress: () => _promptRenameMerge(m),
                          ),
                        ),
                      const SizedBox(height: 12),
                    ],
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
                      child: Text('Saved Runs (${_dateFilter == null ? _localRuns.length : _filteredLocalRuns.length}${_dateFilter == null ? '' : ' of ${_localRuns.length}'})',
                          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey.shade700)),
                    ),
                    ..._filteredLocalRuns.map(_buildLocalRunTile),
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
                _infoRow('Runs on Device', _runCount?.toString() ?? '? (read failed)'),
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
                : Text('${_deviceRuns.length} on device · ${_missingRuns.length} not downloaded'),
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
        // ── Local runs section ──
        if (_localRuns.isNotEmpty) ...[
          const SizedBox(height: 16),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
            child: Text('Saved Runs (${_dateFilter == null ? _localRuns.length : _filteredLocalRuns.length}${_dateFilter == null ? '' : ' of ${_localRuns.length}'})',
                style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey.shade700)),
          ),
          ..._filteredLocalRuns.map(_buildLocalRunTile),
        ],
      ],
    );
  }

  Widget _buildLocalRunTile(SavedRun run) {
    final dateStr = DateTime.fromMillisecondsSinceEpoch(run.timestamp * 1000, isUtc: true)
        .toLocal().toString().substring(0, 16);
    final durStr = '${(run.durationSec ~/ 60).toString().padLeft(2, '0')}:'
        '${(run.durationSec % 60).toStringAsFixed(1).padLeft(4, '0')}';
    // F45: user name if set, else default "date + run_number" label.
    final hasName = run.name != null && run.name!.isNotEmpty;
    final title = hasName
        ? run.name!
        : '${_defaultRunLabel(run)} — $durStr';
    final subtitle = hasName
        ? 'Run #${run.id} · $durStr · $dateStr · ${run.frameCount} frames · ${run.deviceName}'
        : '$dateStr · ${run.frameCount} frames · ${run.deviceName}';

    return Card(
      child: ListTile(
        leading: Icon(
          run.side == 'right' ? Icons.arrow_forward : Icons.arrow_back,
          color: Colors.blue.shade400,
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
        onLongPress: () => _promptRename(run),
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

  /// Distinct local dates present in the local run set (newest first).
  List<String> get _dateOptions => _localRuns
      .map((r) => _dayKeyOf(r.timestamp))
      .toSet()
      .toList()
      ..sort((a, b) => b.compareTo(a));

  /// Local runs after the active date filter (null = all).
  List<SavedRun> get _filteredLocalRuns => _dateFilter == null
      ? _localRuns
      : _localRuns
          .where((r) => _dayKeyOf(r.timestamp) == _dateFilter)
          .toList();

  /// F45 (JP 2026-10-26 #8 revision): default label = 1-based ordinal
  /// that day (ordered by timestamp, then id). Deterministic from the
  /// current run set — no extra storage.
  String _defaultRunLabel(SavedRun run) {
    final day = DateTime.fromMillisecondsSinceEpoch(run.timestamp * 1000, isUtc: true)
        .toLocal();
    final dayKey = '${day.year.toString().padLeft(4, '0')}-'
        '${day.month.toString().padLeft(2, '0')}-'
        '${day.day.toString().padLeft(2, '0')}';
    final sameDay = _localRuns.where((r) {
      final d = DateTime.fromMillisecondsSinceEpoch(r.timestamp * 1000, isUtc: true)
          .toLocal();
      return '${d.year.toString().padLeft(4, '0')}-'
              '${d.month.toString().padLeft(2, '0')}-'
              '${d.day.toString().padLeft(2, '0')}' == dayKey;
    }).toList()
      ..sort((a, b) {
        final c = a.timestamp.compareTo(b.timestamp);
        return c != 0 ? c : a.id.compareTo(b.id);
      });
    final n = sameDay.indexWhere((r) => r.fileName == run.fileName) + 1;
    // JP 2026-10-26 #8: date is visible on the tile + filterable, so the
    // default name carries just the daily run number.
    return 'Run $n';
  }

  /// F45 (merged extension, JP 2026-10-23): user-editable name for merged
  /// courses. Default label = the pair identity ("Run #12 + #13"); a set
  /// name takes the title, the pair identity moves to the subtitle.
  bool _mergeHasName(Map<String, dynamic> m) =>
      (m['name'] as String?)?.isNotEmpty == true;

  String _mergeDisplayName(Map<String, dynamic> m) =>
      _mergeHasName(m) ? m['name'] as String : 'Run #${m['aId']} + #${m['bId']}';

  Future<void> _promptRenameMerge(Map<String, dynamic> m) async {
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => _RenameDialog(
        initialName: m['name'] as String?,
        defaultHint: 'Run #${m['aId']} + #${m['bId']}',
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
  }

  /// F45: rename dialog. Empty input = back to default label.
  /// 1.61.1 fix: the dialog owns its TextEditingController in a
  /// StatefulWidget — disposing it in the caller right after pop crashed
  /// the app on Cancel (exit animation still had the TextField attached:
  /// use-after-dispose). Route widgets are disposed only AFTER the exit
  /// animation completes, so the lifecycle is now correct by construction.
  Future<void> _promptRename(SavedRun run) async {
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => _RenameDialog(
        initialName: run.name,
        defaultHint: _defaultRunLabel(run),
      ),
    );
    if (result != null) {
      await _storage.renameRun(run.fileName, result);
      await _loadLocalRuns();
    }
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
          SizedBox(width: 120, child: Text(label, style: const TextStyle(color: Colors.grey))),
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
