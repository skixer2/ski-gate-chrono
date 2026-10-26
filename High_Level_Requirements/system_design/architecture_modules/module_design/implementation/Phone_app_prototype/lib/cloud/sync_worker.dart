import 'dart:async';

import 'package:connectivity_plus/connectivity_plus.dart';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../processing/decompressor.dart';
import '../processing/gate_time_estimator.dart';
import '../processing/impact_detector.dart';
import '../storage/local_storage.dart';
import 'api_client.dart';
import 'auth_store.dart';
import 'upload_queue.dart';

/// Builds the F23 upload payload from a run's compressed bytes.
///
/// TOP-LEVEL function so it can run inside compute() (background isolate)
/// — the decode + gate estimation is the same heavy pipeline the run
/// detail screen runs on the UI thread; here it must never jank the UI.
///
/// Payload mapping (mirrors what the user actually sees in the app):
/// - timestamps[] ← GateTimeEstimator.estimate() (Bronze tier, course ==
///   null) — the exact gate list rendered by GateTimestampTable in
///   run_detail_screen. `timestamp_ms` = GateTimestamp.msFromStart,
///   `guessed` = GateTimestamp.isEstimated (F27 estimated flag),
///   `arm_side` = the run's own arm from the file header (one run is ONE
///   arm's recording; the gate's L/R course side is a different concept).
/// - barometric_data[] ← 10 Hz decimation of the 100 Hz frames, the same
///   series the altitude/speed charts render (run_detail_screen's
///   _computeBaroData): altitude from frame.baroAltitudeM, speed_mps =
///   vertical speed = altitude derivative (signed m/s).
Map<String, dynamic> buildRunUploadPayload(Uint8List compressed) {
  final decoded = Decompressor().decompressFull(compressed);
  final hdr = decoded.header;
  final frames = decoded.frames;
  final arm = hdr.armSide == 1 ? 'R' : 'L';

  final impacts =
      ImpactDetector(multiplier: 2.5, baselineWindow: 50).detect(frames);
  final gates = GateTimeEstimator(course: null, knownImpacts: impacts)
      .estimate(frames);

  final baro = <Map<String, dynamic>>[];
  for (int i = 0; i < frames.length; i += 10) {
    final f = frames[i];
    double speed = 0;
    if (i >= 10) {
      final dtMs = f.msFromStart - frames[i - 10].msFromStart;
      if (dtMs > 0) {
        speed =
            (f.baroAltitudeM - frames[i - 10].baroAltitudeM) / (dtMs / 1000.0);
      }
    }
    baro.add({
      'time_offset_ms': f.msFromStart,
      'altitude_m': f.baroAltitudeM,
      'speed_mps': speed,
    });
  }

  return {
    'format_version': hdr.formatVersion,
    'arm_side': arm,
    'timestamps': [
      for (final g in gates)
        {
          'timestamp_ms': g.msFromStart,
          'arm_side': arm,
          'guessed': g.isEstimated,
        },
    ],
    'barometric_data': baro,
  };
}

/// One-liner sync state for the UI indicator (queue depth + last result).
class SyncStatus {
  final bool busy;
  final String text;
  const SyncStatus({this.busy = false, this.text = ''});
}

/// F43 sync worker: drains the UploadQueue to POST /v1/runs.
///
/// Single-flight (concurrent kicks coalesce), triggered by:
///   - app start (init),
///   - connectivity regained (connectivity_plus stream),
///   - every run save+decode completion (RunListScreen calls kick()),
///   - the exponential-backoff retry timer.
///
/// Never enqueues when the profile push_to_cloud toggle is off; never
/// uploads when signed out (entries just wait in the queue).
class SyncWorker {
  static final SyncWorker instance = SyncWorker._();
  SyncWorker._();

  static const _prefsPush = 'sgc.profile.push';

  final UploadQueue _queue = UploadQueue.instance;
  final LocalStorage _storage = LocalStorage();

  /// UI indicator: listen with a ValueListenableBuilder.
  final ValueNotifier<SyncStatus> status =
      ValueNotifier(const SyncStatus(text: ''));

  bool _busy = false;
  bool _pendingKick = false;
  bool _initialized = false;
  Timer? _retryTimer;
  StreamSubscription<List<ConnectivityResult>>? _connSub;

  /// App-start init: queue load, connectivity trigger, first pass.
  /// Idempotent (main.dart calls it once, but cheap to guard).
  Future<void> init() async {
    if (_initialized) return;
    _initialized = true;
    await _queue.load();
    try {
      _connSub = Connectivity().onConnectivityChanged.listen((results) {
        final hasNetwork = results.any((r) => r != ConnectivityResult.none);
        if (hasNetwork) kick('connectivity regained');
      });
    } catch (e) {
      debugPrint('[SyncWorker] connectivity stream unavailable: $e');
    }
    // Small delay: let the first frame render before any decode work.
    Timer(const Duration(seconds: 3), () => kick('app start'));
  }

  void _setText(String text, {bool busy = false}) {
    status.value = SyncStatus(busy: busy, text: text);
  }

  /// Trigger a pass. Single-flight: if a pass is running, remember the
  /// kick and run exactly one follow-up pass afterwards (no queue spin).
  Future<void> kick(String reason) async {
    if (_busy) {
      _pendingKick = true;
      return;
    }
    _busy = true;
    // Keep the last status text, flip the busy flag so listers show the
    // progress spinner.
    status.value = SyncStatus(busy: true, text: status.value.text);
    try {
      await _runPass(reason);
    } catch (e, st) {
      debugPrint('[SyncWorker] pass crashed: $e\n$st');
      _setText('Cloud sync error: $e');
    } finally {
      _busy = false;
    }
    if (_pendingKick) {
      _pendingKick = false;
      // Coalesced re-run (e.g. a save landed mid-pass).
      // ignore: unawaited_futures
      kick('coalesced');
    }
  }

