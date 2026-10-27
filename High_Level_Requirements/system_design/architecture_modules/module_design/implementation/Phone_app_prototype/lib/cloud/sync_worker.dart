import 'dart:async';
import 'dart:typed_data';

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

/// 1.68 merged-course support: header part of the a-source run (the
/// merge entry itself stores no format version — read it from the real
/// bytes so the cloud row reflects the firmware family the sources
/// were recorded with). Runs inside compute() like the single payload.
Map<String, dynamic> buildMergedHeaderPart(Uint8List compressed) {
  final decoded = Decompressor().decompressFull(compressed);
  return {'format_version': decoded.header.formatVersion};
}

/// Fixed short backoff while a merged entry waits for its sources to
/// upload (lineage deferral — see _runPass). Deliberately NOT the
/// failure backoff: waiting is normal, not an error.
const Duration _kLineageDeferDelay = Duration(minutes: 2);

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

    // 1.68 merged backfill: every persisted merge not yet queued gets an
    // entry with a DETERMINISTIC uuid (mergedRunUuid) — idempotent by
    // uuid, so this runs at every pass / app start for free and picks up
    // merges that existed before the feature shipped.
    final merges = await _storage.loadMerges();
    final mergesByKey = <String, Map<String, dynamic>>{};
    for (final m in merges) {
      final aFile = m['aFile'] as String?;
      final bFile = m['bFile'] as String?;
      final aId = m['aId'] as int?;
      final bId = m['bId'] as int?;
      if (aFile == null || bFile == null || aId == null || bId == null) {
        debugPrint('[SyncWorker] skipping malformed merge entry '
            '(aFile=$aFile bFile=$bFile aId=$aId bId=$bId)');
        continue;
      }
      final key = UploadQueue.mergedQueueKey(aFile, bFile);
      mergesByKey[key] = m;
      await _queue.enqueueMerged(
        key,
        mergedRunUuid(aId: aId, bId: bId, aFile: aFile, bFile: bFile),
      );
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
    // 1.68: singles FIRST — merged lineage resolves from DONE single
    // entries, so processing singles first lets a same-pass merged run
    // upload immediately instead of deferring to the next pass.
    due.sort((a, b) {
      final am = a.runType == UploadRunType.merged ? 1 : 0;
      final bm = b.runType == UploadRunType.merged ? 1 : 0;
      return am.compareTo(bm);
    });
    int ok = 0, retried = 0, poisoned = 0, deferred = 0;
    for (final entry in due) {
      // GDPR can sign us out mid-pass — stop immediately, queue is cleared.
      if (await AuthStore.instance.isSignedIn() != true) {
        _setText('Signed out — sync stopped');
        return;
      }
      try {
        // ── 1.68 merged-course branch ──
        if (entry.runType == UploadRunType.merged) {
          final merge = mergesByKey[entry.fileName];
          if (merge == null) {
            // Merge entry vanished from merged_runs.json (quarantined
            // corrupt file / future delete feature): no data to build a
            // payload from — a retry can never succeed.
            await _queue.markPoisoned(entry.clientRunUuid,
                'merge entry missing locally (${entry.fileName})');
            poisoned++;
            continue;
          }
          final aFile = merge['aFile'] as String;
          final bFile = merge['bFile'] as String;

          // Lineage: cloud ids of the two source runs, resolved from
          // their DONE queue entries. Sources still PENDING → defer
          // (short fixed backoff, no attempt inflation). Sources
          // permanently POISONED — or already deleted from the phone —
          // upload WITHOUT their id rather than blocking forever.
          final sourceIds = <int>[];
          final waiting = <String>[];
          for (final src in [aFile, bFile]) {
            final se = _queue.forFile(src);
            if (se != null &&
                se.status == UploadStatus.done &&
                se.runId != null) {
              sourceIds.add(se.runId!);
            } else if (se != null && se.status == UploadStatus.poisoned) {
              // permanent — proceed without this lineage id
            } else if (!byFile.containsKey(src)) {
              // source run no longer on the phone — lineage can never
              // resolve; proceed without it
            } else {
              waiting.add(src);
            }
          }
          if (waiting.isNotEmpty) {
            await _queue.markDeferred(
                entry.clientRunUuid, _kLineageDeferDelay);
            debugPrint('[SyncWorker] merged ${entry.fileName} deferred: '
                '${waiting.length} source(s) not uploaded yet');
            deferred++;
            continue;
          }

          final aSaved = byFile[aFile];
          final aData = await _storage.load(aFile);
          if (aSaved == null || aData == null) {
            // Vanished between the lineage check and here (user deleted
            // mid-pass): too transient to poison — try again shortly.
            await _queue.markDeferred(
                entry.clientRunUuid, _kLineageDeferDelay);
            deferred++;
            continue;
          }

          // start = source a-run's header datetime (aSaved is promoted
          // non-null by the guard above, same pattern as the singles
          // path); merge['created'] stays as a documented fallback for
          // the theoretical case the source row disappears pre-guard.
          final created = DateTime.tryParse(merge['created'] as String? ?? '');
          final startIso = aSaved != null
              ? DateTime.fromMillisecondsSinceEpoch(
                  aSaved.timestamp * 1000,
                  isUtc: true)
              .toIso8601String()
              : (created?.toUtc() ?? DateTime.now().toUtc())
              .toIso8601String();
          final hdrPart = await compute(buildMergedHeaderPart, aData);
          final gates =
              (merge['gates'] as List? ?? const [])
                  .cast<Map<String, dynamic>>();
          final baroIn =
              (merge['baro'] as List? ?? const [])
                  .cast<Map<String, dynamic>>();

          // RunMerger gate keys (ms/side/estimated) → upload shape.
          final timestamps = [
            for (final g in gates)
              {
                'timestamp_ms': (g['ms'] as num).toInt(),
                'arm_side': g['side'] as String? ?? 'L',
                'guessed': g['estimated'] == true,
              },
          ];

          // RunMerger baro keys (ms/alt) → single-run 10 Hz shape;
          // vertical speed is the altitude derivative (merged entries
          // store no speed of their own).
          final baroOut = <Map<String, dynamic>>[];
          for (int i = 0; i < baroIn.length; i++) {
            final ms = (baroIn[i]['ms'] as num).toInt();
            final alt = (baroIn[i]['alt'] as num).toDouble();
            double speed = 0;
            if (i > 0) {
              final dtMs = ms - (baroIn[i - 1]['ms'] as num).toInt();
              if (dtMs > 0) {
                speed = (alt - (baroIn[i - 1]['alt'] as num).toDouble()) /
                    (dtMs / 1000.0);
              }
            }
            baroOut.add({
              'time_offset_ms': ms,
              'altitude_m': alt,
              'speed_mps': speed,
            });
          }

          // 1.69: label = custom merge name if set, else the composed
          // default ("<A#x> + <A#y>") — always present in the payload so
          // every cloud row has a readable name.
          final customMergeName = merge['name'] as String?;
          final mergeLabel =
              (customMergeName != null && customMergeName.isNotEmpty)
                  ? customMergeName
                  : await _storage.defaultMergeName(merge, byFile: byFile);
          final payload = <String, dynamic>{
            'client_run_uuid': entry.clientRunUuid,
            'format_version': hdrPart['format_version'],
            'start_datetime': startIso, // aware UTC ("Z")
            'run_number': (merge['aId'] as num).toInt(),
            'name': mergeLabel,
            'run_type': 'merged',
            if (sourceIds.isNotEmpty) 'source_run_ids': sourceIds,
            'timestamps': timestamps,
            'barometric_data': baroOut,
          };

          final resp = await ApiClient.instance.uploadRun(payload);
          await _queue.markDone(entry.clientRunUuid, resp['id'] as int);
          ok++;
          continue;
        }

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
        // 1.69: label = custom name if set, else the computed default
        // ("<deviceLetter>#<device run id>") — always present so the cloud
        // row matches what the local list shows.
        final customName = saved.name;
        final runLabel = (customName != null && customName.isNotEmpty)
            ? customName
            : await _storage.defaultRunName(saved);
        final payload = <String, dynamic>{
          'client_run_uuid': entry.clientRunUuid,
          'format_version': decodedPart['format_version'],
          'start_datetime': startUtc.toIso8601String(), // aware UTC ("Z")
          'run_number': saved.id, // informational device run id
          'name': runLabel,
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
        '${deferred > 0 ? ' · $deferred waiting for sources' : ''}'
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
