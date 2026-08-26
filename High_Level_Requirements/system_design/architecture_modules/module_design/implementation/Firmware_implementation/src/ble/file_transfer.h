/**
 * @file    file_transfer.h
 * @brief   BLE file transfer protocol — device-push streaming (V5.45).
 *
 * Protocol V3 (5.45): device-push, phone listens.
 *   Phone reads Run List (ABC9) → JSON array of {id,ts,size,side}
 *   Phone writes CMD_START to FT Request (ABCA): [0, runId_lo, runId_hi]
 *   Device opens run, notifies FT Status (ABCD) = 1 (FT_STREAMING)
 *   Device pushes chunks via FT Chunk (ABCB, 244 B) @ 25ms cadence
 *   Phone collects notifications until FT_DONE
 *   Device notifies ABCD = 2 (FT_DONE), writes CRC to FT CRC (ABCC)
 *   Phone reads FT CRC (ABCC), verifies against local CRC32
 *   Phone can abort with CMD_ABORT: [2]
 *
 * V4.96/4.97 SPI isolation: while FT active, skip BHY2.update(),
 *   feed_sensors(), and IDLE ambient (see main.cpp).
 */

#pragma once

#include <stdint.h>
#include <ArduinoBLE.h>

void sgc_ble_transfer_init();
void sgc_ble_transfer_poll();
bool sgc_ble_ft_active();   /* true while FT_STREAMING */
/** Abort in-flight FT (disconnect / stall). reason for serial JSON. */
void sgc_ble_ft_abort(const char* reason);
/** Handle phone write request (CMD_START / CMD_ABORT). */
void sgc_ble_ft_on_request(const uint8_t* data, int len);
void sgc_ble_ft_handle_ack();

extern "C" {
    BLECharacteristic* sgc_ble_ft_request_char();
    BLECharacteristic* sgc_ble_ft_stream_char();
}
