/**
 * @file    bitpacker.h
 * @brief   Adaptive bit-packing — C port of ArduinoFW bit_packer.cpp (v4.90).
 *
 * Frame layout (byte-identical to ArduinoFW, golden-tested 2026-10-02):
 *   Header (2 B): bits 15-14 = type, 13-10 = seq (4b), 9-0 = delta ms (10b)
 *   T1 (6 B): 8 x int4 deltas   (7 IMU + baro)  — coasting
 *   T2 (10 B): 8 x int8 deltas  (7 IMU + baro)  — turning
 *   T3 (18 B): 7 x int16 full IMU + uint16 baro — anchor (first frame,
 *              every ANCHOR_EVERY frames, or delta_ms > 1023)
 *
 * Baro carried as baro_pa_div2 (2 Pa/LSB, uint16).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Mirrors ArduinoFW RawFrame (packed, 16 B). */
struct sgc_raw_frame {
    int16_t  q_w, q_x, q_y, q_z;   /* quaternion Q14 */
    int16_t  la_x, la_y, la_z;     /* linear accel (LACC raw) */
    uint16_t baro_pa_div2;         /* pressure / 2 */
};

enum sgc_pkt_type {
    SGC_PKT_T1 = 0,
    SGC_PKT_T2 = 1,
    SGC_PKT_T3 = 2,
};

struct sgc_bitpacker {
    struct sgc_raw_frame last;
    uint16_t frame_count;
    uint32_t last_ms;
    uint8_t  buf[18];
    uint8_t  last_size;
    uint8_t  last_type;
};

void   sgc_bitpacker_reset(struct sgc_bitpacker *bp);
/* Encode one frame; returns packed size (6/10/18). Data in bp->buf. */
uint8_t sgc_bitpacker_encode(struct sgc_bitpacker *bp,
			     const struct sgc_raw_frame *cur, uint32_t ts_ms);

#define SGC_BITPACK_ANCHOR_EVERY 50u   /* forced T3 every 50 frames @100 Hz */

#ifdef __cplusplus
}
#endif
