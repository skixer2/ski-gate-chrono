import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// BLE permissions check result.
enum BLEPermissionStatus {
  granted,
  denied,
  bluetoothOff,
  unsupported,
}

class BLEManager {
  final _stateController = StreamController<BluetoothAdapterState>.broadcast();
  Stream<BluetoothAdapterState> get stateStream => _stateController.stream;
  BluetoothDevice? _device;
  bool _connected = false;
  int _mtu = 23;

  bool get isConnected => _connected;
  int get mtu => _mtu;

  /// Check and request BLE permissions + Bluetooth status.
  /// Returns [BLEPermissionStatus.granted] if ready to scan.
  Future<BLEPermissionStatus> checkPermissions() async {
    // Check adapter availability
    final adapter = FlutterBluePlus.adapterStateNow;
    if (adapter == BluetoothAdapterState.unauthorized) {
      return BLEPermissionStatus.denied;
    }
    if (adapter == BluetoothAdapterState.off) {
      return BLEPermissionStatus.bluetoothOff;
    }
    if (adapter == BluetoothAdapterState.unsupported) {
      return BLEPermissionStatus.unsupported;
    }
    // adapterStateNow == on means permissions are granted on Android 12+
    return BLEPermissionStatus.granted;
  }

  Future<List<ScanResult>> scan() async {
    final results = <ScanResult>[];
    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    final sub = FlutterBluePlus.scanResults.listen((r) {
      for (final s in r) {
        results.add(ScanResult(
          id: s.device.remoteId.str,
          name: s.device.platformName,
          rssi: s.rssi,
        ));
      }
    });
    await Future.delayed(const Duration(seconds: 10));
    await sub.cancel();
    await FlutterBluePlus.stopScan();
    return results;
  }

  Future<void> connect(String deviceId) async {
    _device = BluetoothDevice(remoteId: DeviceIdentifier(deviceId));
    await _device!.connect(autoConnect: false);
    _mtu = await _device!.requestMtu(247);
    _connected = true;
  }

  /// Connect directly to a device from scan results (preferred).
  Future<void> connectToDevice(BluetoothDevice device) async {
    _device = device;
    await _device!.connect(autoConnect: false);
    _mtu = await _device!.requestMtu(247);
    _connected = true;
  }

  Future<void> disconnect() async {
    await _device?.disconnect();
    _connected = false;
  }

  BluetoothDevice? get device => _device;
  void dispose() => _stateController.close();
}

class ScanResult {
  final String id;
  final String name;
  final int rssi;
  const ScanResult({required this.id, required this.name, required this.rssi});
}
