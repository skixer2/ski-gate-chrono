import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../../config/app_version.dart';
import '../../cloud/auth_store.dart';
import '../../cloud/upload_queue.dart';
import 'profile_screen.dart';
import 'ble_devices_screen.dart';
import 'cloud_account_screen.dart';
import 'cloud_runs_screen.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});
  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  bool _autoSync = true;
  int _pairedCount = 0;
  String _profileName = '';
  bool _push = true;   // F20: bound to the profile's push toggle
  String _cloudName = '';   // P1: signed-in account name ('' = not signed in)
  int _pendingUploads = 0;

  static const _prefsAutoSync = 'sgc.autosync';
  static const _prefsPairedMacs = 'sgc.paired_macs';
  static const _prefsPush = 'sgc.profile.push';
  static const _prefsProfileName = 'sgc.profile.name';

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final session = await AuthStore.instance.load();
    await UploadQueue.instance.load();
    if (!mounted) return;
    setState(() {
      _autoSync = prefs.getBool(_prefsAutoSync) ?? true;
      _pairedCount = (prefs.getStringList(_prefsPairedMacs) ?? []).length;
      _push = prefs.getBool(_prefsPush) ?? true;
      _profileName = prefs.getString(_prefsProfileName) ?? '';
      _cloudName = session?.user.name ?? '';
      _pendingUploads = UploadQueue.instance.pendingCount;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        children: [
          // ── Auto-sync (v1.58, JP directive 2026-10-13) ──────────────
          SwitchListTile(
            secondary: const Icon(Icons.sync),
            title: const Text('Auto-sync'),
            subtitle: Text(_autoSync
                ? 'Connects paired devices ($_pairedCount), downloads '
                    'missing runs, merges pairs, then quiet 5 min'
                : 'Off — manual scan only'),
            value: _autoSync,
            onChanged: (v) async {
              final prefs = await SharedPreferences.getInstance();
              await prefs.setBool(_prefsAutoSync, v);
              if (!mounted) return;
              setState(() => _autoSync = v);
            },
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.person),
            title: const Text('Athlete Profile'),
            subtitle: Text(_profileName.isEmpty
                ? 'Name, club, category — not set'
                : _profileName),
            trailing: const Icon(Icons.chevron_right),
            onTap: () async {
              await Navigator.of(context).push(MaterialPageRoute(
                  builder: (_) => const ProfileScreen()));
              _load();   // refresh tile + push state on return
            },
          ),
          ListTile(
            leading: const Icon(Icons.bluetooth),
            title: const Text('BLE Devices'),
            subtitle: Text('$_pairedCount paired (auto-sync targets)'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () async {
              await Navigator.of(context).push(MaterialPageRoute(
                  builder: (_) => const BleDevicesScreen()));
              _load();   // refresh paired count on return
            },
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.cloud_upload),
            title: const Text('Cloud Sync'),
            subtitle: Text(_push
                ? 'Upload runs to team cloud (profile attribution)'
                : 'Off — runs stay on this phone'),
            trailing: Switch(
              value: _push,
              onChanged: (v) async {
                final prefs = await SharedPreferences.getInstance();
                await prefs.setBool(_prefsPush, v);
                if (!mounted) return;
                setState(() => _push = v);
              },
            ),
          ),
          ListTile(
            leading: Icon(
              _cloudName.isEmpty && _push
                  ? Icons.cloud_off
                  : Icons.cloud_outlined,
              color: _cloudName.isEmpty && _push ? Colors.orange : null,
            ),
            title: const Text('Cloud Account'),
            subtitle: Text(_cloudName.isEmpty
                ? (_push
                    ? '⚠ Push is ON but not signed in — runs queue locally. '
                        'Tap to sign in or register'
                    : 'Not signed in')
                : '$_cloudName · $_pendingUploads run(s) queued'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () async {
              await Navigator.of(context).push(MaterialPageRoute(
                  builder: (_) => const CloudAccountScreen()));
              _load();   // refresh name / queue depth on return
            },
          ),
          ListTile(
            leading: const Icon(Icons.cloud_sync_outlined),
            title: const Text('Cloud Runs'),
            subtitle: const Text('Browse your uploaded runs (read-only)'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () {
              Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const CloudRunsScreen()));
            },
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.info_outline),
            title: const Text('About'),
            subtitle: Text('Ski Gate Chrono v$APP_VERSION'),
            onTap: () {},
          ),
        ],
      ),
    );
  }
}
