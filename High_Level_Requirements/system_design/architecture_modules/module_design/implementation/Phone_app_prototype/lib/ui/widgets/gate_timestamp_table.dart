import 'package:flutter/material.dart';
import '../../models/gate_timestamp.dart';
import '../../models/gate_side.dart';

/// Scrollable gate timestamp table.
///
/// Columns: Gate # | Side | Time | Force (G)
/// Estimated gates are marked with * and shown in grey.
/// Side column is color-coded: blue L, red R.
class GateTimestampTable extends StatelessWidget {
  final List<GateTimestamp> gateTimestamps;
  final double maxHeight;

  const GateTimestampTable({
    super.key,
    required this.gateTimestamps,
    this.maxHeight = 320,
  });

  @override
  Widget build(BuildContext context) {
    if (gateTimestamps.isEmpty) {
      return const Card(
        child: Padding(
          padding: EdgeInsets.all(24),
          child: Center(
            child: Text('No gate timestamps available',
                style: TextStyle(color: Colors.grey)),
          ),
        ),
      );
    }

    return Card(
      child: Padding(
        padding: const EdgeInsets.only(top: 8, bottom: 8),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 16, vertical: 4),
              child: Text('Gate Timestamps',
                  style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
            ),
            ConstrainedBox(
              constraints: BoxConstraints(maxHeight: maxHeight),
              child: ListView.builder(
                shrinkWrap: true,
                padding: EdgeInsets.zero,
                itemCount: gateTimestamps.length,
                itemBuilder: (context, index) {
                  final gate = gateTimestamps[index];
                  final timeStr = _formatMs(gate.msFromStart);
                  final sideColor = gate.isEstimated
                      ? Colors.grey
                      : (gate.side == GateSide.rightGate
                          ? Colors.red.shade700
                          : Colors.blue.shade700);
                  final sideLabel = gate.side == GateSide.rightGate ? 'R' : 'L';
                  final forceStr = gate.impactForce != null
                      ? '${gate.impactForce!.toStringAsFixed(1)} G'
                      : '—';
                  final isEstimated = gate.isEstimated;

                  return Container(
                    color: isEstimated
                        ? Colors.grey.shade50
                        : (index.isEven ? null : Colors.grey.shade50),
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 8),
                    child: Row(
                      children: [
                        // Gate number
                        SizedBox(
                          width: 48,
                          child: Text(
                            '#${gate.gateNumber}${isEstimated ? "*" : ""}',
                            style: TextStyle(
                              fontWeight: FontWeight.w600,
                              fontSize: 15,
                              color: isEstimated
                                  ? Colors.grey.shade500
                                  : Colors.black87,
                            ),
                          ),
                        ),
                        // Side badge
                        Container(
                          width: 28,
                          height: 22,
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: sideColor.withOpacity(0.15),
                            borderRadius: BorderRadius.circular(4),
                            border: Border.all(
                                color: sideColor.withOpacity(0.4)),
                          ),
                          child: Text(
                            sideLabel,
                            style: TextStyle(
                              fontWeight: FontWeight.bold,
                              fontSize: 13,
                              color: sideColor,
                            ),
                          ),
                        ),
                        const SizedBox(width: 12),
                        // Time
                        Expanded(
                          child: Text(
                            timeStr,
                            style: TextStyle(
                              fontFamily: 'monospace',
                              fontSize: 14,
                              color: isEstimated
                                  ? Colors.grey.shade600
                                  : Colors.black87,
                            ),
                          ),
                        ),
                        // Force
                        SizedBox(
                          width: 72,
                          child: Text(
                            forceStr,
                            textAlign: TextAlign.right,
                            style: TextStyle(
                              fontFamily: 'monospace',
                              fontSize: 13,
                              color: Colors.grey.shade600,
                            ),
                          ),
                        ),
                      ],
                    ),
                  );
                },
              ),
            ),
            // Legend
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: Row(
                children: [
                  _LegendItem(label: 'L = Left gate', color: Colors.blue),
                  SizedBox(width: 12),
                  _LegendItem(label: 'R = Right gate', color: Colors.red),
                  Spacer(),
                  Text('* = estimated',
                      style: TextStyle(fontSize: 11, color: Colors.grey)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  static String _formatMs(int ms) {
    final minutes = ms ~/ 60000;
    final seconds = (ms % 60000) ~/ 1000;
    final millis = ms % 1000;
    return '${minutes.toString().padLeft(2, '0')}:${seconds.toString().padLeft(2, '0')}.${millis.toString().padLeft(3, '0')}';
  }
}

class _LegendItem extends StatelessWidget {
  final String label;
  final MaterialColor color;
  const _LegendItem({required this.label, required this.color});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 10,
          height: 10,
          decoration: BoxDecoration(
            color: color.shade700.withOpacity(0.2),
            borderRadius: BorderRadius.circular(2),
            border: Border.all(color: color.shade700.withOpacity(0.5)),
          ),
        ),
        const SizedBox(width: 4),
        Text(label,
            style: TextStyle(fontSize: 10, color: Colors.grey.shade600)),
      ],
    );
  }
}
