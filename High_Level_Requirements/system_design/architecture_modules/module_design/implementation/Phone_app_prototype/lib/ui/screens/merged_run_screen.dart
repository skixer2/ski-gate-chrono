import 'package:flutter/material.dart';
import '../../models/barometric_point.dart';
import '../../models/gate_side.dart';
import '../../models/gate_timestamp.dart';
import '../widgets/altitude_chart.dart';
import '../widgets/speed_chart.dart';
import '../widgets/gate_timestamp_table.dart';

/// Read-only detail for a merged dual-device course (JP 2026-10-13).
/// Shows averaged altitude + vertical speed charts (JP 2026-10-14) and
/// the impact-confirmed union of a left-pole + right-pole run pair.
class MergedRunScreen extends StatelessWidget {
  final String title;
  final List<Map<String, dynamic>> gates;
  final List<Map<String, dynamic>> baro; // {'ms','alt'} averaged, 10 Hz

  const MergedRunScreen({
    super.key,
    required this.title,
    required this.gates,
    this.baro = const [],
  });

  List<GateTimestamp> _numbered() {
    final sorted = [...gates]
      ..sort((a, b) => (a['ms'] as int).compareTo(b['ms'] as int));
    return [
      for (int i = 0; i < sorted.length; i++)
        GateTimestamp(
          gateNumber: i + 1,
          side: sorted[i]['side'] == 'R'
              ? GateSide.rightGate
              : GateSide.leftGate,
          isEstimated: false,
          msFromStart: sorted[i]['ms'] as int,
          impactForce: sorted[i]['force'] is double
              ? sorted[i]['force'] as double
              : null,
        ),
    ];
  }

  List<BarometricPoint> _baroPoints() {
    if (baro.isEmpty) return const [];
    final pts = [
      for (final b in baro)
        BarometricPoint(
          msFromStart: b['ms'] as int,
          altitudeM: (b['alt'] as num).toDouble(),
          verticalSpeedMs: 0,
        ),
    ];
    for (int i = 1; i < pts.length; i++) {
      final dt = (pts[i].msFromStart - pts[i - 1].msFromStart) / 1000.0;
      if (dt > 0) {
        final v =
            (pts[i].altitudeM - pts[i - 1].altitudeM) / dt;
        pts[i - 1] = BarometricPoint(
          msFromStart: pts[i - 1].msFromStart,
          altitudeM: pts[i - 1].altitudeM,
          verticalSpeedMs: v,
        );
      }
    }
    return pts;
  }

  @override
  Widget build(BuildContext context) {
    final numbered = _numbered();
    final l = numbered.where((g) => g.side == GateSide.leftGate).length;
    final r = numbered.length - l;
    final baroPoints = _baroPoints();
    final screenH = MediaQuery.of(context).size.height;

    return Scaffold(
      appBar: AppBar(title: Text(title)),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(title, style: Theme.of(context).textTheme.titleMedium),
                  const SizedBox(height: 8),
                  Text(
                    '${numbered.length} gates impact-confirmed '
                    '($l left · $r right)',
                    style: Theme.of(context).textTheme.bodyMedium,
                  ),
                  const SizedBox(height: 4),
                  Text(
                    'Merged from a complementary device pair — every gate '
                    'has pole-contact evidence.',
                    style: Theme.of(context)
                        .textTheme
                        .bodySmall
                        ?.copyWith(color: Colors.grey),
                  ),
                ],
              ),
            ),
          ),
          if (baroPoints.isNotEmpty) ...[
            const SizedBox(height: 12),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(8),
                child: AltitudeChart(
                  barometricData: baroPoints,
                  gateTimestamps: numbered,
                  height: 200,
                ),
              ),
            ),
            const SizedBox(height: 12),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(8),
                child: SpeedChart(
                  barometricData: baroPoints,
                  gateTimestamps: numbered,
                  height: 200,
                ),
              ),
            ),
          ],
          const SizedBox(height: 12),
          GateTimestampTable(
            gateTimestamps: numbered,
            maxHeight: screenH * 0.62, // inner list scrolls (JP 2026-10-14)
          ),
        ],
      ),
    );
  }
}