  Future<void> _runPass(String reason) async {
    await ApiClient.instance.ready;
    await _queue.load();
    _retryTimer?.cancel();

    final prefs = await SharedPreferences.getInstance();
    final push = prefs.getBool(_prefsPush) ?? true;
    if (!push) {
      _setText('Cloud push off — queue idle '
          '(${_queue.pendingCount} pending, ${_queue.poisonedCount} blocked)');
      return;
    }

    // Backfill: every local run not yet queued gets an entry (stable uuid
    // minted once). Purely local bookkeeping — nothing uploads while
    // signed out, but this keeps the queue depth truthful in the UI.
    final runs = await _storage.listAll();
    final byFile = {for (final r in runs) r.fileName: r};
    for (final fileName in byFile.keys) {
      await _queue.enqueue(fileName);
    }

    if (!await AuthStore.instance.isSignedIn()) {
      _setText('Not signed in — ${_queue.pendingCount} run(s) waiting '
          'to upload');
      return;
    }

    final due = _queue.dueEntries(DateTime.now());
    if (due.isEmpty) {
      final waiting = _queue.pendingCount;
      _setText(waiting > 0
          ? 'Cloud: $waiting pending · retry scheduled'
          : 'Cloud up to date · ${_queue.doneCount} uploaded');
      _armRetryTimer();
      return;
    }

    debugPrint('[SyncWorker] pass "$reason": ${due.length} due');
    int ok = 0, retried = 0, poisoned = 0;
    for (final entry in due) {
      // GDPR can sign us out mid-pass — stop immediately, queue is cleared.
      if (await AuthStore.instance.isSignedIn() != true) {
        _setText('Signed out — sync stopped');
        return;
      }
      try {
        final saved = byFile[entry.fileName];
        final data = await _storage.load(entry.fileName);
        if (data == null || saved == null) {
          // Local file gone: nothing can ever be built from it.
          await _queue.markPoisoned(entry.clientRunUuid,
              'local run file missing (${entry.fileName})');
          poisoned++;
          continue;
        }

        // Heavy decode + payload build off the UI thread.
        final decodedPart = await compute(buildRunUploadPayload, data);
        final startUtc = DateTime.fromMillisecondsSinceEpoch(
            saved.timestamp * 1000,
            isUtc: true);
        final payload = <String, dynamic>{
          'client_run_uuid': entry.clientRunUuid,
          'format_version': decodedPart['format_version'],
          'start_datetime': startUtc.toIso8601String(), // aware UTC ("Z")
          'run_number': saved.id, // informational device run id
          if (saved.name != null && saved.name!.isNotEmpty)
            'name': saved.name, // F45 label rides along (1.61 note)
          'timestamps': decodedPart['timestamps'],
          'barometric_data': decodedPart['barometric_data'],
        };

        final resp = await ApiClient.instance.uploadRun(payload);
        await _queue.markDone(
            entry.clientRunUuid, resp['id'] as int);
        ok++;
      } on TooLargeException catch (e) {
        await _queue.markPoisoned(entry.clientRunUuid, e.message);
        poisoned++;
      } on ValidationException catch (e) {
        await _queue.markPoisoned(entry.clientRunUuid, e.message);
        poisoned++;
      } on AuthException {
        // Refresh already attempted inside the client and failed — the
        // session is dead. Stop the pass; the user must sign in again.
        _setText('Sign-in expired — sign in again to upload '
            '(${_queue.pendingCount} waiting)');
        return;
      } on ActivationException catch (e) {
        await _queue.markRetry(entry.clientRunUuid, e.message);
        retried++;
      } on NetworkException catch (e) {
        await _queue.markRetry(entry.clientRunUuid, e.message);
        retried++;
      } on ServerException catch (e) {
        await _queue.markRetry(entry.clientRunUuid, e.message);
        retried++;
      } on CloudException catch (e) {
        // Any other 4xx with the same bytes will fail identically.
        await _queue.markPoisoned(entry.clientRunUuid, e.message);
        poisoned++;
      } catch (e) {
        await _queue.markRetry(entry.clientRunUuid, e.toString());
        retried++;
      }
    }

    final pendingLeft = _queue.pendingCount;
    _setText('Cloud: +$ok uploaded'
        '${retried > 0 ? ' · $retried retrying' : ''}'
        '${poisoned > 0 ? ' · $poisoned blocked' : ''}'
        '${pendingLeft > 0 ? ' · $pendingLeft left' : ''}');
    _armRetryTimer();
  }

  /// Arm the timer for the earliest scheduled retry so a fully offline
  /// pass still re-fires later without connectivity/save events.
  void _armRetryTimer() {
    final next = _queue.nextRetryAt;
    if (next == null) return;
    final delay = next.difference(DateTime.now());
    _retryTimer = Timer(
        delay < const Duration(seconds: 5)
            ? const Duration(seconds: 5)
            : delay,
        () => kick('retry timer'));
  }

  /// Called after GDPR delete: stop everything, wipe the queue.
  Future<void> reset() async {
    _retryTimer?.cancel();
    await _queue.clear();
    _setText('Cloud data deleted — queue cleared');
  }

  void dispose() {
    // Not a widget lifecycle object; only called in tests.
    _retryTimer?.cancel();
    _connSub?.cancel();
  }
}
