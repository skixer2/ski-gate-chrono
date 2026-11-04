import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../cloud/api_client.dart';
import '../../cloud/auth_store.dart';
import '../../cloud/sync_worker.dart';
import '../../cloud/upload_queue.dart';
import '../../config/app_version.dart';

/// Cloud Account (Settings → "Cloud Account").
///
/// Signed out: register (v1: athlete/master self-registration, name IS the
/// login identifier) or sign in. Signed in: account info, sign-out, and
/// the F50 GDPR hard-delete with typed confirmation.
class CloudAccountScreen extends StatefulWidget {
  const CloudAccountScreen({super.key});
  @override
  State<CloudAccountScreen> createState() => _CloudAccountScreenState();
}

class _CloudAccountScreenState extends State<CloudAccountScreen> {
  bool _loading = true;
  bool _busy = false; // register/login/logout/GDPR in flight
  bool _registerMode = false;
  bool _master = false; // false = athlete (v1 user types)
  String? _error;

  final _name = TextEditingController();
  final _password = TextEditingController();
  final _age = TextEditingController();

  CloudUser? _user;
  String? _minAppVersion;
  SyncStatus _sync = const SyncStatus(text: '');

  @override
  void initState() {
    super.initState();
    _reload();
    SyncWorker.instance.status.addListener(_onSync);
  }

  @override
  void dispose() {
    SyncWorker.instance.status.removeListener(_onSync);
    _name.dispose();
    _password.dispose();
    _age.dispose();
    super.dispose();
  }

  void _onSync() {
    if (!mounted) return;
    setState(() => _sync = SyncWorker.instance.status.value);
  }

  Future<void> _reload() async {
    final session = await AuthStore.instance.load();
    if (!mounted) return;
    setState(() {
      _user = session?.user;
      _minAppVersion = ApiClient.instance.minAppVersion;
      _loading = false;
    });
  }

