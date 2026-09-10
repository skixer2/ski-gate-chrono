/**
 * @file    pull_transfer.h
 * @brief   Phone-pull transfer — "serial replacement" console (FW 5.76).
 *
 * PULL_TRANSFER_REDESIGN.md (2026-09-10, JP-approved): the phone drives
 * every byte. The device answers ASCII requests on the console
 * characteristic (ABCA, write+notify) with text frames. The same handler
 * serves USB serial (FWR-1 / SR-5).
 *
 * Commands (one ASCII line per request):
 *   "D"                  → run directory: one JSON line per run, then
 *                          {"d_end":<n>}  (sz = total on-disk blob size:
 *                          16B header + payload + 6B CRC trailer)
 *   "r <id> <off> <len>" → framed hex chunk: "[<seq> <n> <HEX>]" frames
 *                          (paced PULL_FRAME_GAP_MS), then "OK <total>"
 *                          or "E <code>" (1=bad run, 2=bad off/len)
 *
 * Stateless: every request carries (id, offset, len) — no session state,
 * no resume protocol. A disconnect at any moment costs one reconnect and
 * the next request continues where the last one stopped (SR-2).
 *
 * Frames: payload ≤ PULL_FRAME_PAYLOAD bytes, hex-doubled on air
 * (234 chars + header ≤ 244 usable at ATT MTU 247). A logical
 * PULL_CHUNK_BYTES request therefore spans several notifications (IR-3).
 *
 * Watchdog (FWR-3): armed on the FIRST console request after connect
 * (not at connect — S22 service discovery can exceed 2 s), reset on any
 * request; PULL_WDT_MS of silence while connected → {"ev":"pull_wdt"} →
 * disconnect → re-ADV. Stateless design makes false positives cheap.
 *
 * The legacy device-push FT engine stays in the tree but is disabled
 * (FWR-5, sgc_push_ft_enabled()).
 */

#pragma once
#include <stdint.h>

void sgc_pull_init();
/** Parse+execute one request line. via_ble=true → replies on console char
 *  notify; false → replies on USB Serial (same text either way). */
void sgc_pull_handle_line(const char* line, bool via_ble);
/** Main-loop poll: paced frame TX + request watchdog. Never blocks. */
void sgc_pull_poll();
/** Feed the watchdog: call on ANY console characteristic write. */
void sgc_pull_touch();
/** Called on BLE connect/disconnect — clears session + watchdog state. */
void sgc_pull_link_reset();
