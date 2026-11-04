/*
 * ring.c — pre-roll ring implementation (spec v4-final)
 *
 * Simple and safe: ONE 20 B flash_write per push, ONE 20 B flash_read
 * per pop — no RAM page staging (the v0.16.0 page-buffer design broke
 * on 20 B / 256 B page misalignment: slots drift across page
 * boundaries, 12.8 per page — a correct staging buffer needs
 * split-slot handling; deleted instead). Per-push cost ~0.4–0.8 ms
 * (max 3 ms), one per tick — proven affordable by the B4 step-0 sim.
 * Each flash byte is programmed at most once per ARM→run cycle.
 */

#include "ring.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>

/* The store owns the flash device; borrow it (single SPI NOR). */
extern const struct device *store_flash_device(void);

static struct {
	uint16_t head;        /* next slot to pop   (0..4095) */
	uint16_t tail;        /* next slot to push  (0..4095) */
	bool     overflow;    /* tail hit RING_SLOTS: spec violation */
} rg;

void ring_init(void)
{
	memset(&rg, 0, sizeof(rg));
	ring_erase_banks();
}

void ring_arm_reset(void)
{
	memset(&rg, 0, sizeof(rg));
}

bool ring_push(const struct sgc_raw_frame *f, uint32_t ts_ms)
{
	if (rg.tail >= RING_SLOTS) {
		rg.overflow = true;
		return false;   /* recorder flags as skipped */
	}

	uint8_t slot[RING_SLOT_BYTES];

	memcpy(slot, f, sizeof(*f));
	slot[16] = (uint8_t)(ts_ms);
	slot[17] = (uint8_t)(ts_ms >> 8);
	slot[18] = (uint8_t)(ts_ms >> 16);
	slot[19] = (uint8_t)(ts_ms >> 24);

	flash_write(store_flash_device(),
		    RING_BASE + (uint32_t)rg.tail * RING_SLOT_BYTES,
		    slot, RING_SLOT_BYTES);
	rg.tail++;
	return true;
}

bool ring_pop_oldest(struct sgc_raw_frame *f, uint32_t *ts_ms)
{
	if (rg.head >= rg.tail) {
		return false;
	}

	uint8_t slot[RING_SLOT_BYTES];

	flash_read(store_flash_device(),
		   RING_BASE + (uint32_t)rg.head * RING_SLOT_BYTES,
		   slot, RING_SLOT_BYTES);
	memcpy(f, slot, sizeof(*f));
	*ts_ms = (uint32_t)slot[16] | ((uint32_t)slot[17] << 8) |
		 ((uint32_t)slot[18] << 16) | ((uint32_t)slot[19] << 24);
	rg.head++;
	return true;
}

uint32_t ring_count(void)
{
	return (uint32_t)rg.tail - (uint32_t)rg.head;
}

void ring_erase_banks(void)
{
	for (uint32_t a = RING_BASE; a < RING_BASE + RING_BYTES; a += 4096u) {
		flash_erase(store_flash_device(), a, 4096u);
	}
}
