/*
 * store_flash.c — SPI NOR run store (step 4a)
 * =============================================
 * The real store.h implementation, over the on-board MX25R1635F (stock
 * Nicla, 2 MB — the custom carrier's 8 MB part is future hardware).
 *
 * Flash map (Opt-A, classic 2 MB geometry — flash_layout.h VERBATIM):
 *   0x00000–0x13FFF  pre-roll (FlashRing era — untouched here, 4b+)
 *   0x14000–0x1FBFFF 8 run slots × 0x3D000 (244 KiB)
 *   0x1FC000         config (not used yet)
 *   0x1FD000         run index, RRS2 format
 *   0x1FE000+        self-test / reserved
 *
 * Index (raw_run_store.cpp VERBATIM): [IndexHeader 16 B]["RRS2" magic]
 * then per run: [RunEntry 32 B][slot byte] = 33 B records.
 * Run blob in slot: [RunHeader 16 B][payload][C3 32 + CRC32 LE] —
 * CRC over payload only, init/final-xor 0xFFFFFFFF.
 *
 * 4a SCOPE: READ path = production (index + slots). WRITE = synthetic
 * test writer, fires ONLY when the index is absent/invalid (first ever
 * boot on fresh silicon) — it writes two ramp runs (2048 + 39350 B)
 * with NEW timestamps so a previously-benched app sees them as missing.
 * The real lifecycle (create/append/close/rotate) is 4b.
 *
 * NOTE: SWD flashing (zflash) writes the nRF52's INTERNAL code flash —
 * this NOR keeps its contents across firmware re-flashes. The synthetic
 * writer is a once-per-chip event unless the index sector is erased.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include "store.h"

#define FLASH_SECTOR   4096u
#define DATA_BASE      0x14000u
#define SLOT_SIZE      0x3D000u     /* 244 KiB — classic geometry */
#define SLOT_COUNT     8u
#define CONFIG_ADDR    0x1FC000u
#define INDEX_ADDR     0x1FD000u
#define INDEX_MAGIC    0x52525332u  /* "RRS2" */

/* RunHeader 16 B packed — littlefs_storage.h verbatim (v2: data_size
 * and frame_count are 0 ON DISK; live totals live in the index). */
struct __attribute__((packed)) run_header {
	uint8_t  format_ver;
	uint8_t  arm_side;
	uint32_t ts_utc;
	int16_t  baro_temp;
	uint32_t data_size;
	uint16_t frame_count;
	uint8_t  cal_accuracy;
	uint8_t  _pad;
};

/* RunEntry 32 B packed — littlefs_storage.h verbatim. */
struct __attribute__((packed)) run_entry {
	uint16_t run_id;
	uint32_t page_start;     /* unused — kept for struct compat */
	uint32_t page_end;       /* unused — kept for struct compat */
	uint32_t timestamp;
	uint8_t  arm_side;
	uint8_t  format_version;
	uint32_t compressed_size;
	uint32_t frame_count;
	uint8_t  _reserved[8];
};

/* IndexHeader 16 B packed — raw_run_store.h verbatim. */
struct __attribute__((packed)) index_header {
	uint32_t magic;
	uint16_t next_run_id;
	uint16_t entry_count;
	uint32_t slot_used_mask;
	uint16_t max_slots;
	uint8_t  _pad[2];
};

static const struct device *flashdev;
static struct run_entry entries[SLOT_COUNT];
static uint8_t  entry_slot[SLOT_COUNT];
static uint8_t  entry_count;

/* ── CRC32 (bitwise, identical math to RawRunStore's table) ──────── */

