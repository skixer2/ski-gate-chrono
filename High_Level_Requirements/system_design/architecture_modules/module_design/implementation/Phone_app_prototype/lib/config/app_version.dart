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
const String APP_VERSION = '1.2';
