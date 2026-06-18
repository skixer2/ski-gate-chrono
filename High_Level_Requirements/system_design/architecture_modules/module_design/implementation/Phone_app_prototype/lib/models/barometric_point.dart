class BarometricPoint {
  final int msFromStart;
  final double altitudeM;
  final double verticalSpeedMs;
  const BarometricPoint({
    required this.msFromStart, required this.altitudeM, required this.verticalSpeedMs,
  });
}
