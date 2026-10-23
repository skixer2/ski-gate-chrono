import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../../config/app_version.dart';
import 'profile_screen.dart';

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
    if (!mounted) return;
    setState(() {
      _autoSync = prefs.getBool(_prefsAutoSync) ?? true;
      _pairedCount = (prefs.getStringList(_prefsPairedMacs) ?? []).length;
      _push = prefs.getBool(_prefsPush) ?? true;
      _profileName = prefs.getString(_prefsProfileName) ?? '';
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
            onTap: () {},
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
