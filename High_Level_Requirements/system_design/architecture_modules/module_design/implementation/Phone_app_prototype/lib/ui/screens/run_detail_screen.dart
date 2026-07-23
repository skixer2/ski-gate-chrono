import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
import '../../processing/decompressor.dart';
import '../../processing/gate_time_estimator.dart';
import '../../processing/impact_detector.dart';
import '../../models/sensor_frame.dart';
import '../../models/barometric_point.dart';
import '../../models/gate_timestamp.dart';
import '../../models/gate_side.dart';
import '../../models/run.dart';
import 'package:intl/intl.dart';
import '../widgets/altitude_chart.dart';
import '../widgets/speed_chart.dart';
import '../widgets/gate_timestamp_table.dart';

/// Displays a fully processed run: metadata, altitude + speed charts, and
/// gate timestamps.
///
/// Accepts [DecompressResult] from the BLE download + decompress pipeline.
/// Processes the raw frames into barometric decimated data and gate timestamps
/// on init (no course = Bronze tier turn counting).
class RunDetailScreen extends StatefulWidget {
  final DecompressResult result;

  /// Optional device name for display.
  final String? deviceName;

  /// Optional arm side from device config.
  final ArmSide? armSide;

  const RunDetailScreen({
    super.key,
    required this.result,
    this.deviceName,
    this.armSide,
  });

  @override
  State<RunDetailScreen> createState() => _RunDetailScreenState();
}

class _RunDetailScreenState extends State<RunDetailScreen> {
  List<BarometricPoint> _baroData = [];
  List<GateTimestamp> _gateTimestamps = [];

  @override
  void initState() {
    super.initState();
    _processRun();
  }

  void _processRun() {
    try {
      final frames = widget.result.frames;
      if (frames.isEmpty) return;

      // Compute decimated 10 Hz barometric data with vertical speed.
      _baroData = _computeBaroData(frames);

      // Detect impacts
      final detector = ImpactDetector(multiplier: 2.5, baselineWindow: 50);
      final impacts = detector.detect(frames);

      // Estimate gate timestamps (Bronze tier: no course map)
      final estimator = GateTimeEstimator(
        course: null, // Bronze tier for now
        knownImpacts: impacts,
      );
      _gateTimestamps = estimator.estimate(frames);
    } catch (e, stack) {
      debugPrint('[RunDetail] _processRun error: $e\n$stack');
      _gateTimestamps = [];
    }
  }

  /// Decimate 100 Hz frames to 10 Hz barometric points with vertical speed.
  List<BarometricPoint> _computeBaroData(List<SensorFrame> frames) {
    final result = <BarometricPoint>[];
    // Take every 10th frame (100 Hz → 10 Hz)
    for (int i = 0; i < frames.length; i += 10) {
      result.add(BarometricPoint(
        msFromStart: frames[i].msFromStart,
        altitudeM: frames[i].baroAltitudeM,
        verticalSpeedMs: 0, // computed below
      ));
    }
    // Compute vertical speed as altitude derivative
    for (int i = 1; i < result.length; i++) {
      final dt = (result[i].msFromStart - result[i - 1].msFromStart) / 1000.0;
      if (dt > 0) {
        final speed =
            (result[i].altitudeM - result[i - 1].altitudeM) / dt;
        result[i - 1] = BarometricPoint(
          msFromStart: result[i - 1].msFromStart,
          altitudeM: result[i - 1].altitudeM,
          verticalSpeedMs: speed,
        );
      }
    }
    // Last point: copy penultimate speed
    if (result.length >= 2) {
      result.last = BarometricPoint(
        msFromStart: result.last.msFromStart,
        altitudeM: result.last.altitudeM,
        verticalSpeedMs: result[result.length - 2].verticalSpeedMs,
      );
    }
    return result;
  }