  void _snack(String msg, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(msg),
      backgroundColor: error ? Colors.red : Colors.green,
    ));
  }

  int _verKey(String v) {
    final parts = v.split('.').map((e) => int.tryParse(e) ?? 0).toList();
    while (parts.length < 3) {
      parts.add(0);
    }
    return (parts[0] << 20) | (parts[1] << 10) | parts[2];
  }

  Future<void> _submit() async {
    final name = _name.text.trim();
    final password = _password.text;
    if (name.isEmpty || password.isEmpty) {
      setState(() => _error = 'Name and password are required');
      return;
    }
    if (_registerMode && password.length < 8) {
      setState(() => _error = 'Password must be at least 8 characters');
      return;
    }
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      if (_registerMode) {
        final age = int.tryParse(_age.text.trim());
        // Deployed v1 register returns only the user row (no tokens) —
        // log in right after to establish the session.
        await ApiClient.instance.register(
          name: name,
          password: password,
          userType: _master ? 'master' : 'athlete',
          age: age,
        );
        await ApiClient.instance.login(name: name, password: password);
        _snack('Account created — signed in as $name');
      } else {
        await ApiClient.instance.login(name: name, password: password);
        _snack('Signed in as $name');
      }
      _password.clear();
      await _reload();
      // Push any queued runs as soon as an account appears.
      // ignore: unawaited_futures
      SyncWorker.instance.kick('signed in');
    } on ConflictException {
      setState(() => _error = 'That name is already registered — sign in '
          'instead, or pick another name');
    } on AuthException {
      setState(() => _error = 'Wrong name or password');
    } on ValidationException catch (e) {
      setState(() => _error = e.message);
    } on CloudException catch (e) {
      setState(() => _error = e.message);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _signOut() async {
    setState(() => _busy = true);
    await AuthStore.instance.clear();
    if (!mounted) return;
    setState(() => _busy = false);
    await _reload();
    _snack('Signed out (runs stay queued and upload after sign-in)');
  }

  /// F50 GDPR: typed confirmation → hard delete everything server-side,
  /// then local logout + queue wipe so nothing re-uploads.
  Future<void> _gdprDelete() async {
    final user = _user;
    if (user == null) return;
    final confirmed = await showDialog<String>(
      context: context,
      barrierDismissible: false,
      builder: (_) => _GdprDialog(accountName: user.name),
    );
    if (confirmed == null) return;
    setState(() => _busy = true);
    try {
      await ApiClient.instance.gdprDelete();
      await SyncWorker.instance.reset(); // clears the upload queue
      await AuthStore.instance.clear(); // local logout
      if (!mounted) return;
      _snack('Cloud data deleted (runs, timestamps, barometric data, '
          'profile — irreversible)');
      await _reload();
    } on AuthException {
      if (!mounted) return;
      setState(() => _error = 'Session expired — sign in and retry the '
          'deletion');
    } on CloudException catch (e) {
      if (!mounted) return;
      setState(() => _error = 'Delete failed: ${e.message}');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Cloud Account')),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                if (_minAppVersion != null &&
                    _verKey(APP_VERSION) < _verKey(_minAppVersion!))
                  Card(
                    color: Colors.orange.shade50,
                    child: Padding(
                      padding: const EdgeInsets.all(12),
                      child: Row(children: [
                        const Icon(Icons.system_update, color: Colors.orange),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                              'The cloud server requires app version '
                              '≥ $_minAppVersion — please update',
                              style:
                                  const TextStyle(color: Colors.orange)),
                        ),
                      ]),
                    ),
                  ),
                if (_user == null) _buildSignedOut() else _buildSignedIn(),
                const SizedBox(height: 16),
                _buildSyncCard(),
              ],
            ),
    );
  }

  Widget _buildSignedOut() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Row(children: [
              Icon(Icons.person_off, color: Colors.grey),
              SizedBox(width: 8),
              Text('Not signed in',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
            ]),
            const SizedBox(height: 8),
            Text(
              'Runs upload to the cloud only while signed in. '
              'They queue on this phone in the meantime.',
              style: TextStyle(fontSize: 13, color: Colors.grey.shade700),
            ),
            const SizedBox(height: 16),
            TextField(
              controller: _name,
              enabled: !_busy,
              decoration: const InputDecoration(
                labelText: 'Name (your login)',
                hintText: 'e.g. jp_voyat',
                border: OutlineInputBorder(),
                isDense: true,
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _password,
              enabled: !_busy,
              obscureText: true,
              decoration: InputDecoration(
                labelText: 'Password',
                helperText:
                    _registerMode ? 'At least 8 characters' : null,
                border: const OutlineInputBorder(),
                isDense: true,
              ),
            ),
            if (_registerMode) ...[
              const SizedBox(height: 12),
              TextField(
                controller: _age,
                enabled: !_busy,
                keyboardType: TextInputType.number,
                inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                decoration: const InputDecoration(
                  labelText: 'Age (optional)',
                  border: OutlineInputBorder(),
                  isDense: true,
                ),
              ),
              const SizedBox(height: 12),
              SegmentedButton<bool>(
                segments: const [
                  ButtonSegment(
                      value: false,
                      icon: Icon(Icons.downhill_skiing),
                      label: Text('Athlete')),
                  ButtonSegment(
                      value: true,
                      icon: Icon(Icons.school),
                      label: Text('Master')),
                ],
                selected: {_master},
                onSelectionChanged: _busy
                    ? null
                    : (s) => setState(() => _master = s.first),
              ),
            ],
            if (_error != null)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Text(_error!,
                    style: const TextStyle(color: Colors.red, fontSize: 13)),
              ),
            const SizedBox(height: 16),
            SizedBox(
              width: double.infinity,
              child: FilledButton.icon(
                icon: _busy
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2))
                    : Icon(_registerMode ? Icons.person_add : Icons.login),
                label: Text(_registerMode ? 'Create account' : 'Sign in'),
                onPressed: _busy ? null : _submit,
              ),
            ),
            TextButton(
              onPressed:
                  _busy ? null : () => setState(() {
                        _registerMode = !_registerMode;
                        _error = null;
                      }),
              child: Text(_registerMode
                  ? 'Already have an account? Sign in'
                  : 'No account yet? Create one'),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSignedIn() {
    final u = _user!;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              const Icon(Icons.verified_user, color: Colors.green),
              const SizedBox(width: 8),
              Expanded(
                child: Text(u.name,
                    style: const TextStyle(
                        fontSize: 18, fontWeight: FontWeight.bold)),
              ),
            ]),
            const SizedBox(height: 12),
            _row('User ID', '${u.id}'),
            _row('Type', u.userType),
            if (u.age != null) _row('Age', '${u.age}'),
            if (u.category != null && u.category!.isNotEmpty)
              _row('Category', u.category!),
            const SizedBox(height: 16),
            SizedBox(
              width: double.infinity,
              child: OutlinedButton.icon(
                icon: const Icon(Icons.logout, size: 18),
                label: const Text('Sign out'),
                onPressed: _busy ? null : _signOut,
              ),
            ),
            const SizedBox(height: 8),
            SizedBox(
              width: double.infinity,
              child: FilledButton.tonalIcon(
                icon: const Icon(Icons.no_accounts, size: 18),
                label: const Text('Delete my cloud data…',
                    style: TextStyle(color: Colors.red)),
                style: FilledButton.styleFrom(
                  backgroundColor: Colors.red.shade50,
                ),
                onPressed: _busy ? null : _gdprDelete,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSyncCard() {
    final q = UploadQueue.instance;
    return Card(
      child: ListTile(
        leading: _sync.busy
            ? const SizedBox(
                width: 24,
                height: 24,
                child: CircularProgressIndicator(strokeWidth: 2))
            : const Icon(Icons.cloud_sync),
        title: const Text('Upload queue'),
        subtitle: Text(_sync.text.isEmpty
            ? '${q.pendingCount} pending · ${q.doneCount} uploaded'
                '${q.poisonedCount > 0 ? ' · ${q.poisonedCount} blocked' : ''}'
            : _sync.text),
        trailing: IconButton(
          icon: const Icon(Icons.refresh),
          tooltip: 'Sync now',
          onPressed: () => SyncWorker.instance.kick('manual'),
        ),
      ),
    );
  }

  Widget _row(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(children: [
        SizedBox(
            width: 100,
            child: Text(label,
                style:
                    TextStyle(color: Colors.grey.shade600, fontSize: 13))),
        Text(value,
            style:
                const TextStyle(fontWeight: FontWeight.w500, fontSize: 13)),
      ]),
    );
  }
}

