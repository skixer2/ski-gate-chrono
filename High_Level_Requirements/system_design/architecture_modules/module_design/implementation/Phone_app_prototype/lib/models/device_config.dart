import 'gate_side.dart';

/// v1.50 (error-tolerance audit A3): fields are NULLABLE and failures are
/// COLLECTED, never fabricated. null = "read failed / char missing" —
/// the UI renders 'Unknown'. `warnings` carries per-char failure reasons
/// (tolerate AND signal — JP directive 2026-10-06).
class DeviceConfig {
  final String deviceId;
  final String? deviceName;        // null = ABC1 unreadable/missing
  final ArmSide? armSide;          // null = ABC2 unreadable/missing
  final Discipline? discipline;    // null = ABC3 unreadable/missing
  final int? calibrationAccuracy;  // null = ABD0 unreadable/missing
  final List<String> warnings;     // per-char failure reasons, may be empty
  final String? timeSyncError;     // null = ABC0 write verified via readback

  DeviceConfig({
    required this.deviceId,
    this.deviceName,
    this.armSide,
    this.discipline,
    this.calibrationAccuracy,
    this.warnings = const [],
    this.timeSyncError,
  });
}
