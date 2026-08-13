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
const String APP_VERSION = '1.8';
