import 'package:flutter/material.dart';
import '../../models/barometric_point.dart';
import '../../models/gate_timestamp.dart';
import '../../models/gate_side.dart';

/// Vertical-speed-over-time chart with gate marker lines.
///
/// Displays barometric vertical speed (m/s) from decimated 10 Hz data,
/// computed as derivative of altitude.
/// Positive = ascending (lifts/start area), negative = descending (course).
class SpeedChart extends StatelessWidget {
  final List<BarometricPoint> barometricData;
  final List<GateTimestamp> gateTimestamps;
  final double height;

  const SpeedChart({
    super.key,
    required this.barometricData,
    required this.gateTimestamps,
    this.height = 200,
  });

  @override
  Widget build(BuildContext context) {
    if (barometricData.length < 2) {
      return const SizedBox(
        height: 100,
        child: Center(child: Text('Not enough data for speed chart')),
      );
    }

    return SizedBox(
      height: height,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Padding(
            padding: EdgeInsets.only(left: 48, bottom: 4),
            child: Text('Vertical Speed (m/s)',
                style: TextStyle(fontSize: 12, color: Colors.grey)),
          ),
          Expanded(
            child: CustomPaint(
              size: Size.infinite,
              painter: _SpeedChartPainter(
                barometricData: barometricData,
                gateTimestamps: gateTimestamps,
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.only(left: 48, top: 4),
            child: Text(
              'Time (s)',
              style: TextStyle(fontSize: 12, color: Colors.grey.shade500),
            ),
          ),
        ],
      ),
    );
  }
}

class _SpeedChartPainter extends CustomPainter {
  final List<BarometricPoint> barometricData;
  final List<GateTimestamp> gateTimestamps;

  _SpeedChartPainter({
    required this.barometricData,
    required this.gateTimestamps,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (barometricData.length < 2) return;

    final chartLeft = 48.0;
    final chartRight = size.width - 16.0;
    final chartTop = 8.0;
    final chartBottom = size.height - 8.0;
    final chartWidth = chartRight - chartLeft;
    final chartHeight = chartBottom - chartTop;

    // Compute vertical speed if not pre-computed, clamped to ±40 m/s for display
    // (competitive skiing: ~15-30 m/s downhill, ~0-5 m/s uphill)
    double velMin = double.infinity, velMax = double.negativeInfinity;
    for (final p in barometricData) {
      final v = p.verticalSpeedMs.clamp(-40.0, 40.0);
      velMin = v < velMin ? v : velMin;
      velMax = v > velMax ? v : velMax;
    }
    // Ensure zero line is visible
    if (velMin > 0) velMin = -2;
    if (velMax < 0) velMax = 2;
    // Add headroom
    final range = velMax - velMin;
    velMin -= range * 0.1;
    velMax += range * 0.1;

    final double timeMax = barometricData.last.msFromStart.toDouble();
    if (timeMax <= 0) return;

    final timeToX = (double t) => chartLeft + (t / timeMax) * chartWidth;
    final velToY = (double v) =>
        chartTop + (1.0 - (v - velMin) / (velMax - velMin)) * chartHeight;

    // Y-axis labels
    final labelPaint = TextStyle(color: Colors.grey.shade500, fontSize: 10);
    for (int i = 0; i <= 5; i++) {
      final frac = i / 5.0;
      final v = velMin + frac * (velMax - velMin);
      final tp = TextPainter(
        text: TextSpan(text: v.toStringAsFixed(1), style: labelPaint),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(chartLeft - tp.width - 4, velToY(v) - tp.height / 2));
    }

    // X-axis time labels
    for (int i = 0; i <= 5; i++) {
      final frac = i / 5.0;
      final tSec = frac * timeMax / 1000.0;
      final x = chartLeft + frac * chartWidth;
      final tp = TextPainter(
        text: TextSpan(text: tSec.toStringAsFixed(1), style: labelPaint),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x - tp.width / 2, chartBottom + 4));
    }

    // Zero line (highlighted)
    final zeroLinePaint = Paint()
      ..color = Colors.grey.shade400
      ..strokeWidth = 0.8;
    final zeroY = velToY(0);
    canvas.drawLine(Offset(chartLeft, zeroY), Offset(chartRight, zeroY), zeroLinePaint);

    // Grid lines
    final gridPaint = Paint()
      ..color = Colors.grey.shade200
      ..strokeWidth = 0.5;
    for (int i = 0; i <= 5; i++) {
      final y = velToY(velMin + (i / 5.0) * (velMax - velMin));
      canvas.drawLine(Offset(chartLeft, y), Offset(chartRight, y), gridPaint);
    }

    // Speed line — color gradient: green (slow/positive) → orange (medium) → red (fast)
    final speedPath = Path();
    for (int i = 0; i < barometricData.length; i++) {
      final x = timeToX(barometricData[i].msFromStart.toDouble());
      final y = velToY(barometricData[i].verticalSpeedMs.clamp(-40.0, 40.0));
      if (i == 0) {
        speedPath.moveTo(x, y);
      } else {
        speedPath.lineTo(x, y);
      }
    }

    final speedPaint = Paint()
      ..color = Colors.orange.shade600
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    canvas.drawPath(speedPath, speedPaint);

    // Fill area below speed line (light orange with fade)
    if (barometricData.isNotEmpty) {
      final fillPath = Path.from(speedPath);
      fillPath.lineTo(
        timeToX(barometricData.last.msFromStart.toDouble()),
        velToY(0),
      );
      fillPath.lineTo(
        timeToX(barometricData.first.msFromStart.toDouble()),
        velToY(0),
      );
      fillPath.close();
      final fillPaint = Paint()
        ..color = Colors.orange.withOpacity(0.08)
        ..style = PaintingStyle.fill;
      canvas.drawPath(fillPath, fillPaint);
    }

    // Gate markers
    for (final gate in gateTimestamps) {
      final x = timeToX(gate.msFromStart.toDouble());
      if (x < chartLeft || x > chartRight) continue;

      final color = gate.isEstimated
          ? Colors.grey.shade400
          : (gate.side == GateSide.rightGate ? Colors.red : Colors.blue);

      // Dashed vertical line
      final dashPaint = Paint()
        ..color = color.withOpacity(0.6)
        ..strokeWidth = 1.0;
      const dashLen = 4.0;
      const gapLen = 3.0;
      var y = chartTop;
      bool draw = true;
      while (y < chartBottom) {
        if (draw) {
          canvas.drawLine(Offset(x, y), Offset(x, (y + dashLen).clamp(chartTop, chartBottom)), dashPaint);
        }
        y += dashLen;
        draw = !draw;
      }

      // Gate number label at top
      final tp = TextPainter(
        text: TextSpan(
          text: '${gate.gateNumber}${gate.isEstimated ? "*" : ""}',
          style: TextStyle(
              color: color, fontSize: 9, fontWeight: FontWeight.bold),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x - tp.width / 2, chartTop - 2));
    }

    // Chart border
    final borderPaint = Paint()
      ..color = Colors.grey.shade300
      ..strokeWidth = 0.5
      ..style = PaintingStyle.stroke;
    canvas.drawRect(
      Rect.fromLTRB(chartLeft, chartTop, chartRight, chartBottom),
      borderPaint,
    );
  }

  @override
  bool shouldRepaint(covariant _SpeedChartPainter oldDelegate) => true;
}
