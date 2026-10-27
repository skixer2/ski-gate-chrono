/// Single source of truth for the phone-app version.
///
/// Bump on every code change, same convention as the firmware's `config.h`.
///
/// History:
///   1.0  – pre-v2.20 (no version tracking)
///   1.1  – v2.20: BLE download fix (FT_ERROR→fail fast, request-write throws,
///          run-count source changed from total→stored, char_run_list 100→512)
///   1.2  – v2.22: LittleFS config fix (invalid prog_size/block_size → silent
///          reformat on every boot, wiping all runs)
///   1.3  – Q14 quaternion scaling, local run storage, X-axis labels,
///          navigation download→detail, altitude clamping, pktType guard
///   1.4  – CRC32 validation on download + local open (BLE corruption diagnostic)
///   1.5  – Auto-retry on CRC failure (BLE GATT notification loss recovery)
///   1.6  – Abort navigation on CRC fail (no more "No gates detected" from corrupt data)
///   1.7  – validateCRC computes over payload only (skip 16-byte RunHeader),
///          matching firmware close_run(); was always failing on BLE downloads
///   1.8  – FT download timeout 60→120 s + progress/FT_ERROR logs (match FW 4.95
///          20 B @ 25 ms ~41 s for 32 KB); complete on status 2 or 3
///   1.9  – ImpactDetector |g| = 9810 mm/s² (was 9.81) — matches firmware LA units
///   1.10 – Download ALL missing runs (not just latest): track device vs local
///          run ids; "Download Missing Runs (N)" offers every undownloaded run
///   1.11 – Composite run identity (id+ts) so recycled run ids after device
///          reset are not treated as already-downloaded; BLE connection
///          lifecycle fixes (disconnect listener, GATT cache clear, clean
///          rescan) so a device HW reset no longer requires a full app restart
///   1.12 – requestConnectionPriority(balanced) after MTU request to negotiate
///          a longer BLE supervision timeout (~2-5s instead of phone default
///          ~500ms). Fixes LINK_SUPERVISION_TIMEOUT during FT downloads
///          (TC-2026-08-15-001).
///   1.13 – Rewrite FT download as phone-pull request-response: phone writes
///          chunk offset to ABCA, device responds with one notification on
///          ABCB. Eliminates GATT queue overflow that hung transfers after
///          ~14 KB at ~400 B/s push ceiling. Adds CRC32 read from ABCC.
//   1.14 – FT pull timeout 10→15s; chunkSize from negotiated MTU not hardcoded;
//          log MTU at pull-loop start. S22 GATT can stall on 244 B notifications.
//   1.15 – Inter-chunk delay 30ms to prevent S22 BLE buffer exhaustion
//          (LINK_SUPERVISION_TIMEOUT after ~35 chunks without delay).
//   1.16 – Inter-chunk delay 30→50ms (3 runs OK then crash on 4th at 30ms);
//          download progress UI (run X/N, KB received, progress bar).
//   1.17 – Inter-chunk delay 50→20ms + 2s inter-run cooldown. Faster per-run
//          transfer, with breathing room between runs to flush BLE buffers.
//   1.18 – Switch back to device-push (V4.97 style): phone sends CMD_START,
//          device streams 244 B chunks @ 25ms. No per-chunk request →
//          ~8 KB/s, 39 KB in ~5s. Phone just listens + collects.
//   1.19 – Decompress downloaded runs in a background isolate (compute())
//          so the BLE platform channel is not blocked during multi-run
//          downloads. Fixes GATT_ERROR 133 / LINK_SUPERVISION_TIMEOUT
//          that occurs when decompression of run N blocks the main
//          thread before run N+1's setNotifyValue can be processed.
//   1.20 – FT ACKs disabled (pure listener mode) — S22 GATT 133 fix.
//          (Version constant not bumped at the time; noted retroactively.)
//   1.21 – Resume-capable FT download (FW ≥ 5.63): on timeout/disconnect/
//          FT error, re-request from received byte offset (CMD_START+offset,
//          auto-reconnect, backoff 2/4/6/8 s, ≤5 resumes). Fixes two latent
//          parser bugs: START size read from wrong offset, and chunk-index
//          byte stored inline (corrupted every 242 B block). Live services
//          getter (self-heal after reconnect); stream CRC32 verify via
//          device FINAL packet; fail-fast on link drop (no 90 s hang).
//   1.22 – Short-transfer guard: a "completed" transfer with missing bytes
//          throws and resumes (FW < 5.65 signalled abort with a bare 0x03
//          byte the parser mistook for FINAL — partial buffer looked valid,
//          run lost). Phase 2: ONE RUN PER CONNECTION — disconnect/reconnect
//          between runs (replaces 2 s cooldown); fresh GATT session per run
//          kills cumulative BLE buffer pressure by design.
//   1.23 – UI no longer nuked mid-download: the intentional Phase-2 per-run
//          disconnect tripped the unexpected-drop handler, clearing screen
//          state — runs WERE saved but nothing displayed until app relaunch.
//          Batch cancel via disconnect button; device list + service
//          refreshed after the batch.
//   1.24 – Multi-run batch hardening + observability. JP's 5-run bench showed
//          only 2/5 runs landed locally and 3 remained "to download" even
//          though the earlier 2-run bench looked clean. Log device/local/
//          missing run keys (#id@timestamp), retry inter-run reconnects
//          (3× with increasing settle), retry failed/CRC-bad runs on a fresh
//          GATT session, refresh the local index after every save, and show
//          failed run ids in the final snackbar.
//   1.25 – Native Android BluetoothGatt downloader (beta spike). FlutterBluePlus
//          keeps the existing UI path, but a new "Native Download Missing"
//          card hands the radio to a serialized Kotlin GATT engine modeled on
//          the old working SGC_06/BlueSTSDK architecture. Native downloads
//          only the currently-missing run IDs; Flutter then decompresses,
//          validates payload CRC, saves locally, and reconnects its BLE path.
//   1.26 – Native resume hardening from first SGC_NATIVE bench: S22 dropped the
//          link at 39,204/39,372 B (last 168 B + CRC lost). Fail fast on FT
//          link loss instead of waiting 20 s idle, fail connect attempts
//          immediately on Android GATT status, and wait for the device
//          advertisement before resume reconnect to avoid status 147 storms.
//   1.27 – nRF Connect control test completed the full 39,372 B stream on the
//          same S22 (FINAL CRC bytes matched device ft_done). Remove the native
//          downloader's extra requestConnectionPriority(BALANCED); leave link
//          parameters at Android default to match the known-good control path.
//   1.28 – nRF parity notifications. App 1.27 still wedged (chunk 114), and
//          nRF's good log showed subscriptions to every SGC notify char
//          (ABC4/ABC5/ABC7/ABC8/ABCD/ABD0 + Service Changed), not just ABCD.
//          Native now enables the same set; the FT parser consumes only ABCD.
//   1.29 – Connection-settle parity. App 1.28 still wedged (chunk 121); nRF's
//          manual flow gave Android several seconds to finish connection-
//          parameter churn after subscription before CMD_START. Native now logs
//          onConnectionUpdated and waits for a post-subscription update/settle
//          window before starting FT; avoids re-writing the ABCD CCCD.
//   1.30 – Build fix: this Flutter/Android SDK stub does not expose
//          BluetoothGattCallback.onConnectionUpdated to Kotlin, so 1.29 did
//          not compile. Use a fixed 4 s post-subscription settle delay instead.
//   1.31 – Replace the hand-rolled native BluetoothGatt engine with Nordic's
//          open-source Android BLE Library (BleManager 2.11.0, BSD-3-Clause).
//          nRF Connect itself is closed-source, but its hardened BLE queue is
//          the right reusable component. Same FT protocol, resume, batching,
//          full notify parity, and 4 s post-connect settle.
//   1.32 – Live native progress: sgc_native_ble_events channel forwards
//          Kotlin log lines + throttled FT byte progress (~250 ms) to the
//          UI, so a long connect/resume cycle no longer looks like
//          "pressed, nothing happened". Also: 1.31 build fixes (typed
//          ReadRequest.await(ReadResponse::class.java)).
//   1.33 – Connect resilience: one automatic retry on initial connect (the
//          S22 often fails the first connect transiently right after a
//          device reboot, leaving a half-open link the device still counts
//          as connected) + full exception text in the failure snackbar.
//   1.34 – Resume robustness: KEEP_SCREEN_ON + partial wake lock during
//          native batches (attempts 3–5 on 2026-09-02 died because the app
//          was backgrounded mid-resume and Android throttled BLE);
//          resume attempts 6 → 10; connect retry 3/250 → 5/500; 1 s settle
//          after advertisement sighting (device post-abort recovery window).
//   1.35 – Foreground service during native batches. Bench proof 2026-09-02:
//          nRF Connect downloaded a full 39 KB run on the same phone/FW/
//          params (CI 60 ms, 10 s supervision, 244 B chunks) with blocks:0
//          while our app wedged — and mcp's own earlier link ALSO died with
//          status=8, so the wedge is stochastic, but Samsung Freecess only
//          throttles OUR process. Foreground priority closes that gap.
//   1.36 – Incremental native saves (bench 2026-09-03: 4/5 runs completed
//          but none visible even after relaunch — 1.35 saved only after the
//          WHOLE batch returned, so a terminal last run lost all completed
//          ones). Now one native call per run, persisted immediately; per-run
//          try/catch so one failure can't abort the rest. LocalStorage.listAll
//          skips corrupt index entries instead of returning an empty list.
//   1.37 – Wedge-elimination A/B: post-connect settle 4 s → 12 s. JP's
//          directive: no interruptions, not fast recovery. nRF Connect has
//          NEVER wedged mid-download; its manual flow effectively settles
//          15-30 s (hand-subscribing 6 CCCDs + navigation) while our FT
//          starts ~5-8 s post-connect, inside Android's autonomous
//          connection-churn window (conn-param/PHY/data-length updates).
//          If 12 s eliminates wedges, latency will be engineered back down
//          (adaptive settle or single-connection batch).
//   1.43 – SINGLE-CONNECTION BATCH + INCREMENTAL STREAMED SAVES.
//          App 1.36-1.40 called downloadRuns() in a loop, disconnecting and
//          reconnecting for every single run, which caused GATT client leakage
//          (multiple active clientIfs) and S22 link collapse. Now Kotlin connects
//          once for the entire batch, downloads all runs, streams completed runs
//          via a new 'ft_run_complete' event, and disconnects once at the end!
//          This matches nRF Connect's single-connection profile perfectly while
//          preserving 100% incremental saving.

