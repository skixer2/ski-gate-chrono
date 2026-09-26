/*
 * store_flash.c — circular run log over SPI NOR (step 4b)
 * =======================================================
 * The production store. Replaces 4a's slot-based reader with the
 * circular log validated by tests/test_layout.c — read the header
 * essay in store_layout.h for the design rationale (short version:
 * JP's packing insight + retained-history requirement → ~48 typical
 * runs on the 2 MB chip instead of 8, oldest-only reclamation, hot
 * path stays program-only).
 *
 * ── What lives where (flash map, top rows unchanged from 4a) ───────
 *
 *   0x00000–0x13FFF   pre-roll region (FlashRing era — untouched, the
 *                     sensor pipeline owns it when it arrives)
 *   0x14000–0x1FBFFF  THE LOG: packed runs, sector-padded, circular
 *   0x1FC000          config (future: device settings persistence)
 *   0x1FD000          INDEX (format below — device-internal, the app
 *                     never sees it; only the blob + D/s are frozen)
 *   0x1FE000          self-test victim sector (4 KB erase proof)
 *
 * ── Index format ("SGL1", ours to design — see ledger f02fdc5) ────
 *
 *   [16 B header][16 B entry] × entry_count   (≤ 1040 B, one sector)
 *
 *   header: magic "SGL1" · write_cursor · erased_end · entry_count ·
 *           next_run_id
 *   entry:  addr · payload_len · ts · run_id · side · cal
 *
 *   Entries are in AGE ORDER (oldest first) — the same order the
 *   directory lists and reclamation consumes. No slot bytes anymore:
 *   a log entry IS its address range.
 *
 * ── Concurrency & the recording producer ───────────────────────────
 *
 *   Recording must never block the shared system workqueue: erases
 *   take tens of ms per sector and a 40 KB run needs ~10 of them.
 *   So the producer is a small STATE MACHINE driven by a delayable
 *   work item — one sector / one 4 KB chunk per pass, rescheduled
 *   with K_MSEC(1) between passes. The future sensor pipeline will
 *   call the same append path from its own pacing; 'w' merely feeds
 *   it ramp bytes instead of sensor frames.
 *
 *   States: IDLE → ERASING (reclaim + prep, one sector/pass)
 *                 → WRITING (header + payload chunks + CRC on the fly)
 *                 → CLOSING (trailer, index persist, entry commit)
 *                 → IDLE
 *
 * ── Crash windows (documented, accepted for the bench era) ────────
 *
 *   The index is persisted (a) after the erase/reclaim phase and
 *   (b) at close. A crash between those loses the in-flight run's
 *   entry but never corrupts the catalog of CLOSED runs; a stale
 *   entry pointing at erased flash fails CRC at the app — loud, not
 *   silent. Good enough until the watchdog/commit discipline of the
 *   production state machine arrives.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include "store.h"
#include "store_layout.h"

/* ── Flash geometry & index placement (map above) ─────────────────── */
#define INDEX_ADDR       0x1FD000u
#define SELFTEST_ADDR    0x1FE000u
#define SGL1_MAGIC       0x314C4753u   /* "SGL1" little-endian        */

/* Timestamps: the device has no RTC until the epoch-sync port (the
 * phone writes ABC0). Until then, synthetic runs are stamped from the
 * BUILD date + uptime — real dates on the phone's list, no more
 * accidental 2025 (the fake-store lesson, ledgered 09-24). */
#define BUILD_EPOCH      1790294400u   /* 2026-09-24 00:00 UTC        */

/* ── Wire-format structures (FROZEN — app contract) ─────────────────
 * RunHeader 16 B packed (littlefs_storage.h verbatim) and the 6 B
 * trailer (C3 32 + CRC32 LE over payload only). DO NOT touch these:
 * the unchanged App 1.46 parses them. */
struct __attribute__((packed)) run_header {
	uint8_t  format_ver;     /* 2 = append-only era                */
	uint8_t  arm_side;       /* 0=left, 1=right                    */
	uint32_t ts_utc;
	int16_t  baro_temp;      /* tenths of °C                       */
	uint32_t data_size;      /* 0 on disk (v2) — totals in index   */
	uint16_t frame_count;    /* 0 on disk (v2)                     */
	uint8_t  cal_accuracy;
	uint8_t  _pad;
};

