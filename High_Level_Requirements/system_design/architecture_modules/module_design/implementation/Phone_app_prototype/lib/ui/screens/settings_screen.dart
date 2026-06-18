import 'package:flutter/material.dart';

class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        children: [
          ListTile(
            leading: const Icon(Icons.person),
            title: const Text('Athlete Profile'),
            subtitle: const Text('Name, club, category'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () {},
          ),
          ListTile(
            leading: const Icon(Icons.bluetooth),
            title: const Text('BLE Devices'),
            subtitle: const Text('Manage paired devices'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () {},
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.cloud_upload),
            title: const Text('Cloud Sync'),
            subtitle: const Text('Upload runs to team cloud'),
            trailing: Switch(value: false, onChanged: (_) {}),
          ),
          const Divider(),
          ListTile(
            leading: const Icon(Icons.info_outline),
            title: const Text('About'),
            subtitle: const Text('Ski Gate Chrono v1.0'),
            onTap: () {},
          ),
        ],
      ),
    );
  }
}
