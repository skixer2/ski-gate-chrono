import 'package:flutter/material.dart';

import '../../cloud/api_client.dart';
import 'cloud_run_detail_screen.dart';

/// F48 (P1 slice): browse YOUR OWN uploaded runs (GET /v1/runs, keyset-
/// paged with lazy "load more"). Tapping a run opens a read-only detail
/// view — full local-vs-cloud comparison UI is P2/P3, deliberately not
/// built here.
///
/// 1.68: runs carry `run_type` ("single" | "merged", 1.68 server
/// contract). Merged tiles get a merge icon + "Merged" chip, and an
/// All / Single / Merged filter narrows the list. The filter is
/// CLIENT-SIDE over the LOADED pages only — pages not yet fetched via
/// "Load more" are not visible to it (server-side run_type query param
/// is a P2 candidate).
class CloudRunsScreen extends StatefulWidget {
  const CloudRunsScreen({super.key});
  @override
  State<CloudRunsScreen> createState() => _CloudRunsScreenState();
}

class _CloudRunsScreenState extends State<CloudRunsScreen> {
  final List<Map<String, dynamic>> _items = [];
  String? _nextCursor;
  bool _loading = true;
  bool _loadingMore = false;
  String? _error;

  /// 1.68 type filter: 'all' | 'single' | 'merged' (client-side over
  /// loaded pages — see class doc).
  String _typeFilter = 'all';

  @override
  void initState() {
    super.initState();
    _loadFirst();
  }

