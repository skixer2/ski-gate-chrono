import 'package:flutter/material.dart';
import '../models/gate_side.dart';
import '../models/gate_timestamp.dart';
import '../widgets/gate_timestamp_table.dart';

/// Read-only detail for a merged dual-device course (JP 2026-10-13).
/// Shows the impact-confirmed union of a left-pole + right-pole run pair.
class MergedRunScreen extends StatelessWidget {
  final String title;
  final List<Map<String, dynamic>> gates;

  const MergedRunScreen({super.key, required this.title, required this.gates});

  @override
  Widget build(BuildContext context) {
    final ts = [
      for (final g in gates)
        GateTimestamp(
          gateNumber: 0, // assigned below after sort
          side: g['side'] == 'R' ? GateSide.rightGate : GateSide.leftGate,
          isEstimated: false,
          msFromStart: g['ms'] as int,
          impactForce: g['force'] is double ? g['force'] as double : null,
        ),
    ];
    // Number gates 1..N in time order for display.
    final numbered = [
      for (int i = 0; i < ts.length; i++)
        GateTimestamp(
          gateNumber: i + 1,
          side: ts[i].side,
          isEstimated: false,
          msFromStart: ts[i].msFromStart,
          impactForce: ts[i].impactForce,
        ),
    ];
    final l = numbered.where((g) => g.side == GateSide.leftGate).length;
    final r = numbered.length - l;

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
          const SizedBox(height: 12),
          GateTimestampTable(
            gateTimestamps: numbered,
            maxHeight: 1600,
          ),
        ],
      ),
    );
  }
}
