import 'package:flutter/material.dart';
import '../../config/app_version.dart';
import 'run_list_screen.dart';
import 'course_setup_screen.dart';
import 'settings_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});
  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _selectedIndex = 0;

  static const _screens = [
    _Tab('Runs', Icons.timer, RunListScreen()),
    _Tab('Course', Icons.flag, CourseSetupScreen()),
    _Tab('Settings', Icons.settings, SettingsScreen()),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: IndexedStack(
        index: _selectedIndex,
        children: _screens.map((t) => t.screen).toList(),
      ),
      bottomNavigationBar: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          NavigationBar(
            selectedIndex: _selectedIndex,
            onDestinationSelected: (i) => setState(() => _selectedIndex = i),
            destinations: _screens
                .map((t) => NavigationDestination(icon: Icon(t.icon), label: t.label))
                .toList(),
          ),
          Container(
            width: double.infinity,
            padding: const EdgeInsets.only(bottom: 4),
            child: Text(
              'SGC v$APP_VERSION',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 10, color: Colors.grey.shade500),
            ),
          ),
        ],
      ),
    );
  }
}

class _Tab {
  final String label;
  final IconData icon;
  final Widget screen;
  const _Tab(this.label, this.icon, this.screen);
}
