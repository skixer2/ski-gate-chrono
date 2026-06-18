class SensorFrame {
  final int msFromStart;
  final double qW, qX, qY, qZ;
  final double laX, laY, laZ;
  final double baroPressurePa;
  final double baroAltitudeM;
  final double verticalSpeedMs;
  const SensorFrame({
    required this.msFromStart,
    required this.qW, required this.qX, required this.qY, required this.qZ,
    required this.laX, required this.laY, required this.laZ,
    required this.baroPressurePa,
    this.baroAltitudeM = 0, this.verticalSpeedMs = 0,
  });
}