///          Settle 12s->4s. 2M PHY requested.

///   1.44 - V2 STREAMING PULL (FW 5.77): one command, binary stream,
///          NO RESUME. Settle 12s to 4s. 2M PHY requested.
///   1.60 - MERGE HARDENING x4 (2026-10-14 diagnosis, JP-agreed): the
///          "Merged Courses disappeared" transient. (1) loadMerges: no
///          silent catch — corrupt file quarantined as merged_runs.corrupt.json
///          + loud log. (2) saveMerges: atomic temp+rename. (3) merge pass
///          race guard: empty in-memory list reloaded from disk before
///          pairing. (4) monotonic save guard: shrinking saves rejected
///          (allowShrink reserved for explicit user deletion).
///   1.61 - F45 RUN NAMING: user-editable label per run (edit icon or
///          long-press a run → dialog, 40 chars, empty = default). Default
///          label = local date + daily ordinal ("2026-10-23 Run 2"); name
///          persisted in runs.json index, shown in list + detail header;
///          will ride along with cloud upload attribution later.
///   1.61.1 - CRASH FIX (JP bench, 2026-10-23): rename dialog Cancel crashed
///          the app — TextEditingController was disposed in the caller right
///          after pop while the dialog exit animation still held the
///          TextField attached (use-after-dispose). Dialog is now its own
///          StatefulWidget owning the controller (disposed only after the
///          route is fully gone); pop returns the text or null on cancel.
///   1.62 - F45 EXTENSION (JP 2026-10-23 12:55): merged courses renameable
///          too — same _RenameDialog, name persisted in merged_runs.json,
///          default stays "Run #a + #b" (moves to subtitle when named),
///          detail header follows the name. Same-count save passes the
///          monotonic shrink guard by design.
///   1.63 - ABC5 BATTERY % IN DEVICE CARD (snow kit): read on connect
///          (bit7 = charging, 0x7F = unknown → "— (unknown)"), shown as
///          "87%" / "87% · charging ⚡"; battery read failure never masks
///          the run list (own try). Cleared on every disconnect/teardown.
///   1.64 - DISCONNECT (JP 2026-10-23 13:16): manual Disconnect button on
///          the connected card (disabled mid-download) + auto-disconnect
///          after 1 min idle. Timer re-arms on connect-complete and after
///          every download batch; self-re-arms if it fires mid-transfer;
///          cancelled on disconnect/teardown/dispose.
///   1.65 - F21 + F20 (snow kit complete, JP "go with the rest" 14:53):
///          F21 device params editable over BLE — Device Name / Arm Side /
///          Discipline rows on the connected card (tap → dialog → write →
///          config re-read; name change re-advertises). F20 athlete profile
///          screen (name/club/group/age/category + push-to-cloud toggle,
///          SharedPreferences persistence) wired to Settings; Cloud Sync
///          switch bound to the same pref. SGCService gains writeArmSide +
///          writeDiscipline.
///   1.66 - JP BENCH FEEDBACK 2026-10-26 12:18: (1) Course setup Cancel —
///          AppBar close button mid-definition, warning + confirmation,
///          onSave(null) per contract. (2) Settings > BLE Devices was a
///          dead tile — new BleDevicesScreen (list paired auto-sync
///          targets, remove one / remove all, SharedPreferences-backed).
///          (3) F45 label revision: default run name = plain 'Run N'
///          (date stays visible on the tile); new date filter in the app
///          bar (popup, distinct local dates).
///   1.66.1 - JP BENCH 2026-10-26 15:13: merged courses follow the active
///          date filter (merge day = first source run's day via aFile
///          lookup; missing-source merges are never hidden). Header shows
///          "n of N" while filtered.
///   1.67 - P1 CLOUD INTEGRATION (F20/F23/F43/F48/F50 + arch §8) against
///          the deployed sgc-api (https://sgc-api.vytsolutions.com):
///          ApiClient (I07 bootstrap — app follows api_base, bearer attach,
///          single mutex'd refresh-on-401 retry, typed exceptions),
///          AuthStore (tokens in flutter_secure_storage, never
///          SharedPreferences), UploadQueue (durable upload_queue.json,
///          atomic temp+rename, uuid-v4 idempotency keys, exponential
///          backoff 1m→…→1h cap, POISONED only on 413/validation),
///          SyncWorker (single-flight; triggers: app start, connectivity
///          regained, every run save, manual; respects push_to_cloud),
///          upload payload built in an isolate from the SAME decode the
///          UI shows (GateTimeEstimator timestamps + 10 Hz baro/vertical
///          speed), Cloud Account screen (register athlete/master + login
///          + logout + F50 GDPR typed-confirmation hard delete → local
///          logout + queue wipe), Cloud Runs browser (paged GET /v1/runs,
///          lazy load-more) + read-only detail (timestamps list + baro
///          summary), Settings "Cloud Account"/"Cloud Runs" tiles (warn
///          when push on but signed out), profile age/category mirror on
///          save (name never PUT — it is the v1 login credential).
///   1.68 - MERGED COURSES TO THE CLOUD (JP-approved 2026-10-26 19:06:
///          "upload everything: singles AND merged, as distinct types").
///          UploadQueue entries gain run_type (single|merged) + a merged
///          lane: deterministic RFC-4122-SHAPED uuid v5-style from the
///          merge identity (namespace const + aId|bId|aFile|bFile,
///          hand-rolled FNV-1a cascade — no crypto dep) so the
///          idempotency key is stable across restarts/queue rebuilds;
///          SyncWorker backfills every persisted merge each pass,
///          processes singles BEFORE merged, and DEFERS a merged upload
///          (fixed 2-min backoff, no attempt inflation) until both
///          sources are DONE — sources permanently POISONED or deleted
///          upload without their lineage id. Merged payload = blended
///          confirmed gates (ms/side/estimated → timestamp_ms/arm_side/
///          guessed) + averaged 10 Hz baro (ms/alt → time_offset_ms/
///          altitude_m/speed_mps derivative), start = source-a header
///          datetime, run_number = aId, name = merge name, + new server
///          fields run_type="merged" and source_run_ids=[a,b].
///          Cloud Runs: Merged badge + All/Single/Merged filter
///          (client-side over loaded pages); Cloud detail: Type row,
///          "Merged from runs #a + #b" lineage, Merged app-bar badge.
///          Trigger: merge pass completion (new merges or baro
///          backfill) + the existing app-start pass (idempotent).///   1.69 - JP NAMING SCHEME v2 (confirmed 2026-10-27 10:18): stable
///          device-letter registry + computed run names + day pager.///          (1) DeviceRegistry (device_registry.json, atomic temp+rename):
///          every distinct BLE MAC gets a permanent letter — first ever
///          MAC = A, new MACs at pair/save time take the next free
///          letter (A..Z, rolls to AA.. if a 27th device ever appears);
///          re-pairing a known MAC keeps its letter. First-launch
///          migration seeds from sgc.paired_macs in stored order, so
///          today's single device lands on A.
///          (2) SavedRun gains sourceMac (stamped on all three download
///          paths; pre-1.69 rows keep null and resolve to letter A).
///          (3) Default run name = "<letter>#<device run id>" ("A#32"),
///          COMPUTED not stored; custom F45 names still override.
///          (4) Day pager replaces the date-filter popup: AppBar ◀ /
///          "Sun 26 Oct" / ▶ moves one day at a time (launch = newest
///          day with runs, arrows disabled at the ends); runs within the
///          day sorted (timestamp, R-before-L, id) and numbered 1..N in
///          a display-only circle avatar; merged courses stay scoped to
///          the same day.
///          (5) Merged default name = "<A#x> + <A#y>" from the two
///          sources' default names (dev case "A#32 + A#33"); custom
///          merge name overrides.
///          (6) Upload payloads ALWAYS carry 'name' (custom or computed
///          default — singles and merged alike); cloud list title = row
///          name (legacy-nameless rows fall back to "Single run" /
///          "Merged run"), replacing the cloud-id title logic.
///   1.70 - SNOW-READINESS PAIR (JP GO 2026-10-27 11:04): F45 rename
///          propagation to the cloud + F46 delete UI (phone+cloud legs).
///          (1) Rename outbox: pending cloud renames persist in
///          rename_queue.json (atomic temp+rename); enqueued on every
///          successful local rename (single AND merged) when the run/merge
///          is already uploaded (DONE queue entry with cloud id); a newer
///          rename supersedes a pending one. UploadQueue.markDone now
///          captures the server lock_version from the upload response.
///          (2) SyncWorker delivers due renames BEFORE the push-toggle
///          gate (renames are explicit user actions on already-uploaded
///          rows): PUT /v1/runs/{id} + If-Match <lock_version>; 409 → GET
///          fresh → retry ONCE → still 409 = rename loses, entry dropped
///          + surfaced ("rename conflict — cloud kept its name"); 404
///          (row deleted) drops silently; legacy pre-1.70 entries GET the
///          lock_version first; 401 stops the pass, 403/network/5xx retry
///          with the shared exponential backoff; validation/other 4xx
///          drop + surface. Identity-based removal so a rename queued
///          mid-flight survives an older entry's completion; retry timer
///          arms from the rename schedule too. New ApiClient.renameRun
///          (PUT + If-Match header, contract per sgc_api runs.py: 428
///          missing / 409 mismatch / hit bumps lock_version).
///          (3) F46 delete: long-press a run tile or merged tile → menu
///          (rename/delete) → confirmation with honest scoping —
///          "Delete from phone" + "Delete from cloud" (default-checked
///          only when uploaded; disabled + explained when not); device
///          flash copy explicitly NOT deleted (recycles automatically;
///          per-run device deletion comes with a firmware update);
///          deleting a merge source keeps merged courses' data.
///          Execution order per arch §8: cloud DELETE first (failure →
///          abort + snackbar, everything kept), then local: runs.json +
///          run file + queue entry + rename outbox entries; merges leave
///          merged_runs.json via allowShrink: true (reserved path used)
///          + queue/outbox cleanup so the merged backfill cannot
///          resurrect them. Deleting from the cloud only keeps the local
///          copy and its DONE queue entry (no re-upload resurrection).
///          (4) Counter labels clarified on the connected card:
///          "On device (flash slots)" (ABC run count) vs "Downloaded
///          (phone)" (local index); download-tile subtitle now reads
///          "On device: N · Downloaded: N · Not downloaded: N".
const String APP_VERSION = '1.70.0';

///   1.48 - SETTLE 4 s -> 0.5 s: idle watchdog makes bad starts self-correcting.

///   1.47 - STREAM WATCHDOGS (2026-09-29, JP): 2 s IDLE timeout (reset per
///          frame; a flowing stream never fires) + dynamic OVERALL deadline
///          (bytes / 5 KB/s floor x 1.3 margin). Kills the fixed 10 s wall
///          clock that made any run slower than 10 s total loop forever
///          (root cause of the 90 KB "infinite download loop", 2026-09-28).

///   1.46 - SLOW-MODE GUARD: re-assert CONNECTION_PRIORITY_HIGH on any conn-param
///          update >30 ms during download + before every console request.
///          S2x system churn (battery/Bose probes) leaves CI at 60 ms = 13 fps.
