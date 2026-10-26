import 'package:flutter/material.dart';

import '../../cloud/api_client.dart';
import 'cloud_run_detail_screen.dart';

/// F48 (P1 slice): browse YOUR OWN uploaded runs (GET /v1/runs, keyset-
/// paged with lazy "load more"). Tapping a run opens a read-only detail
/// view — full local-vs-cloud comparison UI is P2/P3, deliberately not
/// built here.
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

  static String _titleOf(Map<String, dynamic> run) {
    final name = run['name'] as String?;
    if (name != null && name.isNotEmpty) return name;
    return 'Run #${run['run_number'] ?? run['id']}';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Cloud Runs (yours only)')),
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
                      : ListView.builder(
                          itemCount:
                              _items.length + (_nextCursor != null ? 1 : 0),
                          itemBuilder: (context, i) {
                            if (i >= _items.length) {
                              return Padding(
                                padding: const EdgeInsets.all(16),
                                child: Center(
                                  child: _loadingMore
                                      ? const CircularProgressIndicator(
                                          strokeWidth: 2)
                                      : OutlinedButton.icon(
                                          icon: const Icon(
                                              Icons.expand_more, size: 18),
                                          label:
                                              const Text('Load more'),
                                          onPressed: _loadMore,
                                        ),
                                ),
                              );
                            }
                            final run = _items[i];
                            final start = _startOf(run);
                            final dateStr = start == null
                                ? '—'
                                : start.toString().substring(0, 16);
                            return Card(
                              child: ListTile(
                                leading:
                                    const Icon(Icons.cloud_done_outlined),
                                title: Text(_titleOf(run),
                                    style: const TextStyle(
                                        fontWeight: FontWeight.w500)),
                                subtitle: Text(
                                    '$dateStr · cloud id #${run['id']}'
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
                          },
                        ),
                ),
    );
  }
}