static uint32_t crc32_byte(uint32_t crc, uint8_t b)
{
	crc ^= b;
	for (uint8_t i = 0; i < 8; i++) {
		crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return crc;
}

/* ── Store API (reads served from the in-RAM index cache) ────────── */

uint8_t store_run_count(void)
{
	return entry_count;
}

uint32_t store_run_payload_len(uint8_t id)
{
	return entries[id].compressed_size;
}

uint32_t store_run_ts(uint8_t id)
{
	return entries[id].timestamp;
}

uint8_t store_run_side(uint8_t id)
{
	return entries[id].arm_side;
}

uint32_t store_run_total(uint8_t id)
{
	return sizeof(struct run_header) +
	       entries[id].compressed_size + 6;
}

void store_read_blob(uint8_t id, uint32_t off, uint8_t *dst, uint32_t n)
{
	uint32_t addr = DATA_BASE +
			(uint32_t)entry_slot[id] * SLOT_SIZE + off;
	int rc = flash_read(flashdev, (off_t)addr, dst, (size_t)n);

	if (rc != 0) {
		printk("ERROR: flash_read @0x%lx -> %d\n",
		       (unsigned long)addr, rc);
		memset(dst, 0xFF, n);
	}
}

/* ── Index load (production read path) ───────────────────────────── */

static bool load_index(void)
{
	struct index_header ih;

	if (flash_read(flashdev, INDEX_ADDR, &ih, sizeof(ih)) != 0) {
		return false;
	}
	if (ih.magic != INDEX_MAGIC ||
	    ih.entry_count == 0 || ih.entry_count > SLOT_COUNT) {
		return false;
	}

	for (uint8_t i = 0; i < ih.entry_count; i++) {
		uint8_t rec[sizeof(struct run_entry) + 1];

		if (flash_read(flashdev,
			       (off_t)(INDEX_ADDR + sizeof(ih) +
				       (uint32_t)i * sizeof(rec)),
			       rec, sizeof(rec)) != 0) {
			return false;
		}
		memcpy(&entries[i], rec, sizeof(struct run_entry));
		entry_slot[i] = rec[sizeof(struct run_entry)];
		if (entry_slot[i] >= SLOT_COUNT) {
			return false;
		}
	}
	entry_count = ih.entry_count;
	return true;
}

/* ── Synthetic writer (4a only — replaced by lifecycle in 4b) ────── */

static bool program_run(uint8_t slot, uint8_t side, uint32_t ts,
			uint32_t payload_len, uint8_t cal)
{
	uint32_t base = DATA_BASE + (uint32_t)slot * SLOT_SIZE;
	uint32_t need = sizeof(struct run_header) + payload_len + 6;
	uint32_t sectors = (need + FLASH_SECTOR - 1) / FLASH_SECTOR;

	/* Erase only what we use (lazy sectors, ensure_erased style) —
	 * full-slot erase is 4b's job at POST_RUN/boot. */
	if (flash_erase(flashdev, (off_t)base,
			sectors * FLASH_SECTOR) != 0) {
		printk("ERROR: slot %u erase\n", slot);
		return false;
	}

	struct run_header hdr = {
		.format_ver = 2, .arm_side = side, .ts_utc = ts,
		.baro_temp = 125, .data_size = 0, .frame_count = 0,
		.cal_accuracy = cal, ._pad = 0,
	};

	if (flash_write(flashdev, (off_t)base, &hdr, sizeof(hdr)) != 0) {
		return false;
	}

	/* Ramp payload, generated in 4 KB chunks (no big RAM buffer) —
	 * the 39 KB classic run fits comfortably through a window this
	 * small; identical math to the fake store. */
	uint8_t buf[FLASH_SECTOR];
	uint32_t crc = 0xFFFFFFFFu;
	uint32_t done = 0;

	while (done < payload_len) {
		uint32_t take = MIN(FLASH_SECTOR, payload_len - done);

		for (uint32_t i = 0; i < take; i++) {
			buf[i] = (uint8_t)(done + i);
			crc = crc32_byte(crc, buf[i]);
		}
		if (flash_write(flashdev,
				(off_t)(base + sizeof(hdr) + done),
				buf, take) != 0) {
			return false;
		}
		done += take;
	}

	crc ^= 0xFFFFFFFFu;
	uint8_t trailer[6] = { 0xC3, 0x32,
			       (uint8_t)crc, (uint8_t)(crc >> 8),
			       (uint8_t)(crc >> 16), (uint8_t)(crc >> 24) };

	if (flash_write(flashdev, (off_t)(base + sizeof(hdr) + payload_len),
			trailer, sizeof(trailer)) != 0) {
		return false;
	}
	return true;
}

static bool persist_index(void)
{
	if (flash_erase(flashdev, INDEX_ADDR, FLASH_SECTOR) != 0) {
		return false;
	}

	uint8_t page[256];

	memset(page, 0xFF, sizeof(page));
	struct index_header ih = {
		.magic = INDEX_MAGIC,
		.next_run_id = entry_count,
		.entry_count = entry_count,
		.slot_used_mask = 0x00000003u,   /* slots 0+1 */
		.max_slots = SLOT_COUNT,
	};
	memcpy(page, &ih, sizeof(ih));

	size_t off = sizeof(ih);

	for (uint8_t i = 0; i < entry_count; i++) {
		memcpy(page + off, &entries[i], sizeof(entries[i]));
		off += sizeof(entries[i]);
		page[off++] = entry_slot[i];
	}
	return flash_write(flashdev, INDEX_ADDR, page, sizeof(page)) == 0;
}

static void write_synthetic_runs(void)
{
	printk("flash: empty index — writing synthetic runs (once)\n");

	/* New timestamps (≠ fake-store era) so a benched app sees these
	 * as missing runs without clearing its data. */
	const uint32_t lens[2]  = { 2048, 39350 };
	const uint32_t tss[2]   = { 1758584000u, 1758593000u };
	const uint8_t  sides[2] = { 0, 1 };

	for (uint8_t i = 0; i < 2; i++) {
		if (!program_run(i, sides[i], tss[i], lens[i], 3)) {
			printk("ERROR: synthetic run %u failed\n", i);
			entry_count = 0;
			return;
		}
		memset(&entries[i], 0, sizeof(entries[i]));
		entries[i].run_id = i;
		entries[i].timestamp = tss[i];
		entries[i].arm_side = sides[i];
		entries[i].format_version = 2;
		entries[i].compressed_size = lens[i];
		entries[i].frame_count =
			(uint32_t)((lens[i] + 239) / 240);
		entry_slot[i] = i;
	}
	entry_count = 2;

	if (!persist_index()) {
		printk("ERROR: index persist\n");
		entry_count = 0;
	}
}

/* ── Boot ─────────────────────────────────────────────────────────── */

void store_init(void)
{
	flashdev = DEVICE_DT_GET(DT_NODELABEL(mx25r1635f));
	if (!device_is_ready(flashdev)) {
		printk("ERROR: flash device not ready\n");
		entry_count = 0;
		return;
	}

	struct flash_pages_info info;

	if (flash_get_page_info_by_offs(flashdev, 0, &info) == 0) {
		size_t pages = flash_get_page_count(flashdev);

		printk("flash: %u KiB, %u pages x %u B\n",
		       (unsigned)(pages * info.size / 1024),
		       (unsigned)pages, (unsigned)info.size);
	}

	if (!load_index()) {
		write_synthetic_runs();
	}

	/* 4 KB sector-erase proof (4b precondition, JP 09-24): the region
	 * ArduinoFW reserved as "SPI self-test" (chip-0x2000) is the safe
	 * victim — 4 KB past the index sector, no run data. Save, erase
	 * ONE sector, verify 0xFF, restore. */
	{
		/* 4 KB static save buffer — 8 KB of locals would blow the
		 * main stack; verify in small chunks instead. */
		static uint8_t save[FLASH_SECTOR];
		uint8_t chk[256];
		uint32_t st = 0x1FE000u;
		bool all_ff = true;

		flash_read(flashdev, st, save, sizeof(save));
		if (flash_erase(flashdev, st, FLASH_SECTOR) == 0) {
			for (uint32_t o = 0; o < FLASH_SECTOR && all_ff;
			     o += sizeof(chk)) {
				flash_read(flashdev, st + o, chk, sizeof(chk));
				for (uint32_t i = 0; i < sizeof(chk); i++) {
					if (chk[i] != 0xFF) {
						all_ff = false;
						break;
					}
				}
			}
		} else {
			all_ff = false;
		}
		printk("selftest: 4K erase %s (layout page=%u)\n",
		       all_ff ? "OK" : "FAILED", (unsigned)info.size);
		flash_write(flashdev, st, save, sizeof(save));
	}

	for (uint8_t i = 0; i < entry_count; i++) {
		printk("run %u: payload=%lu sz=%lu slot=%u ts=%lu\n",
		       i,
		       (unsigned long)entries[i].compressed_size,
		       (unsigned long)store_run_total(i),
		       entry_slot[i],
		       (unsigned long)entries[i].timestamp);
	}
}
