/**
 * @file    ring_buffer.h
 * @brief   Data structures: 16-byte RawFrame, 22-byte CompressedFrame.
 *          Ring buffer implementation moved to flash_ring.h (on MX25R1635F).
 *          Matches sgc_architecture_devices.md §2.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* Newest frames kept at start (phone/run). Capacity is FlashRing TOTAL_SLOTS=3000. */
static constexpr size_t RING_SIZE = 1000;  /* PREROLL_KEEP */

struct __attribute__((packed)) RawFrame {
    int16_t  q_w, q_x, q_y, q_z;   /* quaternion, Q30 fixed-point — 8 bytes */
    int16_t  la_x, la_y, la_z;     /* linear acceleration, mm/s² — 6 bytes  */
    uint16_t baro_pa_div2;         /* pressure / 2 — 2 bytes, 2 Pa/LSB     */
};

struct __attribute__((packed)) CompressedFrame {
    uint32_t ts_ms;                /* absolute timestamp */
    int16_t  q_delta[4];           /* quaternion delta from previous */
    int16_t  la_x, la_y, la_z;     /* linear acceleration */
    uint16_t baro_pa_div2;         /* pressure / 2, 2 Pa/LSB */
    uint8_t  _pad[2];              /* align → 22 bytes total */
};
