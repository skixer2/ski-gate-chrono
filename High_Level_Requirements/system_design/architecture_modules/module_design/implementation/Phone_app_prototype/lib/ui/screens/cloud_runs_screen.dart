import 'package:flutter/material.dart';

import '../../cloud/api_client.dart';
import '../../cloud/upload_queue.dart';
import '../../storage/local_storage.dart';
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
///
/// 1.72 (JP redesign): every row gets a trailing Synch switch — a cloud
/// row IS synced by definition, so the switch starts checked.
///  * Uncheck on a row that STILL EXISTS locally (client_run_uuid →
///    queue entry → fileName → local file/merge entry) = desync: light
///    confirm, cloud DELETE, the local queue entry becomes DESYNCED
///    (dead — no auto re-upload). The local archive stays.
///  * Orphan rows (no local copy anywhere along that chain — the phone
///    copy was deleted) can never re-sync, so they show a delete icon
///    instead of a switch: STRONG definitive warning first ("no copy
///    will remain anywhere"), then cloud DELETE. The row disappears on
///    the next refresh (removed from the loaded list immediately).
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

  /// 1.72 local presence, refreshed on every (re)load: local run
  /// fileNames (runs.json) and merged queue keys (merged_runs.json).
  /// Backs the orphan detection for the per-row Synch switch.
  Set<String> _localSingleFiles = const {};
  Set<String> _localMergeKeys = const {};

  /// 1.72: a desync/delete API call in flight for a row — the switch is
  /// disabled meanwhile (single-flight per row, no double DELETEs).
  final Set<int> _busyRowIds = {};

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
    // 1.72: queue + local presence must be current before any row renders
    // its switch (orphan detection reads the queue). Idempotent + cheap.
    await UploadQueue.instance.load();
    await _refreshLocalPresence();
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

  // ── 1.72: per-row Synch switch / orphan delete ────────────────────

  /// Refresh the local-presence sets (runs.json + merged_runs.json).
  Future<void> _refreshLocalPresence() async {
    final runs = await LocalStorage().listAll();
    final merges = await LocalStorage().loadMerges();
    if (!mounted) return;
    setState(() {
      _localSingleFiles = {for (final r in runs) r.fileName};
      _localMergeKeys = {
        for (final m in merges)
          if (m['aFile'] is String && m['bFile'] is String)
            UploadQueue.mergedQueueKey(
                m['aFile'] as String, m['bFile'] as String),
      };
    });
  }

  /// Orphan detection (1.72). A cloud row still has a local source when
  /// the chain client_run_uuid → queue entry → fileName → local copy
  /// resolves: singles by fileName in runs.json, merged by the merged
  /// key in merged_runs.json. Anything else (row without a usable uuid,
  /// pruned/corrupt queue, or the local copy deleted) = ORPHAN: no
  /// re-sync source exists, so the row gets a delete icon instead of a
  /// switch and deleting it is definitive.
  bool _hasLocalCopy(Map<String, dynamic> run) {
    final uuid = run['client_run_uuid'];
    if (uuid is! String || uuid.isEmpty) return false;
    final entry = UploadQueue.instance.forUuid(uuid);
    if (entry == null) return false;
    return entry.runType == UploadRunType.merged
        ? _localMergeKeys.contains(entry.fileName)
        : _localSingleFiles.contains(entry.fileName);
  }

  UploadEntry? _entryFor(Map<String, dynamic> run) {
    final uuid = run['client_run_uuid'];
    if (uuid is! String || uuid.isEmpty) return null;
    return UploadQueue.instance.forUuid(uuid);
  }

  /// Trailing widget: non-orphans get the Synch switch (a cloud row IS
  /// synced by definition — starts checked), orphans get a delete icon.
  Widget _trailingFor(Map<String, dynamic> run) {
    if (!_hasLocalCopy(run)) {
      final busy = _busyRowIds.contains(run['id'] as int);
      return IconButton(
        icon: const Icon(Icons.delete_outline, color: Colors.red),
        tooltip: 'Delete from cloud (no local copy left)',
        onPressed: busy ? null : () => _deleteOrphanFromCloud(run),
      );
    }
    return Switch(
      value: true,
      onChanged: _busyRowIds.contains(run['id'] as int)
          ? null
          : (_) => _desyncFromCloud(run),
    );
  }

  /// Desync from the CLOUD surface (Synch switch ON→OFF on a row that
  /// still exists locally): light confirm → cloud DELETE → local queue
  /// entry marked DESYNCED (dead: the sync worker can never re-upload it
  /// automatically) + pending renames dropped. The local archive and the
  /// device copy stay; re-syncing from the Runs screen creates a NEW row.
  Future<void> _desyncFromCloud(Map<String, dynamic> run) async {
    final rowId = run['id'] as int;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Remove from cloud?'),
        content: const Text('Phone and device copies stay.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Remove'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;
    setState(() => _busyRowIds.add(rowId));
    try {
      try {
        await ApiClient.instance.deleteRun(rowId);
      } on CloudException catch (e) {
        // 404/410 "already gone" = desynced all the same.
        if (e.statusCode != 404 && e.statusCode != 410) rethrow;
        debugPrint('[CloudRuns] row #$rowId already gone '
            '(HTTP ${e.statusCode}) — marking desynced');
      }
      final entry = _entryFor(run);
      if (entry != null) {
        await UploadQueue.instance.markDesynced(entry.clientRunUuid);
        await UploadQueue.instance.removeRenamesForKey(entry.fileName);
      }
      if (!mounted) return;
      setState(() {
        _items.remove(run);
        _busyRowIds.remove(rowId);
      });
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content:
              Text('Removed from cloud — phone and device copies kept')));
    } on CloudException catch (e) {
      if (!mounted) return;
      setState(() => _busyRowIds.remove(rowId));
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Cloud delete failed — row kept. ($e)'),
          backgroundColor: Colors.red));
    } catch (e) {
      if (!mounted) return;
      setState(() => _busyRowIds.remove(rowId));
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Cloud delete failed — row kept. ($e)'),
          backgroundColor: Colors.red));
    }
  }

  /// ORPHAN delete (no local copy left): the STRONG definitive warning
  /// first — with the phone copy gone, this removes the last copy
  /// anywhere — then the cloud DELETE. No queue bookkeeping applies (no
  /// local copy ⇒ usually no entry; a stale one is removed for hygiene).
  Future<void> _deleteOrphanFromCloud(Map<String, dynamic> run) async {
    final rowId = run['id'] as int;
    final isMerged = _typeOf(run) == 'merged';
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Delete "${_titleOf(run)}" from the cloud?'),
        content: Text(
          '⚠ This deletes it completely — no copy will remain anywhere '
          '(the copy on this phone was already deleted). This cannot be '
          'undone.${isMerged ? '\n\nOnly this merged cloud row is removed.' : ''}',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: TextButton.styleFrom(foregroundColor: Colors.red),
            child: const Text('Delete forever'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;
    setState(() => _busyRowIds.add(rowId));
    try {
      try {
        await ApiClient.instance.deleteRun(rowId);
      } on CloudException catch (e) {
        if (e.statusCode != 404 && e.statusCode != 410) rethrow;
        debugPrint('[CloudRuns] orphan row #$rowId already gone '
            '(HTTP ${e.statusCode})');
      }
      // Hygiene: a stale queue entry (file deleted elsewhere) must not
      // linger — the run no longer exists on EITHER side.
      final entry = _entryFor(run);
      if (entry != null) {
        await UploadQueue.instance.removeFile(entry.fileName);
        await UploadQueue.instance.removeRenamesForKey(entry.fileName);
      }
      if (!mounted) return;
      setState(() {
        _items.remove(run);
        _busyRowIds.remove(rowId);
      });
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Deleted from the cloud — no copy remains')));
    } on CloudException catch (e) {
      if (!mounted) return;
      setState(() => _busyRowIds.remove(rowId));
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Cloud delete failed — row kept. ($e)'),
          backgroundColor: Colors.red));
    } catch (e) {
      if (!mounted) return;
      setState(() => _busyRowIds.remove(rowId));
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Cloud delete failed — row kept. ($e)'),
          backgroundColor: Colors.red));
    }
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
                  '${_hasLocalCopy(run) ? '' : ' · no local copy'}'
                  '${isMerged ? ' · merged from ${_sourceIdsOf(run).map((id) => '#$id').join(' + ')}' : ''}'
                  ' · ${run['format_version'] ?? '?'}'
                  ' fmt'),
              trailing: _trailingFor(run),
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