  @override
  Widget build(BuildContext context) {
    final hdr = widget.result.header;
    final duration = widget.result.totalDurationSec;
    final frames = widget.result.frames;
    final detectedGates =
        _gateTimestamps.where((g) => !g.isEstimated).length;
    final estimatedGates =
        _gateTimestamps.where((g) => g.isEstimated).length;

    // Compute avg speed if we have baro data
    double avgSpeed = 0;
    if (_baroData.isNotEmpty) {
      double sumV = 0;
      for (final b in _baroData) {
        sumV += b.verticalSpeedMs.abs();
      }
      avgSpeed = sumV / _baroData.length;
    }

    final dateStr = DateFormat('yyyy-MM-dd HH:mm:ss').format(
      DateTime.fromMillisecondsSinceEpoch(
          hdr.startTimestamp * 1000, isUtc: true),
    );

    return Scaffold(
      appBar: AppBar(
        title: Text(widget.deviceName ?? 'Run Detail'),
        actions: [
          IconButton(
            icon: const Icon(Icons.ios_share),
            tooltip: 'Export',
            onPressed: () {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Export coming soon')),
              );
            },
          ),
        ],
      ),
      body: frames.isEmpty
          ? const Center(
              child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                Icon(Icons.warning_amber, size: 48, color: Colors.orange),
                SizedBox(height: 16),
                Text('No sensor data in this run', style: TextStyle(fontSize: 16)),
                SizedBox(height: 8),
                Text('The run may be too short or corrupted',
                    style: TextStyle(color: Colors.grey)),
              ]),
            )
          : (_gateTimestamps.isEmpty && frames.length < 100)
              ? Center(
                  child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                    Icon(Icons.timer_off, size: 48, color: Colors.orange),
                    SizedBox(height: 16),
                    Text('Run too short for analysis', style: TextStyle(fontSize: 16)),
                    SizedBox(height: 8),
                    Text('${frames.length} frames (need ≥100)',
                        style: TextStyle(color: Colors.grey)),
                    SizedBox(height: 24),
                    // Still show basic metadata
                    _buildShortRunSummary(dateStr, duration, hdr, frames),
                  ]),
                )
              : _gateTimestamps.isEmpty
                  ? Center(
                      child: Padding(
                        padding: const EdgeInsets.all(32),
                        child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                          Icon(Icons.speed, size: 48, color: Colors.orange.shade300),
                          SizedBox(height: 16),
                          Text('No gates detected', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500)),
                          SizedBox(height: 12),
                          Text(
                            '${frames.length} frames decoded but no gate crossings found.\n\n'
                            'This may indicate:\n'
                            '• Sensor calibration not yet complete\n'
                            '• Run too short (< 1 second)\n'
                            '• Device motionless during recording',
                            textAlign: TextAlign.center,
                            style: TextStyle(color: Colors.grey.shade600, fontSize: 14),
                          ),
                          SizedBox(height: 24),
                          // Still show basic metadata
                          _buildShortRunSummary(dateStr, duration, hdr, frames),
                        ]),
                      ),
                    )
                  : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                // ── Metadata card ──────────────────────────────
                _buildMetadataCard(dateStr, duration, avgSpeed, hdr, frames,
                    detectedGates, estimatedGates),
                const SizedBox(height: 16),

                // ── Altitude chart ─────────────────────────────
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(8),
                    child: AltitudeChart(
                      barometricData: _baroData,
                      gateTimestamps: _gateTimestamps,
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // ── Speed chart ────────────────────────────────
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(8),
                    child: SpeedChart(
                      barometricData: _baroData,
                      gateTimestamps: _gateTimestamps,
                    ),
                  ),
                ),
                const SizedBox(height: 16),

                // ── Gate timestamp table ───────────────────────
                GateTimestampTable(
                  gateTimestamps: _gateTimestamps,
                ),

                // ── Raw frame table expandable ──────────────────
                const SizedBox(height: 16),
                _buildRawDataSection(),
                const SizedBox(height: 32),
              ],
            ),
    );
  }

  Widget _buildMetadataCard(
    String dateStr,
    double duration,
    double avgSpeed,
    RunHeader hdr,
    List<SensorFrame> frames,
    int detectedGates,
    int estimatedGates,
  ) {
    final armLabel =
        widget.armSide != null ? widget.armSide!.label : 'Unknown';
    final discLabel = const {'sl': 'Slalom', 'gs': 'GS', 'sg': 'Super-G', 'dh': 'DH'};
    final calLabel = ['—', 'Low', 'Medium', 'High'][hdr.calAccuracy.clamp(0, 3)];

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.timer, color: Colors.blue, size: 24),
                const SizedBox(width: 8),
                Text(
                  'Run — ${_formatDuration(duration)}',
                  style: const TextStyle(
                      fontSize: 18, fontWeight: FontWeight.bold),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _metaChip('Date', dateStr.substring(0, 10)),
                ),
                Expanded(
                  child: _metaChip('Arm', armLabel),
                ),
                Expanded(
                  child: _metaChip('Frames', '${widget.result.frameCount}'),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: _metaChip(
                      'Compression',
                      frames.isNotEmpty
                          ? '${((1 - hdr.compressedSize / (frames.length * 16)) * 100).toStringAsFixed(0)}%'
                          : '—'),
                ),
                Expanded(
                  child: _metaChip('Avg Speed', '${avgSpeed.toStringAsFixed(1)} m/s'),
                ),
                Expanded(
                  child: _metaChip('Cal', calLabel),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: _metaChip('Gates', '$detectedGates detected'),
                ),
                Expanded(
                  child: _metaChip('Estimated', '$estimatedGates estimated'),
                ),
                Expanded(
                  child: _metaChip(
                      'Temp', '${hdr.baroTempC.toStringAsFixed(1)}°C'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _metaChip(String label, String value) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label,
            style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
        const SizedBox(height: 2),
        Text(value,
            style: const TextStyle(
                fontSize: 14, fontWeight: FontWeight.w500)),
      ],
    );
  }

  Widget _buildShortRunSummary(String dateStr, double duration, RunHeader hdr, List<SensorFrame> frames) {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 32),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            _infoRow('Date', dateStr.substring(0, 10)),
            _infoRow('Duration', '${duration.toStringAsFixed(1)}s'),
            _infoRow('Frames', '${frames.length}'),
            _infoRow('Format', 'v${hdr.formatVersion}'),
          ],
        ),
      ),
    );
  }

  Widget _infoRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          SizedBox(width: 80, child: Text(label, style: TextStyle(color: Colors.grey.shade600, fontSize: 13))),
          Text(value, style: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13)),
        ],
      ),
    );
  }

  Widget _buildRawDataSection() {
    final frameCount = widget.result.frameCount;
    return ExpansionTile(
      title: const Text('Raw Sensor Data',
          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
      subtitle: Text('$frameCount frames at 100 Hz'),
      children: [
        SizedBox(
          height: 200,
          child: ListView.builder(
            shrinkWrap: true,
            physics: const AlwaysScrollableScrollPhysics(),
            padding: const EdgeInsets.symmetric(horizontal: 16),
            itemCount: frameCount,
            itemBuilder: (context, index) {
              final f = widget.result.frames[index];
              return Padding(
                padding: const EdgeInsets.symmetric(vertical: 2),
                child: Text(
                  '${f.msFromStart.toString().padLeft(6)}ms | '
                  'alt:${f.baroAltitudeM.toStringAsFixed(1)}m | '
                  'Pa:${f.baroPressurePa.toStringAsFixed(0)} | '
                  'q:(${f.qW.toStringAsFixed(3)},${f.qX.toStringAsFixed(3)},${f.qY.toStringAsFixed(3)},${f.qZ.toStringAsFixed(3)}) | '
                  'la:(${f.laX.toStringAsFixed(0)},${f.laY.toStringAsFixed(0)},${f.laZ.toStringAsFixed(0)})',
                  style: TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 10,
                    color: Colors.grey.shade700,
                  ),
                ),
              );
            },
          ),
        ),
      ],
    );
  }

  static String _formatDuration(double seconds) {
    final mins = seconds ~/ 60;
    final secs = (seconds % 60).toStringAsFixed(1);
    return '${mins.toString().padLeft(2, '0')}:${secs.padLeft(4, '0')}';
  }
}
