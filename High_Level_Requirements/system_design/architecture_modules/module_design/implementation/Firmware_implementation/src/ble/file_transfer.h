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
 * V4.96: BHI260AP (CS p31) and MX25R flash (CS p26) share SPI bus p3/p4/p5 via
 *   SEPARATE mbed::SPI objects → no shared lock. During a BLE download they
 *   alternate (BHY2.update every loop vs read_run_data per chunk) and race the
 *   bus → SPI wedges → loop() hangs → LINK_SUPERVISION_TIMEOUT. Fix: suspend
 *   BHY2 while sgc_ble_ft_active() (see main.cpp).
 */

#pragma once

#include <stdint.h>

void sgc_ble_transfer_init();
void sgc_ble_transfer_poll();
bool sgc_ble_ft_active();   /* true while streaming a run over BLE */
