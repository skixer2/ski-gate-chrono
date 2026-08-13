class SensorFrame {
  final int msFromStart;
  /// Unit quaternion (decompressed from Q14 / 16384).
  final double qW, qX, qY, qZ;
  /// Linear acceleration from firmware LACC — **mm/s²** (not m/s²).
  /// |g| ≈ 9810. Wire int16 ceiling ±32767 mm/s² ≈ ±3.34 g.
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
