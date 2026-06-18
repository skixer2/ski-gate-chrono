import 'gate_side.dart';
class DeviceConfig {
  final String deviceId;
  String deviceName;
  ArmSide armSide;
  Discipline discipline;
  int calibrationAccuracy;
  DeviceConfig({
    required this.deviceId, this.deviceName = 'SGC', this.armSide = ArmSide.left,
    this.discipline = Discipline.gs, this.calibrationAccuracy = 0,
  });
}
