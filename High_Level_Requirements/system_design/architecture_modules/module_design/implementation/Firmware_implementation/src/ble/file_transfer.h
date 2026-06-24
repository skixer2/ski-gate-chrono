/**
 * @file    file_transfer.h
 * @brief   BLE file transfer protocol — run list enumeration + chunked download.
 *
 * Protocol:
 *   Phone reads Run List (ABC9) → JSON array of {id,ts,size,side}
 *   Phone writes run_id to FT Request (ABCA)
 *   Device reads run from flash, streams 244-byte chunks via FT Chunk (ABCB)
 *   Device sets FT Status (ABCD) = 2 (complete), writes CRC to FT CRC (ABCC)
 *   Phone reads FT CRC, verifies against local CRC32
 */

#pragma once

#include <stdint.h>

void sgc_ble_transfer_init();
void sgc_ble_transfer_poll();
