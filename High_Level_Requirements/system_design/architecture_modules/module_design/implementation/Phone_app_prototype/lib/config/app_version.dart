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
const String APP_VERSION = '1.33';
