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
const String APP_VERSION = '1.16';
