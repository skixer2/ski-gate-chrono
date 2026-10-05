/*
 * ring.c — pre-roll ring implementation (spec v4-final)
 *
 * Program-only forward fill: each flash byte is programmed at most once
 * per ARM→run cycle (page-buffered 256 B writes, partial-page legal on
 * NOR — bits only ever go 1→0). No erase except ring_erase_banks().
 * All cursor state lives in RAM (the region fit is exact — no in-flash
 * metadata space, by design).
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
	bool     full;        /* tail wrapped past end: spec violation */
	/* Page staging: bytes of the current 256 B flash page. Flush on
	 * page boundary crossing; each byte programmed exactly once. */
	uint8_t  page[256];
	uint16_t page_fill;   /* bytes staged (byte address = tail_bytes) */
	uint32_t flushed_to;  /* flash byte offset already programmed */
} rg;

static void flush_page(void)
{
	const struct device *dev = store_flash_device();
	uint32_t addr = RING_BASE + rg.flushed_to;

	if (rg.page_fill == 0) {
		return;
	}
	/* Partial page: program exactly the staged prefix. */
	flash_write(dev, addr, rg.page, rg.page_fill);
	rg.flushed_to += rg.page_fill;
	rg.page_fill = 0;
}

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
		rg.full = true;
		return false;   /* spec violation — recorder flags it */
	}

	uint8_t slot[RING_SLOT_BYTES];

	memcpy(slot, f, sizeof(*f));          /* 16 B, packed struct */
	slot[16] = (uint8_t)(ts_ms);
	slot[17] = (uint8_t)(ts_ms >> 8);
	slot[18] = (uint8_t)(ts_ms >> 16);
	slot[19] = (uint8_t)(ts_ms >> 24);

	/* Stage into the page buffer; flush when the slot would cross
	 * the 256 B page boundary. */
	uint32_t byte_off = (uint32_t)rg.tail * RING_SLOT_BYTES;
	uint32_t page_off = byte_off & 0xFFu;

	if (page_off + RING_SLOT_BYTES > 256u) {
		flush_page();
		page_off = 0;
	}
	memcpy(&rg.page[page_off], slot, RING_SLOT_BYTES);
	rg.page_fill = page_off + RING_SLOT_BYTES;
	rg.tail++;

	/* Last slot of the region: flush the remainder. */
	if (rg.tail == RING_SLOTS) {
		flush_page();
	}
	return true;
}

bool ring_pop_oldest(struct sgc_raw_frame *f, uint32_t *ts_ms)
{
	const struct device *dev = store_flash_device();

	if (rg.head >= rg.tail) {
		return false;
	}

	/* Not-yet-flushed bytes live in the page buffer. */
	uint32_t byte_off = (uint32_t)rg.head * RING_SLOT_BYTES;
	uint8_t slot[RING_SLOT_BYTES];

	if (byte_off < rg.flushed_to) {
		flash_read(dev, RING_BASE + byte_off, slot, RING_SLOT_BYTES);
	} else {
		uint32_t page_off = byte_off & 0xFFu;

		memcpy(slot, &rg.page[page_off], RING_SLOT_BYTES);
	}

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
	const struct device *dev = store_flash_device();

	for (uint32_t a = RING_BASE; a < RING_BASE + RING_BYTES; a += 4096u) {
		flash_erase(dev, a, 4096u);
	}
}
