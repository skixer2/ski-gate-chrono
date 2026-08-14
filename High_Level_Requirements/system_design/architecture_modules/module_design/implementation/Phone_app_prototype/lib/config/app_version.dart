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
const String APP_VERSION = '1.11';