  Future<void> _loadFirst() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final page = await ApiClient.instance.listRuns(limit: 50);
      if (!mounted) return;
      setState(() {
        _items
          ..clear()
          ..addAll((page['items'] as List).cast<Map<String, dynamic>>());
        _nextCursor = page['nextCursor'] as String?;
        _loading = false;
      });
    } on AuthException {
      if (!mounted) return;
      setState(() {
        _error = 'Session expired — sign in again from Settings → Cloud '
            'Account';
        _loading = false;
      });
    } on NetworkException catch (e) {
      if (!mounted) return;
      setState(() {
        _error = 'Offline: ${e.message}';
        _loading = false;
      });
    } on CloudException catch (e) {
      if (!mounted) return;
      setState(() {
        _error = 'Error: ${e.message}';
        _loading = false;
      });
    }
  }

  Future<void> _loadMore() async {
    if (_nextCursor == null || _loadingMore) return;
    setState(() => _loadingMore = true);
    try {
      final page =
          await ApiClient.instance.listRuns(limit: 50, cursor: _nextCursor);
      if (!mounted) return;
      setState(() {
        _items.addAll((page['items'] as List).cast<Map<String, dynamic>>());
        _nextCursor = page['nextCursor'] as String?;
        _loadingMore = false;
      });
    } on CloudException catch (e) {
      if (!mounted) return;
      setState(() => _loadingMore = false);
      ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Load more failed: ${e.message}')));
    }
  }

  static DateTime? _startOf(Map<String, dynamic> run) {
    final raw = run['start_datetime'];
    if (raw is! String) return null;
    return DateTime.tryParse(raw)?.toLocal();
  }

  /// Server contract 1.68: run_type "single" (default for old rows
  /// that predate the field) | "merged".
  static String _typeOf(Map<String, dynamic> run) =>
      run['run_type'] as String? ?? 'single';

  /// Cloud id of a merged run's source runs (empty for singles).
  static List<int> _sourceIdsOf(Map<String, dynamic> run) => [
        for (final v in run['source_run_ids'] as List<dynamic>? ?? const [])
          if (v is num) v.toInt(),
      ];

  List<Map<String, dynamic>> get _visibleItems => _typeFilter == 'all'
      ? _items
      : _items.where((r) => _typeOf(r) == _typeFilter).toList();

  /// 1.69 (JP): title = the row's name — every upload now always carries
  /// one (custom name or computed default "A#x" / "A#x + A#y"). The old
  /// "Merged #id + #id" cloud-id title logic is gone. Fallback for
  /// legacy rows uploaded before 1.69 without a name: generic type label.
  static String _titleOf(Map<String, dynamic> run) {
    final name = run['name'] as String?;
    if (name != null && name.isNotEmpty) return name;
    return _typeOf(run) == 'merged' ? 'Merged run' : 'Single run';
  }

  /// 1.68: small "Merged" chip for merged tiles.
  Widget _mergedBadge() => Container(
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
        decoration: BoxDecoration(
          border: Border.all(color: Colors.deepPurple.shade300),
          borderRadius: BorderRadius.circular(10),
        ),
        child: Text('Merged',
            style: TextStyle(
                fontSize: 10,
                color: Colors.deepPurple.shade400,
                fontWeight: FontWeight.w600)),
      );

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Cloud Runs (yours only)'),
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(48),
          child: Padding(
            padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
            child: SegmentedButton<String>(
              segments: const [
                ButtonSegment(value: 'all', label: Text('All')),
                ButtonSegment(value: 'single', label: Text('Single')),
                ButtonSegment(value: 'merged', label: Text('Merged')),
              ],
              selected: {_typeFilter},
              onSelectionChanged: (selection) =>
                  setState(() => _typeFilter = selection.first),
            ),
          ),
        ),
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _error != null && _items.isEmpty
              ? RefreshIndicator(
                  onRefresh: _loadFirst,
                  child: ListView(children: [
                    Padding(
                      padding: const EdgeInsets.all(32),
                      child: Column(children: [
                        const Icon(Icons.cloud_off,
                            size: 48, color: Colors.grey),
                        const SizedBox(height: 12),
                        Text(_error!,
                            textAlign: TextAlign.center,
                            style: const TextStyle(color: Colors.grey)),
                        const SizedBox(height: 12),
                        const Text('Pull down to retry',
                            style:
                                TextStyle(fontSize: 12, color: Colors.grey)),
                      ]),
                    ),
                  ]),
                )
              : RefreshIndicator(
                  onRefresh: _loadFirst,
                  child: _items.isEmpty
                      ? ListView(children: const [
                          Padding(
                            padding: EdgeInsets.all(32),
                            child: Column(children: [
                              Icon(Icons.cloud_upload,
                                  size: 48, color: Colors.grey),
                              SizedBox(height: 12),
                              Text('Nothing uploaded yet',
                                  style: TextStyle(color: Colors.grey)),
                              SizedBox(height: 4),
                              Text(
                                  'Runs upload automatically while signed in '
                                  'and Cloud Sync is on',
                                  textAlign: TextAlign.center,
                                  style: TextStyle(
                                      fontSize: 12, color: Colors.grey)),
                            ]),
                          ),
                        ])
                      : _buildFilteredList(),
                ),
    );
  }

  /// The type-filtered list. Client-side filter over LOADED pages only
  /// (see class doc); "Load more" keeps appending pages regardless of
  /// the active filter.
  Widget _buildFilteredList() {
    final visible = _visibleItems;
    final showEmptyTypeRow = visible.isEmpty;
    final rowCount = visible.length +
        (showEmptyTypeRow ? 1 : 0) +
        (_nextCursor != null ? 1 : 0);
    return ListView.builder(
      itemCount: rowCount,
      itemBuilder: (context, i) {
        if (i < visible.length) {
          final run = visible[i];
          final start = _startOf(run);
          final dateStr =
              start == null ? '—' : start.toString().substring(0, 16);
          final isMerged = _typeOf(run) == 'merged';
          return Card(
            child: ListTile(
              leading: Icon(isMerged
                  ? Icons.merge_type
                  : Icons.cloud_done_outlined),
              title: Row(children: [
                Expanded(
                  child: Text(_titleOf(run),
                      style: const TextStyle(fontWeight: FontWeight.w500)),
                ),
                if (isMerged) const SizedBox(width: 6),
                if (isMerged) _mergedBadge(),
              ]),
              subtitle: Text(
                  '$dateStr · cloud id #${run['id']}'
                  '${isMerged ? ' · merged from ${_sourceIdsOf(run).map((id) => '#$id').join(' + ')}' : ''}'
                  ' · ${run['format_version'] ?? '?'}'
                  ' fmt'),
              trailing: const Icon(Icons.chevron_right,
                  color: Colors.grey),
              onTap: () {
                Navigator.of(context).push(
                  MaterialPageRoute(
                    builder: (_) => CloudRunDetailScreen(
                        runId: run['id'] as int),
                  ),
                );
              },
            ),
          );
        }
        if (showEmptyTypeRow && i == visible.length) {
          return const Padding(
            padding: EdgeInsets.all(32),
            child: Center(
              child: Text('No runs of this type in the loaded pages',
                  style: TextStyle(color: Colors.grey)),
            ),
          );
        }
        return Padding(
          padding: const EdgeInsets.all(16),
          child: Center(
            child: _loadingMore
                ? const CircularProgressIndicator(strokeWidth: 2)
                : OutlinedButton.icon(
                    icon: const Icon(Icons.expand_more, size: 18),
                    label: const Text('Load more'),
                    onPressed: _loadMore,
                  ),
          ),
        );
      },
    );
  }
}
