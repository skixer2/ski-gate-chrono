/*
 * store_fake.c — RAM-synthesized run store (step 3a/3b heritage)
 * ===============================================================
 * Two fake runs; payload bytes are GENERATED (byte at payload offset o
 * = o & 0xFF — eyeball-friendly in nRF Connect), never stored: 64 KB
 * RAM has no room for real blobs. The generator is deterministic, so
 * hashing it once at boot yields the trailer CRC for free.
 *
 * run 0: 2048 B payload (sz 2070)
 * run 1: 39350 B payload (sz 39372 — the classic bench-run size)
 *
 * 3d upgrade path: implement store.h against SPI flash, swap the
 * CMake entry. This file dies with honors.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include "store.h"

#define N_FAKE_RUNS 2

/* RunHeader (16 B packed) — littlefs_storage.h verbatim. */
struct __attribute__((packed)) fake_hdr {
	uint8_t  format_ver;     /* 2 = append-only era */
	uint8_t  arm_side;       /* 0=left, 1=right */
	uint32_t ts_utc;
	int16_t  baro_temp;      /* tenths of °C */
	uint32_t data_size;      /* 0 on-disk for v2 */
	uint16_t frame_count;    /* 0 on-disk for v2 */
	uint8_t  cal_accuracy;   /* BHI260AP fusion 0-3 */
	uint8_t  _pad;           /* align → 16 */
};

struct fake_run {
	struct fake_hdr hdr;
	uint32_t payload_len;
	uint32_t crc;            /* payload only, precomputed at boot */
};

static struct fake_run fake_runs[N_FAKE_RUNS] = {
	{ .hdr = { .format_ver = 2, .arm_side = 0, .ts_utc = 1758500000,
		   .baro_temp = 125, .data_size = 0, .frame_count = 0,
		   .cal_accuracy = 3, ._pad = 0 },
	  .payload_len = 2048 },
	{ .hdr = { .format_ver = 2, .arm_side = 1, .ts_utc = 1758510000,
		   .baro_temp = 87, .data_size = 0, .frame_count = 0,
		   .cal_accuracy = 2, ._pad = 0 },
	  .payload_len = 39350 },
};

uint8_t store_run_count(void)
{
	return N_FAKE_RUNS;
}

uint32_t store_run_payload_len(uint8_t id)
{
	return fake_runs[id].payload_len;
}

uint32_t store_run_ts(uint8_t id)
{
	return fake_runs[id].hdr.ts_utc;
}

uint8_t store_run_side(uint8_t id)
{
	return fake_runs[id].hdr.arm_side;
}

uint32_t store_run_total(uint8_t id)
{
	return sizeof(fake_runs[id].hdr) + fake_runs[id].payload_len + 6;
}

/* Standard reflected CRC-32, bitwise (same polynomial as the RawRunStore
 * table) — small enough to need no table in our RAM budget. */
static uint32_t crc32_byte(uint32_t crc, uint8_t b)
{
	crc ^= b;
	for (uint8_t i = 0; i < 8; i++) {
		crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return crc;
}

/* Synthesize blob bytes: header fields, ramp payload, or trailer. */
void store_read_blob(uint8_t id, uint32_t off, uint8_t *dst, uint32_t n)
{
	const struct fake_run *r = &fake_runs[id];
	uint32_t hdr_len = sizeof(r->hdr);
	uint32_t payload_len = r->payload_len;

	for (uint32_t i = 0; i < n; i++, off++) {
		if (off < hdr_len) {
			dst[i] = ((const uint8_t *)&r->hdr)[off];
		} else if (off < hdr_len + payload_len) {
			dst[i] = (uint8_t)(off - hdr_len);   /* ramp */
		} else {
			uint32_t t = off - hdr_len - payload_len;

			if (t == 0)      dst[i] = 0xC3;          /* magic hi */
			else if (t == 1) dst[i] = 0x32;          /* magic lo */
			else              dst[i] = (uint8_t)(r->crc >> (8 * (t - 2))); /* LE */
		}
	}
}

void store_init(void)
{
	/* Precompute payload CRCs (ArduinoFW-exact semantics). */
	for (uint8_t i = 0; i < N_FAKE_RUNS; i++) {
		uint32_t crc = 0xFFFFFFFFu;

		for (uint32_t o = 0; o < fake_runs[i].payload_len; o++) {
			crc = crc32_byte(crc, (uint8_t)o);
		}
		fake_runs[i].crc = crc ^ 0xFFFFFFFFu;
		printk("run %u: payload=%lu sz=%lu crc=0x%08lx\n", i,
		       (unsigned long)fake_runs[i].payload_len,
		       (unsigned long)store_run_total(i),
		       (unsigned long)fake_runs[i].crc);
	}
}
