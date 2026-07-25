import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../../ble/ble_manager.dart' hide ScanResult;
import '../../ble/sgc_service.dart';
import '../../ble/file_transfer.dart';
import '../../processing/decompressor.dart';
import '../../storage/local_storage.dart';
import '../../config/app_version.dart';
import '../../models/device_config.dart';
import 'run_detail_screen.dart';

class RunListScreen extends StatefulWidget {
  const RunListScreen({super.key});
  @override
  State<RunListScreen> createState() => _RunListScreenState();
}

class _RunListScreenState extends State<RunListScreen> {
  final _ble = BLEManager();
  final _storage = LocalStorage();
  SGCService? _sgc;
  DeviceConfig? _config;
  int _runCount = 0;
  bool _isConnecting = false;
  bool _isScanning = false;
  bool _isDownloading = false;
  List<SavedRun> _localRuns = [];

  @override
  void initState() {
    super.initState();
    _loadLocalRuns();
  }

  @override
  void dispose() {
    FlutterBluePlus.stopScan();  // clean up if tab switched mid-scan
    _ble.dispose();
    super.dispose();
  }

  Future<void> _scanDevices() async {
    if (_isScanning) return;
    _isScanning = true;

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
    } catch (e) {
      setState(() => _isConnecting = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Connection failed: ${e.toString().split('\n').first}'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  Future<void> _disconnect() async {
    await _ble.disconnect();
    setState(() {
      _sgc = null;
      _config = null;
      _runCount = 0;
    });
  }

  Future<void> _loadLocalRuns() async {
    final runs = await _storage.listAll();
    if (mounted) setState(() => _localRuns = runs);
  }

  Future<void> _openLocalRun(SavedRun run) async {
    final data = await _storage.load(run.fileName);
    if (data == null || !mounted) return;

    final decompressor = Decompressor();
    final crcOk = decompressor.validateCRC(data);
    debugPrint('[SGC] ${crcOk ? "✅" : "⚠️"} Local CRC for ${run.fileName}: ${crcOk ? "OK" : "FAILED"}');
    final decoded = decompressor.decompressFull(data);

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
    setState(() => _isDownloading = true);

    try {
      final ft = FileTransfer(_sgc!);

      // Fetch run list from device
      final runs = await ft.getRunList();
      debugPrint('[SGC] Run list: ${runs.length} runs');

      if (runs.isEmpty) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('No runs on device')),
          );
        }
        return;
      }

      // Download the latest run (last in list)
      final latest = runs.last;
      debugPrint('[SGC] Downloading run #${latest.id} (${latest.size} bytes, side=${latest.side})');

      // Download with up to one retry on CRC failure (BLE GATT notification loss)
      TransferResult? result;
      bool crcOk = false;
      const maxAttempts = 2;
      for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        result = await ft.download(latest.id);

        if (result.compressedData.isEmpty) {
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('Download failed — empty data')),
            );
          }
          return;
        }

        final decompressor = Decompressor();
        crcOk = decompressor.validateCRC(result.compressedData);
        if (crcOk) {
          debugPrint('[SGC] ✅ CRC valid for run #${latest.id} (attempt $attempt)');
          break;
        }
        debugPrint('[SGC] ⚠️ CRC FAILED for run #${latest.id} (attempt $attempt/${maxAttempts})');
        if (attempt < maxAttempts) {
          debugPrint('[SGC]    Retrying download...');
          await Future.delayed(const Duration(milliseconds: 500));
        }
      }

      if (!crcOk) {
        debugPrint('[SGC] ⚠️ CRC STILL FAILED after $maxAttempts attempts for run #${latest.id} — download aborted');
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(
              content: Text('⚠️ Download corrupted — try again. Firmware chunk rate may need reduction.'),
              backgroundColor: Colors.orange,
              duration: Duration(seconds: 5),
            ),
          );
        }
        setState(() => _isDownloading = false);
        return; // Don't decompress or navigate on corrupt data
      }

      // Decompress
      final decompressor = Decompressor();
      final decoded = decompressor.decompressFull(result!.compressedData);

      // Save to local storage
      await _storage.save(
        runId: latest.id,
        compressedData: result.compressedData,
        result: decoded,
        deviceName: _config?.deviceName ?? 'SGC',
      );

      // Reload local runs list
      await _loadLocalRuns();

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              'Downloaded run #${latest.id}: '
              '${decoded.frameCount} frames '
              '(${decoded.totalDurationSec.toStringAsFixed(1)}s)',
            ),
            backgroundColor: Colors.green,
          ),
        );

        // Navigate to detail screen
        Navigator.of(context).push(
          MaterialPageRoute(
            builder: (_) => RunDetailScreen(
              result: decoded,
              deviceName: _config?.deviceName,
              armSide: _config?.armSide,
            ),
          ),
        );
      }

      debugPrint('[SGC] Run header: fmt=${decoded.header.formatVersion} '
          'side=${decoded.header.armSide} '
          'cal=${decoded.header.calAccuracy} '
          'temp=${decoded.header.baroTempC.toStringAsFixed(1)}°C');
      debugPrint('[SGC] Frames: ${decoded.frameCount}, '
          'duration=${decoded.totalDurationSec.toStringAsFixed(1)}s');
    } catch (e, stack) {
      debugPrint('[SGC] Download error: $e');
      debugPrint('[SGC] $stack');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Download error: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _isDownloading = false);
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
            title: const Text('Download Latest Run'),
            subtitle: Text(_isDownloading ? 'Transferring…' : 'Transfer runs from device'),
            trailing: const Icon(Icons.chevron_right),
            onTap: (_runCount > 0 && !_isDownloading) ? _downloadRuns : null,
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
