import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../../ble/ble_manager.dart' hide ScanResult;
import '../../ble/sgc_service.dart';
import '../../ble/native_ble_downloader.dart';
import '../../ble/file_transfer.dart';
import '../../processing/decompressor.dart';
import '../../storage/local_storage.dart';
import '../../config/app_version.dart';
import '../../models/device_config.dart';
import 'run_detail_screen.dart';

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

class _RunListScreenState extends State<RunListScreen> {
  final _ble = BLEManager();
  final _storage = LocalStorage();
  SGCService? _sgc;
  BluetoothDevice? _device;   // V1.22: kept for per-run reconnect (Phase 2)
  DeviceConfig? _config;
  int _runCount = 0;
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

  @override
  void initState() {
    super.initState();
    _loadLocalRuns();
    // React to unexpected BLE drops so the UI is never stuck "connected".
    _connSub = _ble.connectionStream.listen(_onConnectionChanged);
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
    if (!mounted) return;
    setState(() {
      _sgc = null;
      _config = null;
      _runCount = 0;
      _deviceRuns = [];
      _isConnecting = false;
      _isDownloading = false;
    });
  }

  @override
  void dispose() {
    _connSub?.cancel();
    FlutterBluePlus.stopScan();  // clean up if tab switched mid-scan
    _ble.dispose();
    super.dispose();
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
          _runCount = 0;
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

    try {
      await _ble.connectToDevice(device);
      _device = device;  // V1.22: keep for per-run reconnect
      final sgc = SGCService(_ble);

      // Dump discovered services so we can see what the device actually exposes
      sgc.debugLogServices();

      try {
        debugPrint('[SGC] syncing time…');
        await sgc.syncTime();
        debugPrint('[SGC] reading config…');
        _config = await sgc.readConfig();
        debugPrint('[SGC] config: ${_config?.deviceName} / ${_config?.armSide.label} / ${_config?.discipline.label}');
        debugPrint('[SGC] reading run count…');
        _runCount = await sgc.getRunCount();
        debugPrint('[SGC] run count = $_runCount');
        final ft = FileTransfer(sgc);
        _deviceRuns = await ft.getRunList();
        debugPrint('[SGC] device runs: ${_deviceRuns.length}');
      } catch (e, stack) {
        debugPrint('[SGC] ERROR during setup: $e');
        debugPrint('[SGC] stack: $stack');
      }

      setState(() {
        _sgc = sgc;
        _isConnecting = false;
      });

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Connected to ${device.platformName}'),
            backgroundColor: Colors.green,
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
    _isDownloading = false;  // V1.23: cancel any in-flight batch first
    await _ble.disconnect();
    if (mounted) {
      setState(() {
        _sgc = null;
        _device = null;
        _config = null;
        _runCount = 0;
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
        actions: isConnected
            ? [
                IconButton(
                  icon: const Icon(Icons.bluetooth_connected, color: Colors.green),
                  tooltip: 'Disconnect',
                  onPressed: _disconnect,
                ),
              ]
            : null,
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
                    const SizedBox(height: 12),
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
                      child: Text('Saved Runs (${_localRuns.length})',
                          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey.shade700)),
                    ),
                    ..._localRuns.map(_buildLocalRunTile),
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
                _infoRow('Arm Side', _config?.armSide.label ?? 'Unknown'),
                _infoRow('Discipline', _config?.discipline.label ?? 'Unknown'),
                _infoRow('Runs on Device', _runCount.toString()),
                _infoRow('Not Downloaded', _missingRuns.length.toString()),
                _infoRow('BLE MTU', '${_ble.mtu} bytes'),
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
            onTap: (_missingRuns.isNotEmpty && !_isDownloading) ? _downloadRuns : null,
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
            child: Text('Saved Runs (${_localRuns.length})',
                style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey.shade700)),
          ),
          ..._localRuns.map(_buildLocalRunTile),
        ],
      ],
    );
  }

  Widget _buildLocalRunTile(SavedRun run) {
    final dateStr = DateTime.fromMillisecondsSinceEpoch(run.timestamp * 1000, isUtc: true)
        .toLocal().toString().substring(0, 16);
    final durStr = '${(run.durationSec ~/ 60).toString().padLeft(2, '0')}:'
        '${(run.durationSec % 60).toStringAsFixed(1).padLeft(4, '0')}';

    return Card(
      child: ListTile(
        leading: Icon(
          run.side == 'right' ? Icons.arrow_forward : Icons.arrow_back,
          color: Colors.blue.shade400,
        ),
        title: Text('Run #${run.id} — $durStr',
            style: const TextStyle(fontWeight: FontWeight.w500)),
        subtitle: Text('$dateStr · ${run.frameCount} frames · ${run.deviceName}'),
        trailing: const Icon(Icons.chevron_right, color: Colors.grey),
        onTap: () => _openLocalRun(run),
      ),
    );
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
