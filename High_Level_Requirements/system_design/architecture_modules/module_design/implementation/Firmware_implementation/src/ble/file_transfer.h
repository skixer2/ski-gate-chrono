/**
 * @file    file_transfer.h
 * @brief   BLE file transfer protocol V2 — request-response (phone pulls chunks).
 *
 * Protocol V2 (5.14):
 *   Phone reads Run List (ABC9) → JSON array of {id,ts,size,side}
 *   Phone writes CMD_START to FT Request (ABCA): [0, runId_lo, runId_hi]
 *   Device opens run, notifies FT Status (ABCD) = 1 (FT_READY)
 *   Phone writes CMD_CHUNK to ABCA: [1, off0, off1, off2, off3] (u32 LE)
 *   Device reads flash at offset, notifies chunk via FT Chunk (ABCB, up to 244 B)
 *   Phone repeats CMD_CHUNK until last chunk received
 *   Device notifies ABCD = 2 (FT_DONE) after last chunk, writes CRC to FT CRC (ABCC)
 *   Phone reads FT CRC (ABCC), verifies against local CRC32
 *   Phone can abort with CMD_ABORT: [2]
 *
 * V4.96/4.97: BHI260AP (CS p31) and MX25R flash (CS p26) share SPI bus p3/p4/p5
 *   via SEPARATE mbed::SPI objects → no shared lock. During BLE download they
 *   race → SPI wedges → LINK_SUPERVISION_TIMEOUT. Fix: while sgc_ble_ft_active(),
 *   skip BHY2.update(), feed_sensors(), and IDLE ambient (see main.cpp).
 */

#pragma once

#include <stdint.h>

void sgc_ble_transfer_init();
void sgc_ble_transfer_poll();
bool sgc_ble_ft_active();   /* true while FT_READY (phone pull in progress) */
/** Abort in-flight FT (disconnect / SLEEP / stall). reason for serial JSON. */
void sgc_ble_ft_abort(const char* reason);
/** V2: handle phone write request (CMD_START / CMD_CHUNK / CMD_ABORT). */
void sgc_ble_ft_on_request(const uint8_t* data, int len);
