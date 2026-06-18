/**
 * @file    file_transfer.h
 * @brief   BLE file transfer protocol — stub (Phase 8).
 *
 * Protocol (planned):
 *   Phone reads Run List characteristic → selects run →
 *   phone writes CMD_START(run_id) → device streams chunks
 *   (244 bytes each via BLE notification) → CRC32 at end.
 */

#pragma once

#include <stdint.h>

/** @brief Initialize file transfer resources (call once in setup). */
void sgc_ble_transfer_init();

/** @brief Check for incoming transfer commands (call in loop). */
void sgc_ble_transfer_poll();
