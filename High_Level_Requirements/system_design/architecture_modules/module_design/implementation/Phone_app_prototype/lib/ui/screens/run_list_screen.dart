import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../../ble/ble_manager.dart' hide ScanResult;
import '../../ble/sgc_service.dart';
import '../../models/device_config.dart';

class RunListScreen extends StatefulWidget {
  const RunListScreen({super.key});
  @override
  State<RunListScreen> createState() => _RunListScreenState();
}

class _RunListScreenState extends State<RunListScreen> {
  final _ble = BLEManager();
  SGCService? _sgc;
  DeviceConfig? _config;
  int _runCount = 0;
  bool _isConnecting = false;
  bool _isScanning = false;

  @override
  void dispose() {
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

      try {
        await sgc.syncTime();
        _config = await sgc.readConfig();
        _runCount = await sgc.getRunCount();
      } catch (_) {}

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

  @override
  Widget build(BuildContext context) {
    final isConnected = _sgc != null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Runs'),
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
          : const Center(
              child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                Icon(Icons.timer_off, size: 64, color: Colors.grey),
                SizedBox(height: 16),
                Text('No runs yet', style: TextStyle(fontSize: 18, color: Colors.grey)),
                SizedBox(height: 8),
                Text('Connect a device via BLE to download runs',
                    style: TextStyle(color: Colors.grey)),
              ]),
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
            leading: const Icon(Icons.download),
            title: const Text('Download Runs'),
            subtitle: const Text('Transfer runs from device'),
            trailing: const Icon(Icons.chevron_right),
            onTap: _runCount > 0
                ? () {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(content: Text('File transfer coming soon')),
                    );
                  }
                : null,
          ),
        ),
      ],
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