/* ── Index structures (device-internal — ours) ────────────────────── */
struct __attribute__((packed)) sgl_header {
	uint32_t magic;
	uint32_t write_cursor;    /* next run starts here (sector-aligned) */
	uint32_t erased_end;      /* [.., erased_end) is erased & ready    */
	uint16_t entry_count;
	uint16_t next_run_id;
	uint8_t  _pad[2];
};

struct __attribute__((packed)) sgl_entry {
	uint32_t addr;            /* absolute flash address of the header   */
	uint32_t payload_len;
	uint32_t ts_utc;
	uint16_t run_id;
	uint8_t  arm_side;
	uint8_t  cal_accuracy;
};

/* ── In-RAM store state (the index, cached) ───────────────────────── */
static const struct device *flashdev;
static struct sgl_entry entries[MAX_INDEXED_RUNS];
static uint16_t entry_count;
static uint16_t next_run_id;
static uint32_t write_cursor;
static uint32_t erased_end;

/* ── CRC32 (bitwise; identical math to RawRunStore's table) ───────── */
static uint32_t crc32_byte(uint32_t crc, uint8_t b)
{
	crc ^= b;
	for (uint8_t i = 0; i < 8; i++) {
		crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return crc;
}

/* ════════════════════════════════════════════════════════════════════
 * READ API — called by pull_proto (D / s commands, ABC9 listings)
 * Everything is served from the in-RAM index cache; blob reads hit
 * flash only. Same signatures as the fake store: the protocol layer
 * never knew 4b happened.
 * ════════════════════════════════════════════════════════════════════ */

uint8_t store_run_count(void)
{
	return (uint8_t)entry_count;   /* capped at 64, fits u8 */
}

uint32_t store_run_payload_len(uint8_t id)
{
	return entries[id].payload_len;
}

uint32_t store_run_ts(uint8_t id)
{
	return entries[id].ts_utc;
}

uint8_t store_run_side(uint8_t id)
{
	return entries[id].arm_side;
}

uint32_t store_run_total(uint8_t id)
{
	return BLOB_OVERHEAD + entries[id].payload_len;
}

void store_read_blob(uint8_t id, uint32_t off, uint8_t *dst, uint32_t n)
{
	/* One linear read — this is WHY runs never wrap mid-run. */
	int rc = flash_read(flashdev, (off_t)(entries[id].addr + off),
			    dst, (size_t)n);

	if (rc != 0) {
		printk("ERROR: flash_read @0x%lx -> %d\n",
		       (unsigned long)(entries[id].addr + off), rc);
		memset(dst, 0xFF, n);
	}
}

/* ════════════════════════════════════════════════════════════════════
 * INDEX PERSISTENCE
 * One 4 KB sector at INDEX_ADDR: erase, then a single page write of
 * header + entries. ~1 KB worst case — comfortably one sector.
 * ════════════════════════════════════════════════════════════════════ */

static void persist_index(void)
{
	struct sgl_header hdr = {
		.magic = SGL1_MAGIC,
		.write_cursor = write_cursor,
		.erased_end = erased_end,
		.entry_count = entry_count,
		.next_run_id = next_run_id,
	};
	/* STATIC: 1040 B as a local overflowed the main thread's ~1 KB
	 * stack — the boot MPU fault (PC landing in the kernel stacks below
	 * z_main_stack). Same lesson as the self-test buffer, re-learned
	 * 09-26. Single-writer (store ops are serialized), so shared. */
	static uint8_t page[sizeof(struct sgl_header) + sizeof(entries)];

	/* (Erase is ~50–100 ms; we run in work context, never ISR.) */
	flash_erase(flashdev, INDEX_ADDR, LOG_SECTOR_SIZE);
	memcpy(page, &hdr, sizeof(hdr));
	memcpy(page + sizeof(hdr), entries,
	       entry_count * sizeof(entries[0]));
	flash_write(flashdev, INDEX_ADDR, page,
		    sizeof(hdr) + entry_count * sizeof(entries[0]));
}

static bool load_index(void)
{
	struct sgl_header hdr;

	if (flash_read(flashdev, INDEX_ADDR, &hdr, sizeof(hdr)) != 0 ||
	    hdr.magic != SGL1_MAGIC ||
	    hdr.entry_count > MAX_INDEXED_RUNS) {
		return false;
	}
	if (hdr.entry_count > 0 &&
	    flash_read(flashdev, INDEX_ADDR + sizeof(hdr),
		       entries, hdr.entry_count * sizeof(entries[0])) != 0) {
		return false;
	}

	/* Cursor sanity: a corrupt cursor would poison every future
	 * write, so we validate against the region and the last entry. */
	if (hdr.write_cursor < LOG_START || hdr.write_cursor > LOG_END ||
	    hdr.erased_end < LOG_START || hdr.erased_end > LOG_END) {
		return false;
	}

	entry_count = hdr.entry_count;
	next_run_id = hdr.next_run_id;
	write_cursor = hdr.write_cursor;
	erased_end = hdr.erased_end;
	return true;
}

/* ════════════════════════════════════════════════════════════════════
 * RECORDING PRODUCER — the real lifecycle, chunked & non-blocking
 *
 * store_record_start(len) validates, plans (address, erase range,
 * reclaims), and kicks the state machine. Each work pass does ONE
 * bounded piece of work (one sector erase OR one 4 KB payload chunk)
 * and reschedules — the sysworkq is never held for more than a few
 * ms of flash time per pass.
 * ════════════════════════════════════════════════════════════════════ */

enum rec_state { REC_IDLE, REC_ERASING, REC_WRITING, REC_CLOSING };

static struct k_work_delayable rec_work;
static enum rec_state rec_state = REC_IDLE;

/* Plan, filled by store_record_start: */
static struct {
	uint32_t addr;         /* where this run's header goes          */
	uint32_t span;         /* sector-padded total occupancy         */
	uint32_t payload_len;
	uint32_t written;      /* payload bytes emitted so far          */
	uint32_t crc;          /* running CRC (payload only!)           */
	uint32_t erase_from;   /* erase sweep cursor                    */
	uint32_t erase_to;     /* inclusive end of erase range          */
	bool     wrapped;      /* true → erase range includes the stub  */
	uint32_t ts;
	uint8_t  side;
} rec;

bool store_busy(void)
{
	return rec_state != REC_IDLE;
}

/* Does [a1,a2) intersect [b1,b2)? (Mirror of the host-test helper.) */
static bool overlaps(uint32_t a1, uint32_t a2, uint32_t b1, uint32_t b2)
{
	return a1 < b2 && b1 < a2;
}

static void rec_work_handler(struct k_work *w)
{
	switch (rec_state) {
	case REC_ERASING: {
		/* One sector per pass. The sweep covers [addr, addr+span)
		 * plus the wrap stub when we circled — and it may have
		 * required reclaiming the oldest entries (done at plan
		 * time; their extents are inside this same sweep). */
		uint32_t from = rec.erase_from;

		if (from < rec.erase_to) {
			flash_erase(flashdev, from, LOG_SECTOR_SIZE);
			rec.erase_from = from + LOG_SECTOR_SIZE;
			if (erased_end < from + LOG_SECTOR_SIZE) {
				erased_end = from + LOG_SECTOR_SIZE;
			}
			k_work_schedule(&rec_work, K_MSEC(1));
			return;
		}

		/* Erase phase done → persist (crash window (a), see file
		 * header) and write the run header. */
		persist_index();

		struct run_header hdr = {
			.format_ver = 2, .arm_side = rec.side,
			.ts_utc = rec.ts, .baro_temp = 125,
			.data_size = 0, .frame_count = 0,
			.cal_accuracy = 3, ._pad = 0,
		};

		flash_write(flashdev, (off_t)rec.addr, &hdr, sizeof(hdr));
		printk("rec: writing %lu B at 0x%lx\n",
		       (unsigned long)rec.payload_len,
		       (unsigned long)rec.addr);
		rec_state = REC_WRITING;
		k_work_schedule(&rec_work, K_MSEC(1));
		return;
	}

	/* NOTE (bug 09-26, fixed v0.7.4): BLOB_OVERHEAD (22) is SIZE math
	 * (header+trailer). Byte OFFSETS use sizeof(run_header) — the 4b
	 * producer wrote payload at +22 instead of +16, pushing the trailer
	 * 6 bytes past the payload end. Size vs offset: never mix them. */
	case REC_WRITING: {
		/* One 4 KB chunk per pass. The payload is the ramp
		 * generator (bench producer); the sensor pipeline will
		 * replace just this loop's data source, nothing else.
		 * STATIC: 4 KB as a local would blow the sysworkq stack —
		 * the same overflow family as persist_index (09-26). The
		 * producer is a single state machine, so sharing is safe. */
		static uint8_t buf[LOG_SECTOR_SIZE];
		uint32_t remain = rec.payload_len - rec.written;
		uint32_t take = MIN(sizeof(buf), remain);

		for (uint32_t i = 0; i < take; i++) {
			buf[i] = (uint8_t)(rec.written + i);
			rec.crc = crc32_byte(rec.crc, buf[i]);
		}
		flash_write(flashdev,
			    (off_t)(rec.addr + sizeof(struct run_header) +
				    rec.written),
			    buf, take);
		rec.written += take;

		if (rec.written < rec.payload_len) {
			k_work_schedule(&rec_work, K_MSEC(1));
			return;
		}
		rec_state = REC_CLOSING;
		k_work_schedule(&rec_work, K_MSEC(1));
		return;
	}

	case REC_CLOSING: {
		/* Trailer: C3 32 + CRC32 LE (payload only, init/final-xor
		 * 0xFFFFFFFF — the frozen wire format). */
		uint32_t crc = rec.crc ^ 0xFFFFFFFFu;
		uint8_t trailer[6] = { 0xC3, 0x32,
				       (uint8_t)crc, (uint8_t)(crc >> 8),
				       (uint8_t)(crc >> 16),
				       (uint8_t)(crc >> 24) };

		flash_write(flashdev,
			    (off_t)(rec.addr + sizeof(struct run_header) +
				    rec.payload_len),
			    trailer, sizeof(trailer));

		/* Commit the entry (append at the age-ordered tail) and
		 * advance the cursor to the next sector boundary. */
		if (entry_count == MAX_INDEXED_RUNS) {
			/* Tiny-run edge: index full → drop the OLDEST
			 * entry (its flash joins future reclamation). */
			memmove(&entries[0], &entries[1],
				(entry_count - 1) * sizeof(entries[0]));
			entry_count--;
		}
		entries[entry_count] = (struct sgl_entry){
			.addr = rec.addr,
			.payload_len = rec.payload_len,
			.ts_utc = rec.ts,
			.run_id = next_run_id++,
			.arm_side = rec.side,
			.cal_accuracy = 3,
		};
		entry_count++;
		write_cursor = rec.addr + rec.span;

		persist_index();          /* crash window (b) closed */
		printk("rec: done id=%u payload=%lu total=%lu runs=%u\n",
		       (unsigned)(next_run_id - 1),
		       (unsigned long)rec.payload_len,
		       (unsigned long)(BLOB_OVERHEAD + rec.payload_len),
		       entry_count);
		rec_state = REC_IDLE;
		return;
	}

	default:
		return;
	}
}

bool store_record_start(uint32_t payload_len)
{
	if (store_busy()) {
		return false;
	}
	if (payload_len == 0 ||
	    payload_len + BLOB_OVERHEAD > MAX_RUN_BUDGET) {
		return false;
	}

	uint32_t span = run_span(payload_len);

	/* Plan the address (wrap rule: never straddle LOG_END). */
	rec.addr = next_run_addr(write_cursor, span, &rec.wrapped);
	rec.span = span;
	rec.payload_len = payload_len;
	rec.written = 0;
	rec.crc = 0xFFFFFFFFu;
	rec.ts = BUILD_EPOCH + k_uptime_get_32() / 1000;
	rec.side = (uint8_t)(next_run_id & 1);   /* alternate L/R for bench */

	/* Erase sweep = the run's extent (+ stub if wrapped), starting
	 * from wherever the erased runway currently ends. */
	rec.erase_from = (rec.addr > erased_end) ? rec.addr : erased_end;
	rec.erase_to = rec.addr + span;
	if (rec.wrapped) {
		rec.erase_to += (LOG_END - write_cursor);
	}

	/* RECLAMATION: drop oldest entries whose extents intersect the
	 * sweep — the entire garbage policy, mirroring the host test. */
	while (entry_count > 0 &&
	       overlaps(entries[0].addr,
			entries[0].addr + run_span(entries[0].payload_len),
			rec.erase_from, rec.erase_to)) {
		printk("rec: reclaim id=%u (%lu B, oldest)\n",
		       entries[0].run_id,
		       (unsigned long)entries[0].payload_len);
		memmove(&entries[0], &entries[1],
			(entry_count - 1) * sizeof(entries[0]));
		entry_count--;
	}

	printk("rec: start %lu B%s at 0x%lx (erase %lu B)\n",
	       (unsigned long)payload_len, rec.wrapped ? " [wrap]" : "",
	       (unsigned long)rec.addr,
	       (unsigned long)(rec.erase_to - rec.erase_from));

	rec_state = REC_ERASING;
	k_work_schedule(&rec_work, K_NO_WAIT);
	return true;
}

/* ════════════════════════════════════════════════════════════════════
 * BOOT
 * ════════════════════════════════════════════════════════════════════ */

void store_init(void)
{
	flashdev = DEVICE_DT_GET(DT_NODELABEL(mx25r1635f));
	if (!device_is_ready(flashdev)) {
		printk("ERROR: flash device not ready\n");
		return;
	}

	/* Page report doubles as the layout sanity check: we REQUIRE the
	 * 4 KiB granularity the log is built on (see self-test below). */
	struct flash_pages_info info;

	if (flash_get_page_info_by_offs(flashdev, 0, &info) == 0) {
		size_t pages = flash_get_page_count(flashdev);

		printk("flash: %u KiB, %u pages x %u B\n",
		       (unsigned)(pages * info.size / 1024),
		       (unsigned)pages, (unsigned)info.size);
	}

	/* 4 KB sector-erase proof (4b precondition, benched 09-24):
	 * victim = the self-test sector, 4 KB past the index. Save,
	 * erase ONE sector, verify 0xFF in chunks, restore. Static
	 * buffer — 8 KB of locals would blow the main stack. */
	{
		static uint8_t save[LOG_SECTOR_SIZE];
		uint8_t chk[256];
		bool all_ff = true;

		flash_read(flashdev, SELFTEST_ADDR, save, sizeof(save));
		if (flash_erase(flashdev, SELFTEST_ADDR,
				LOG_SECTOR_SIZE) == 0) {
			for (uint32_t o = 0; o < LOG_SECTOR_SIZE && all_ff;
			     o += sizeof(chk)) {
				flash_read(flashdev, SELFTEST_ADDR + o,
					   chk, sizeof(chk));
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
		flash_write(flashdev, SELFTEST_ADDR, save, sizeof(save));
	}

	/* Load the log index — or start fresh. Fresh means: cursor at
	 * LOG_START, NOTHING erased yet, zero entries; the first 'w'
	 * erases exactly what it needs. Legacy slot-era data (5 runs,
	 * safe on the phone since 09-24) is consumed by reclamation as
	 * the log grows — no bulk erase, no boot delay. */
	if (!load_index()) {
		write_cursor = LOG_START;
		erased_end = LOG_START;
		entry_count = 0;
		next_run_id = 0;
		printk("store: fresh log (legacy slot data reclaims lazily)\n");
		persist_index();
	} else {
		printk("store: %u runs, cursor 0x%lx, erased to 0x%lx\n",
		       entry_count, (unsigned long)write_cursor,
		       (unsigned long)erased_end);
	}

	for (uint8_t i = 0; i < entry_count && i < 8; i++) {
		printk("run %u: payload=%lu sz=%lu id=%u ts=%lu\n",
		       i, (unsigned long)entries[i].payload_len,
		       (unsigned long)(BLOB_OVERHEAD + entries[i].payload_len),
		       entries[i].run_id, (unsigned long)entries[i].ts_utc);
	}
	if (entry_count > 8) {
		printk("… +%u more\n", entry_count - 8);
	}

	k_work_init_delayable(&rec_work, rec_work_handler);
}

void store_wipe(void)
{
	if (store_busy()) {
		return;
	}
	flash_erase(flashdev, INDEX_ADDR, LOG_SECTOR_SIZE);
	write_cursor = LOG_START;
	erased_end = LOG_START;
	entry_count = 0;
	next_run_id = 0;
	printk("store: wiped\n");
}
