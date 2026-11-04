import 'gate_side.dart';
class GateTimestamp {
  final int gateNumber;
  final GateSide? side;
  final bool isEstimated;
  final int msFromStart;
  final double? impactForce;
  final String? rfidTagId;
  const GateTimestamp({
    required this.gateNumber, this.side, this.isEstimated = false,
    required this.msFromStart, this.impactForce, this.rfidTagId,
  });
}
