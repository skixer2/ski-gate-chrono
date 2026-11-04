import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// BLE adapter status check result.
enum BLEPermissionStatus {
  ready,
  bluetoothOff,
  error,
}

class BLEManager {
  final _stateController = StreamController<BluetoothAdapterState>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();

  Stream<BluetoothAdapterState> get stateStream => _stateController.stream;

  /// Emits `true` when a device is connected, `false` on disconnect (manual
  /// or unexpected). UI uses this to clear stale "connected" state.
  Stream<bool> get connectionStream => _connectionController.stream;

  BluetoothDevice? _device;
  StreamSubscription<BluetoothConnectionState>? _connSub;
  bool _connected = false;
  int _mtu = 23;
  List<BluetoothService>? _services;

  bool get isConnected => _connected;
  int get mtu => _mtu;
  List<BluetoothService>? get services => _services;
  BluetoothDevice? get device => _device;

  /// Check Bluetooth adapter status.
  /// flutter_blue_plus v1.x handles Android permissions internally.
  Future<BLEPermissionStatus> checkPermissions() async {
    try {
      final state = await FlutterBluePlus.adapterState.first;
      if (state == BluetoothAdapterState.off) {
        return BLEPermissionStatus.bluetoothOff;
      }
      return BLEPermissionStatus.ready;
    } catch (_) {
      return BLEPermissionStatus.error;
    }
  }

