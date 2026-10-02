/**
 * @file    bitpacker.c
 * @brief   Adaptive bit-packing — C port of ArduinoFW bit_packer.cpp.
 *
 * Faithful port: same classification ranges, same clamps, same header
 * layout, same seq timing (seq = frame_count at encode time, pre-increment).
 * Differential-tested byte-exact against the original C++ on host.
 */

#include "bitpacker.h"
#include <string.h>

void sgc_bitpacker_reset(struct sgc_bitpacker *bp)
{
	memset(&bp->last, 0, sizeof(bp->last));
	memset(bp->buf, 0, sizeof(bp->buf));
	bp->frame_count = 0;
	bp->last_ms = 0;
	bp->last_size = 0;
	bp->last_type = SGC_PKT_T3;
}

static uint8_t classify(const int32_t *deltas, int32_t baro_delta)
{
	int all4 = 1, all8 = 1;

	for (int i = 0; i < 7; i++) {
		if (deltas[i] < -8 || deltas[i] > 7) {
			all4 = 0;
		}
		if (deltas[i] < -128 || deltas[i] > 127) {
			all8 = 0;
		}
	}
	if (baro_delta < -8 || baro_delta > 7) {
		all4 = 0;
	}
	if (baro_delta < -128 || baro_delta > 127) {
		all8 = 0;
	}
	if (all4) {
		return SGC_PKT_T1;
	}
	if (all8) {
		return SGC_PKT_T2;
	}
	return SGC_PKT_T3;
}

static void write_header(struct sgc_bitpacker *bp, uint32_t delta_ms, uint8_t type)
{
	uint8_t seq = (uint8_t)(bp->frame_count & 0x0Fu);
	uint16_t hdr = (uint16_t)(((uint16_t)type << 14) | ((uint16_t)seq << 10) |
				  ((uint16_t)(delta_ms & 0x3FFu)));
	bp->buf[0] = (uint8_t)(hdr & 0xFFu);
	bp->buf[1] = (uint8_t)(hdr >> 8);
}

static uint8_t clamp4(int32_t v)
{
	if (v < -8) {
		v = -8;
	}
	if (v > 7) {
		v = 7;
	}
	return (uint8_t)((uint32_t)v & 0x0Fu);
}

static uint8_t clamp8(int32_t v)
{
	if (v < -128) {
		v = -128;
	}
	if (v > 127) {
		v = 127;
	}
	return (uint8_t)((uint32_t)v & 0xFFu);
}

static void write_t1_payload(struct sgc_bitpacker *bp, const int32_t *deltas, int32_t baro_delta)
{
	uint8_t n[8];

	for (int i = 0; i < 7; i++) {
		n[i] = clamp4(deltas[i]);
	}
	n[7] = clamp4(baro_delta);
	bp->buf[2] = (uint8_t)((n[0] << 4) | n[1]);
	bp->buf[3] = (uint8_t)((n[2] << 4) | n[3]);
	bp->buf[4] = (uint8_t)((n[4] << 4) | n[5]);
	bp->buf[5] = (uint8_t)((n[6] << 4) | n[7]);
}

static void write_t2_payload(struct sgc_bitpacker *bp, const int32_t *deltas, int32_t baro_delta)
{
	for (int i = 0; i < 7; i++) {
		bp->buf[2 + i] = clamp8(deltas[i]);
	}
	bp->buf[9] = clamp8(baro_delta);
}

static void put16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_t3_payload(struct sgc_bitpacker *bp, const struct sgc_raw_frame *cur)
{
	put16le(&bp->buf[2], (uint16_t)cur->q_w);
	put16le(&bp->buf[4], (uint16_t)cur->q_x);
	put16le(&bp->buf[6], (uint16_t)cur->q_y);
	put16le(&bp->buf[8], (uint16_t)cur->q_z);
	put16le(&bp->buf[10], (uint16_t)cur->la_x);
	put16le(&bp->buf[12], (uint16_t)cur->la_y);
	put16le(&bp->buf[14], (uint16_t)cur->la_z);
	put16le(&bp->buf[16], cur->baro_pa_div2);
}

uint8_t sgc_bitpacker_encode(struct sgc_bitpacker *bp,
			     const struct sgc_raw_frame *cur, uint32_t ts_ms)
{
	int32_t deltas[7] = { 0 };

	if (bp->frame_count == 0) {
		bp->last = *cur;
		bp->last_ms = ts_ms;
		bp->frame_count = 1;
		write_header(bp, 0, SGC_PKT_T3);
		write_t3_payload(bp, cur);
		bp->last_size = 18;
		bp->last_type = SGC_PKT_T3;
		return 18;
	}

	deltas[0] = (int32_t)cur->q_w - (int32_t)bp->last.q_w;
	deltas[1] = (int32_t)cur->q_x - (int32_t)bp->last.q_x;
	deltas[2] = (int32_t)cur->q_y - (int32_t)bp->last.q_y;
	deltas[3] = (int32_t)cur->q_z - (int32_t)bp->last.q_z;
	deltas[4] = (int32_t)cur->la_x - (int32_t)bp->last.la_x;
	deltas[5] = (int32_t)cur->la_y - (int32_t)bp->last.la_y;
	deltas[6] = (int32_t)cur->la_z - (int32_t)bp->last.la_z;

	int32_t baro_delta = (int32_t)cur->baro_pa_div2 - (int32_t)bp->last.baro_pa_div2;

	uint8_t type = classify(deltas, baro_delta);

	if ((bp->frame_count % SGC_BITPACK_ANCHOR_EVERY) == 0) {
		type = SGC_PKT_T3;
	}

	uint32_t delta_ms = ts_ms - bp->last_ms;
	if (delta_ms > 1023) {
		type = SGC_PKT_T3;
	}

	write_header(bp, (delta_ms > 1023) ? 0 : delta_ms, type);

	switch (type) {
	case SGC_PKT_T1:
		write_t1_payload(bp, deltas, baro_delta);
		bp->last_size = 6;
		break;
	case SGC_PKT_T2:
		write_t2_payload(bp, deltas, baro_delta);
		bp->last_size = 10;
		break;
	default:
		write_t3_payload(bp, cur);
		bp->last_size = 18;
		break;
	}

	bp->last_type = type;
	bp->last = *cur;
	bp->last_ms = ts_ms;
	bp->frame_count++;

	return bp->last_size;
}
