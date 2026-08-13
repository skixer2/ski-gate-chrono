/**
 * @file    file_transfer.h
 * @brief   BLE file transfer protocol — run list enumeration + chunked download.
 *
 * Protocol:
 *   Phone reads Run List (ABC9) → JSON array of {id,ts,size,side}
 *   Phone writes run_id to FT Request (ABCA)
 *   Device reads run from flash, streams chunks via FT Chunk (ABCB).
 *   Device sets FT Status (ABCD) = 2 (complete), writes CRC to FT CRC (ABCC)
 *   Phone reads FT CRC, verifies against local CRC32
 *
 * V4.96/4.97: BHI260AP (CS p31) and MX25R flash (CS p26) share SPI bus p3/p4/p5
 *   via SEPARATE mbed::SPI objects → no shared lock. During BLE download they
 *   race → SPI wedges → LINK_SUPERVISION_TIMEOUT. Fix: while sgc_ble_ft_active(),
 *   skip BHY2.update(), feed_sensors(), and IDLE ambient (see main.cpp).
 * V4.93/4.95: 20 B @ 25 ms + BLE.poll after notify + ft_prog (keep; not the root hang).
 */

#pragma once

#include <stdint.h>

void sgc_ble_transfer_init();
void sgc_ble_transfer_poll();
bool sgc_ble_ft_active();   /* true while streaming a run over BLE */
/** Abort in-flight FT (disconnect / SLEEP / stall). reason for serial JSON. */
void sgc_ble_ft_abort(const char* reason);