  Future<List<ScanResult>> scan() async {
    // Clean any prior scan / held connection before scanning fresh.
    try { await FlutterBluePlus.stopScan(); } catch (_) {}
    if (_connected) { await disconnect(); }

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

  /// Best-effort clear of the GATT cache. Android-only; no-op elsewhere.
  /// Useful before reconnecting to the same device after a reset/reboot,
  /// because the ATT table and bond cache can go stale.
  Future<void> clearGattCache() async {
    final d = _device;
    if (d == null) return;
    await _clearGattCacheFor(d);
  }

  Future<void> _clearGattCacheFor(BluetoothDevice d) async {
    try {
      await d.clearGattCache();
      debugPrint('[BLE] clearGattCache OK for ${d.remoteId.str}');
    } catch (e) {
      // Method exists only on Android; other platforms throw.
      debugPrint('[BLE] clearGattCache skipped (${e.runtimeType}): $e');
    }
  }

  Future<List<BluetoothService>> connect(String deviceId) async {
    final dev = BluetoothDevice(remoteId: DeviceIdentifier(deviceId));
    return connectToDevice(dev);
  }

  /// Connect directly to a device from scan results (preferred).
  /// Discovers services once and caches them for all subsequent reads/writes.
  ///
  /// V1.33: one automatic retry. On the S22 the first connect right after a
  /// device reboot/adv restart often fails transiently (GATT busy, MTU or
  /// service-discovery error, or a connect timeout that leaves a half-open
  /// link the device still counts as connected). The download path already
  /// retries for this exact reason; the initial connect deserves the same.
  Future<List<BluetoothService>> connectToDevice(BluetoothDevice device) async {
    try {
      return await _connectOnce(device);
    } catch (e) {
      debugPrint('[BLE] connect attempt 1 failed: $e — cleaning up, retry in 1.5 s');
      await _onConnectFailed(device);
      await Future.delayed(const Duration(milliseconds: 1500));
      return _connectOnce(device);
    }
  }

  Future<List<BluetoothService>> _connectOnce(BluetoothDevice device) async {
    // If already connected to a different device, disconnect first.
    // If reconnecting to the SAME device after a drop (device reboot/reset),
    // clear the GATT cache first to avoid stale ATT table / bond issues.
    final existing = _device;
    if (existing != null) {
      if (existing.remoteId.str != device.remoteId.str) {
        debugPrint('[BLE] disconnecting ${existing.remoteId.str} before connecting ${device.remoteId.str}');
        await disconnect();
      } else if (!_connected) {
        debugPrint('[BLE] reconnecting to ${device.remoteId.str}; clearing GATT cache');
        await _clearGattCacheFor(device);
      }
    }

    _attach(device);

    try {
      await device.connect(timeout: const Duration(seconds: 12), autoConnect: false);
    } catch (e) {
      await _onConnectFailed(device);
      rethrow;
    }

    try {
      _mtu = await device.requestMtu(247);
      try {
        await device.requestConnectionPriority(
            connectionPriorityRequest: ConnectionPriority.balanced);
        debugPrint('[BLE] requestConnectionPriority(balanced) OK');
      } catch (e) {
        debugPrint('[BLE] requestConnectionPriority failed: $e');
      }
      _services = await device.discoverServices();
    } catch (e) {
      debugPrint('[BLE] post-connect setup failed: $e');
      try { await device.disconnect(); } catch (_) {}
      _connSub?.cancel();
      _connSub = null;
      _clearInternalState();
      rethrow;
    }

    _connected = true;
    _autoReconnectAttempts = 0;  // V1.38: fresh link resets the retry budget
    _connectionController.add(true);
    debugPrint('[BLE] connected to ${device.remoteId.str} (mtu=$_mtu, ${_services!.length} services)');
    return _services!;
  }

  void _attach(BluetoothDevice device) {
    _device = device;
    _connSub?.cancel();
    _connSub = device.connectionState.listen((state) {
      debugPrint('[BLE] ${device.remoteId.str} connectionState → $state');
      if (state == BluetoothConnectionState.disconnected && _connected) {
        // Unexpected drop (we thought we were connected): clear state and
        // notify the UI so it isn't stuck showing a dead connection.
        debugPrint('[BLE] unexpected disconnect from ${device.remoteId.str}');
        _clearInternalState();
        _connectionController.add(false);
        // V1.38: auto-reconnect. The S22 kills links status=8 inside the
        // post-connect churn window even with ZERO traffic (bench 2026-09-03:
        // link died idle before JP could press Start). Don't strand the UI —
        // reconnect quietly (max 2 attempts, 2 s apart). Intentional
        // disconnect() cancels this listener first, so it never fires here.
        unawaited(_autoReconnect(device));
      }
    });
  }

  int _autoReconnectAttempts = 0;
  static const int _kAutoReconnectMax = 2;

  Future<void> _autoReconnect(BluetoothDevice device) async {
    if (_autoReconnectAttempts >= _kAutoReconnectMax) return;
    _autoReconnectAttempts++;
    await Future.delayed(const Duration(seconds: 2));
    if (_connected || _device == null) {
      _autoReconnectAttempts = 0;
      return;  // already reconnected (or detached) meanwhile
    }
    debugPrint('[BLE] auto-reconnect attempt $_autoReconnectAttempts/$_kAutoReconnectMax');
    try {
      await connectToDevice(device);
      _autoReconnectAttempts = 0;
      debugPrint('[BLE] auto-reconnect OK');
    } catch (e) {
      debugPrint('[BLE] auto-reconnect failed: $e');
      unawaited(_autoReconnect(device));
    }
  }

  void _clearInternalState() {
    _connected = false;
    _services = null;
    _mtu = 23;
  }

  Future<void> _onConnectFailed(BluetoothDevice device) async {
    debugPrint('[BLE] connect failed for ${device.remoteId.str}');
    try { await device.disconnect(); } catch (_) {}
    await _clearGattCacheFor(device);
    _clearInternalState();
  }

  Future<void> disconnect() async {
    // Cancel the listener first so the device's own disconnect event doesn't
    // re-trigger cleanup (we're doing it explicitly here).
    _connSub?.cancel();
    _connSub = null;
    try { await _device?.disconnect(); } catch (_) {}
    _clearInternalState();
    _connectionController.add(false);
  }

  void dispose() {
    _connSub?.cancel();
    _stateController.close();
    _connectionController.close();
  }
}

class ScanResult {
  final String id;
  final String name;
  final int rssi;
  const ScanResult({required this.id, required this.name, required this.rssi});
}
