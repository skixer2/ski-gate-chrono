import 'package:flutter/material.dart';
import '../../models/sensor_frame.dart';
import '../../models/gate_timestamp.dart';
import '../../models/gate_side.dart';
import '../../models/barometric_point.dart';

/// Altitude-over-time chart with gate marker lines.
///
/// Displays barometric altitude (m) from decimated 10 Hz data.
/// Gate timestamps are overlaid as vertical dashed lines:
///   - Blue = left gate
///   - Red = right gate
///   - Grey = estimated gate
class AltitudeChart extends StatelessWidget {
  final List<BarometricPoint> barometricData;
  final List<GateTimestamp> gateTimestamps;
  final double height;

  const AltitudeChart({
    super.key,
    required this.barometricData,
    required this.gateTimestamps,
    this.height = 220,
  });

  @override
  Widget build(BuildContext context) {
    if (barometricData.isEmpty) {
      return const SizedBox(
        height: 100,
        child: Center(child: Text('No altitude data')),
      );
    }

    return SizedBox(
      height: height,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Padding(
            padding: EdgeInsets.only(left: 48, bottom: 4),
            child: Text('Altitude (m)',
                style: TextStyle(fontSize: 12, color: Colors.grey)),
          ),
          Expanded(
            child: CustomPaint(
              size: Size.infinite,
              painter: _AltitudeChartPainter(
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

class _AltitudeChartPainter extends CustomPainter {
  final List<BarometricPoint> barometricData;
  final List<GateTimestamp> gateTimestamps;

  _AltitudeChartPainter({
    required this.barometricData,
    required this.gateTimestamps,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (barometricData.isEmpty) return;

    final chartLeft = 48.0;
    final chartRight = size.width - 16.0;
    final chartTop = 8.0;
    final chartBottom = size.height - 8.0;
    final chartWidth = chartRight - chartLeft;
    final chartHeight = chartBottom - chartTop;

    // Compute altitude range
    double altMin = double.infinity, altMax = double.negativeInfinity;
    for (final p in barometricData) {
      altMin = p.altitudeM < altMin ? p.altitudeM : altMin;
      altMax = p.altitudeM > altMax ? p.altitudeM : altMax;
    }
    if ((altMax - altMin) < 10) {
      final mid = (altMax + altMin) / 2;
      altMin = mid - 5;
      altMax = mid + 5;
    }

    final double timeMax = barometricData.last.msFromStart.toDouble();
    if (timeMax <= 0) return;

    final timeToX = (double t) => chartLeft + (t / timeMax) * chartWidth;
    final altToY = (double a) =>
        chartTop + (1.0 - (a - altMin) / (altMax - altMin)) * chartHeight;

    // Draw Y-axis labels (every ~10% of range)
    final labelPaint = TextStyle(color: Colors.grey.shade500, fontSize: 10);
    for (int i = 0; i <= 5; i++) {
      final frac = i / 5.0;
      final alt = altMin + frac * (altMax - altMin);
      final tp = TextPainter(
        text: TextSpan(text: alt.toStringAsFixed(0), style: labelPaint),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(chartLeft - tp.width - 4, altToY(alt) - tp.height / 2));
    }

    // Draw X-axis time labels (every ~20% of duration)
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

    // Draw grid lines
    final gridPaint = Paint()
      ..color = Colors.grey.shade200
      ..strokeWidth = 0.5;
    for (int i = 0; i <= 5; i++) {
      final y = altToY(altMin + (i / 5.0) * (altMax - altMin));
      canvas.drawLine(Offset(chartLeft, y), Offset(chartRight, y), gridPaint);
    }

    // Draw altitude line
    final linePaint = Paint()
      ..color = Colors.blue.shade400
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    final path = Path();
    for (int i = 0; i < barometricData.length; i++) {
      final x = timeToX(barometricData[i].msFromStart.toDouble());
      final y = altToY(barometricData[i].altitudeM);
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(path, linePaint);

    // Draw gate markers
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

      // Gate number at top
      final tp = TextPainter(
        text: TextSpan(
          text: '${gate.gateNumber}${gate.isEstimated ? "*" : ""}',
          style: TextStyle(
              color: color, fontSize: 9, fontWeight: FontWeight.bold),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x - tp.width / 2, chartTop - 2));

      // Gate symbol at bottom
      final symPaint = Paint()..color = color;
      if (gate.side == GateSide.rightGate) {
        // R for right
        final rp = TextPainter(
          text: TextSpan(
              text: 'R', style: TextStyle(color: color, fontSize: 9, fontWeight: FontWeight.bold)),
          textDirection: TextDirection.ltr,
        )..layout();
        rp.paint(canvas, Offset(x - rp.width / 2, chartBottom - rp.height));
      } else {
        final lp = TextPainter(
          text: TextSpan(
              text: 'L', style: TextStyle(color: color, fontSize: 9, fontWeight: FontWeight.bold)),
          textDirection: TextDirection.ltr,
        )..layout();
        lp.paint(canvas, Offset(x - lp.width / 2, chartBottom - lp.height));
      }
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
  bool shouldRepaint(covariant _AltitudeChartPainter oldDelegate) => true;
}
