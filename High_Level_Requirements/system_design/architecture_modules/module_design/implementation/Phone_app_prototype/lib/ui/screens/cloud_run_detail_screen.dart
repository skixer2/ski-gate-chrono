import 'package:flutter/material.dart';

import '../../cloud/api_client.dart';

/// Read-only viewer for one cloud run (GET /v1/runs/{id} with
/// ?include=timestamps,barometric_data). P1 scope: metadata + the gate
/// timestamp list + a baro-series summary. Charts / comparison against the
/// local twin are P2/P3 — deliberately NOT built here.
class CloudRunDetailScreen extends StatefulWidget {
  final int runId;

  const CloudRunDetailScreen({super.key, required this.runId});

  @override
  State<CloudRunDetailScreen> createState() => _CloudRunDetailScreenState();
}

class _CloudRunDetailScreenState extends State<CloudRunDetailScreen> {
  late final Future<Map<String, dynamic>> _future;

  @override
  void initState() {
    super.initState();
    // Fetched once; FutureBuilder re-uses the same future on rebuilds.
    _future = ApiClient.instance
        .getRun(widget.runId, include: 'timestamps,barometric_data');
  }

  static String _fmtMs(int ms) {
    final m = ms ~/ 60000;
    final s = (ms % 60000) / 1000.0;
    return '${m.toString().padLeft(2, '0')}:${s.toStringAsFixed(3).padLeft(6, '0')}';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        // 1.68: the merged-ness is only known after the fetch — the
        // badge rides on the same (memoized) future as the body.
        title: FutureBuilder<Map<String, dynamic>>(
          future: _future,
          builder: (context, snap) {
            final isMerged = snap.hasData &&
                (snap.data!['run_type'] as String? ?? 'single') == 'merged';
            return Row(children: [
              const Text('Cloud Run Detail'),
              if (isMerged) ...[
                const SizedBox(width: 10),
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                  decoration: BoxDecoration(
                    border:
                        Border.all(color: Colors.deepPurple.shade300),
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Text('Merged',
                      style: TextStyle(
                          fontSize: 11,
                          color: Colors.deepPurple.shade400,
                          fontWeight: FontWeight.w600)),
                ),
              ],
            ]);
          },
        ),
      ),
      body: FutureBuilder<Map<String, dynamic>>(
        future: _future,
        builder: (context, snap) {
          if (snap.connectionState != ConnectionState.done) {
            return const Center(child: CircularProgressIndicator());
          }
          if (snap.hasError) {
            return Center(
              child: Padding(
                padding: const EdgeInsets.all(32),
                child: Text('Failed to load: ${snap.error}',
                    textAlign: TextAlign.center,
                    style: const TextStyle(color: Colors.grey)),
              ),
            );
          }
          final run = snap.data!;
          final name = run['name'] as String?;
          final startRaw = run['start_datetime'];
          final start = startRaw is String
              ? DateTime.tryParse(startRaw)?.toLocal()
              : null;
          final dateStr =
              start == null ? '—' : start.toString().substring(0, 19);
          final timestamps =
              (run['timestamps'] as List<dynamic>? ?? const []).cast<Map<String, dynamic>>();
          final baro =
              (run['barometric_data'] as List<dynamic>? ?? const []).cast<Map<String, dynamic>>();
          // 1.68: lineage — cloud ids of the source runs this merged
          // course was blended from (empty/absent for singles and for
          // merged rows uploaded before the lineage was resolvable).
          final sourceIds = [
            for (final v in run['source_run_ids'] as List<dynamic>? ?? const [])
              if (v is num) v.toInt(),
          ];
          final runType = run['run_type'] as String? ?? 'single';

          return ListView(
            padding: const EdgeInsets.all(16),
            children: [
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(children: [
                        const Icon(Icons.cloud_done_outlined,
                            color: Colors.blue, size: 24),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            (name != null && name.isNotEmpty)
                                ? name
                                : 'Run #${run['run_number'] ?? run['id']}',
                            style: const TextStyle(
                                fontSize: 18, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ]),
                      const SizedBox(height: 12),
                      _row('Cloud ID', '${run['id']}'),
                      _row('Type', runType == 'merged' ? 'Merged course' : 'Single run'),
                      if (sourceIds.isNotEmpty)
                        _row('Merged from',
                            'runs ${sourceIds.map((id) => '#$id').join(' + ')}'),
                      _row('Date', dateStr),
                      _row('Run number', '${run['run_number'] ?? '—'}'),
                      _row('Format version', '${run['format_version'] ?? '—'}'),
                      _row('Lock version', '${run['lock_version'] ?? '—'}'),
                      _row('Gate times', '${timestamps.length}'),
                      _row('Baro points', '${baro.length}'),
                    ],
                  ),
                ),
              ),
              if (baro.isNotEmpty) ...[
                const SizedBox(height: 16),
                _buildBaroSummaryCard(baro),
              ],
              const SizedBox(height: 16),
              Card(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Padding(
                      padding: const EdgeInsets.all(12),
                      child: Text(
                        'Gate Timestamps (${timestamps.length}) — read-only',
                        style: const TextStyle(
                            fontSize: 14, fontWeight: FontWeight.w600),
                      ),
                    ),
                    if (timestamps.isEmpty)
                      Padding(
                        padding: const EdgeInsets.all(12),
                        child: Text('No gate timestamps uploaded for this run',
                            style: TextStyle(
                                fontSize: 13, color: Colors.grey.shade600)),
                      )
                    else
                      for (int i = 0; i < timestamps.length; i++)
                        _timestampRow(i, timestamps[i]),
                  ],
                ),
              ),
              const SizedBox(height: 32),
            ],
          );
        },
      ),
    );
  }

  /// Compact baro-series summary (min/max altitude, peak |vertical speed|).
  Widget _buildBaroSummaryCard(List<Map<String, dynamic>> baro) {
    double? minAlt, maxAlt, maxSpeed;
    for (final b in baro) {
      final alt = (b['altitude_m'] as num?)?.toDouble();
      final spd = (b['speed_mps'] as num?)?.toDouble() ?? 0;
      if (alt != null) {
        if (minAlt == null || alt < minAlt) minAlt = alt;
        if (maxAlt == null || alt > maxAlt) maxAlt = alt;
      }
      if (spd.abs() > (maxSpeed ?? 0)) maxSpeed = spd.abs();
    }
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text('Barometric Series Summary',
                  style: TextStyle(
                      fontSize: 14, fontWeight: FontWeight.w600)),
              const SizedBox(height: 8),
              _row('Points', '${baro.length} (10 Hz)'),
              if (minAlt != null && maxAlt != null)
                _row('Altitude range',
                    '${minAlt.toStringAsFixed(1)} … ${maxAlt.toStringAsFixed(1)} m'),
              if (maxSpeed != null)
                _row('Peak |vertical speed|',
                    '${maxSpeed.toStringAsFixed(1)} m/s'),
            ]),
      ),
    );
  }

  Widget _timestampRow(int index, Map<String, dynamic> t) {
    final ms = (t['timestamp_ms'] as num?)?.toInt() ?? 0;
    final side = t['arm_side'] as String? ?? '?';
    final guessed = t['guessed'] == true;
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: Row(children: [
        SizedBox(
            width: 40,
            child: Text('${index + 1}',
                style: TextStyle(color: Colors.grey.shade600, fontSize: 13))),
        SizedBox(
            width: 90,
            child: Text(_fmtMs(ms),
                style: const TextStyle(
                    fontWeight: FontWeight.w500,
                    fontSize: 13,
                    fontFamily: 'monospace'))),
        Icon(
          side == 'R' ? Icons.arrow_forward : Icons.arrow_back,
          size: 16,
          color: side == 'R' ? Colors.red.shade300 : Colors.blue.shade300,
        ),
        const SizedBox(width: 8),
        if (guessed)
          Text('estimated',
              style: TextStyle(
                  fontSize: 11,
                  fontStyle: FontStyle.italic,
                  color: Colors.orange.shade700)),
      ]),
    );
  }

  Widget _row(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(children: [
        SizedBox(
            width: 110,
            child: Text(label,
                style:
                    TextStyle(color: Colors.grey.shade600, fontSize: 13))),
        Expanded(
          child: Text(value,
              style:
                  const TextStyle(fontWeight: FontWeight.w500, fontSize: 13)),
        ),
      ]),
    );
  }
}