/// F50 confirmation dialog: lists EXACTLY what is deleted and requires the
/// user to type their account name. Own StatefulWidget so the controller
/// lives with the route (the 1.61.1 use-after-dispose lesson).
class _GdprDialog extends StatefulWidget {
  final String accountName;
  const _GdprDialog({required this.accountName});

  @override
  State<_GdprDialog> createState() => _GdprDialogState();
}

class _GdprDialogState extends State<_GdprDialog> {
  final _ctrl = TextEditingController();

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  bool get _matches => _ctrl.text.trim() == widget.accountName;

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Delete all cloud data?'),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'This permanently and irreversibly deletes EVERYTHING stored on '
            'the SGC cloud for this account:',
            style: TextStyle(fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 8),
          const Text('• All your runs\n'
              '• All gate timestamps\n'
              '• All barometric altitude/speed series\n'
              '• Your profile and account itself'),
          const SizedBox(height: 8),
          const Text('Local runs on this phone are NOT touched. '
              'This cannot be undone.'),
          const SizedBox(height: 12),
          Text(
            'Type "${widget.accountName}" to confirm:',
            style: const TextStyle(fontSize: 13),
          ),
          TextField(
            controller: _ctrl,
            autofocus: true,
            onChanged: (_) => setState(() {}),
            decoration: const InputDecoration(
              hintText: 'Account name',
              border: OutlineInputBorder(),
              isDense: true,
            ),
          ),
        ],
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Cancel'),
        ),
        FilledButton(
          style: FilledButton.styleFrom(backgroundColor: Colors.red),
          onPressed:
              _matches ? () => Navigator.pop(context, _ctrl.text.trim()) : null,
          child: const Text('Delete everything'),
        ),
      ],
    );
  }
}
