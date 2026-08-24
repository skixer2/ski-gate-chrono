# TC-2026-08-15-001 — BLE FT download stall + `ble_radio` regression (FW 5.14)

**Status:** PASS (FW 5.16)  
**Closed:** 2026-08-18  
**Parent:** TC-2026-08-14-002 (BLE zombie PASS 5.03)

## Summary

FT download stalled after ~5 chunks when flashing via warm reset (PlatformIO).
Hard button reboot (true POR) → same FW → FT succeeds cleanly.

Root cause: nRF52 warm reset preserves Cordio BLE statics → BLE.begin() doesn't
fully reinitialize → supervision timeout at chunk 5.

`ble_radio` restart returned ok=0 in both stall and zombie scenarios — regression
from 5.10.

## Fix

- **T-008a (5.15):** `BLE.end()` before `BLE.begin()` on warm boot (`ble_warm_deinit`)
- **T-008b (5.15):** zombie path: `ble_radio ok:1 retry:0`
- **T-008c (5.16):** NVIC_SystemReset fallback for active-traffic stalls — self-recovery confirmed

## Evidence

- FW 5.14 POR: FT 38780 B, 159 chunks, CRC 0x21A5B233 ✅
- FW 5.16 after recovery: FT 38434 B ✅
- `ble_warm_deinit rr:6` → `init ble ok:1` on every warm boot

## Archived from

TEST_LEDGER.md §2 (active session), 2026-08-15 through 2026-08-18.
