import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// JP 2026-10-26: Settings > BLE Devices was a dead tile (onTap: {}).
/// This screen lists the paired devices (auto-sync targets, stored as
/// MAC list under SharedPreferences key below — same key the run list
/// uses to auto-connect) and lets the user remove ("forget") them.
class BleDevicesScreen extends StatefulWidget {
  const BleDevicesScreen({super.key});

  @override
  State<BleDevicesScreen> createState() => _BleDevicesScreenState();
}

class _BleDevicesScreenState extends State<BleDevicesScreen> {
  static const _prefsPairedMacs = 'sgc.paired_macs';
  List<String> _macs = [];

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    if (!mounted) return;
    setState(() {
      _macs = (prefs.getStringList(_prefsPairedMacs) ?? []).toList();
    });
  }

  Future<void> _persist(List<String> next) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList(_prefsPairedMacs, next);
    _load();
  }

  Future<void> _confirmRemove(String mac) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Remove $mac?'),
        content: const Text(
            'This device will no longer be an auto-sync target. '
            'You can pair it again any time by connecting to it.'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('Keep')),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Remove'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    _persist(_macs.where((m) => m != mac).toList());
  }

  Future<void> _confirmRemoveAll() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Remove all paired devices?'),
        content: Text(
            '${_macs.length} device(s) will stop being auto-sync targets.'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('Keep')),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: Colors.red),
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Remove all'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    _persist([]);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('BLE Devices'),
        actions: [
          if (_macs.isNotEmpty)
            IconButton(
              icon: const Icon(Icons.delete_sweep),
              tooltip: 'Remove all',
              onPressed: _confirmRemoveAll,
            ),
        ],
      ),
      body: _macs.isEmpty
          ? const Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.bluetooth_disabled,
                      size: 64, color: Colors.grey),
                  SizedBox(height: 16),
                  Text('No paired devices',
                      style:
                          TextStyle(fontSize: 18, color: Colors.grey)),
                  SizedBox(height: 8),
                  Text(
                      'Connect a device to pair it — it then becomes an
'
                      'auto-sync target when the app is open.',
                      textAlign: TextAlign.center,
                      style: TextStyle(color: Colors.grey)),
                ],
              ),
            )
          : ListView(
              children: [
                Padding(
                  padding: const EdgeInsets.all(16),
                  child: Text(
                      'Paired devices are auto-sync targets: when the app '
                      'is open and auto-sync armed, it connects to them '
                      'and downloads new runs automatically.',
                      style: TextStyle(
                          fontSize: 13, color: Colors.grey.shade600)),
                ),
                for (final mac in _macs)
                  ListTile(
                    leading: const Icon(Icons.bluetooth),
                    title: Text(mac, style: const TextStyle(
                        fontFamily: 'monospace', fontSize: 14)),
                    subtitle: const Text('Auto-sync target'),
                    trailing: IconButton(
                      icon: const Icon(Icons.remove_circle_outline,
                          color: Colors.red),
                      tooltip: 'Remove',
                      onPressed: () => _confirmRemove(mac),
                    ),
                  ),
              ],
            ),
    );
  }
}
