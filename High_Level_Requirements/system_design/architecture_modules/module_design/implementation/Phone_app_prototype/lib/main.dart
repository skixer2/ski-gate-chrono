import 'package:flutter/material.dart';
import 'cloud/api_client.dart';
import 'cloud/sync_worker.dart';
import 'storage/device_delete_backlog.dart';
import 'ui/screens/home_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // P1 cloud (F23/F43/F48/F50): bootstrap indirection (I07) loads the
  // cached api_base and refreshes /bootstrap-info in the background; the
  // sync worker loads the durable upload queue, arms the connectivity
  // trigger and kicks the first pass (3 s in, after first frame).
  // Both are fire-and-forget — offline start must never block the UI.
  ApiClient.instance.init();
  SyncWorker.instance.init();
  // 1.71 (F46 v2): device-delete backlog — durable "delete on device"
  // queue applied on every connect before any listing/download. Same
  // fire-and-forget discipline (every consumer awaits load() anyway).
  DeviceDeleteBacklog.instance.load();
  runApp(const SGCApp());
}

class SGCApp extends StatelessWidget {
  const SGCApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Ski Gate Chrono',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blue, brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const HomeScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}
