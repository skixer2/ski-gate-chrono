/*
 * store_flash.c — SGL2: the circular run log over real SPI NOR
 * ====================================================================
 * Design essay: store_layout.h (the pure arithmetic — host-tested).
 * This file is the hardware side: flash ops + the record state machine.
 *
 * ── Record lifecycle (SGL2) ────────────────────────────────────────
 *
 *   store_record_start(len)  NO erase, NO reclaim here. The runway
 *   ──────────────────────  was topped up right after the PREVIOUS
 *                           run (and at boot / after wipe). A coverage
 *                           guard verifies and fails LOUDLY if the
 *                           invariant was ever violated (defense).
 *   IDLE → WRITING          header + payload chunks + CRC on the fly
 *          → CLOSING        trailer, index entry commit, persist
 *          → TOPUP          erase top-up until coverage reaches
 *                           U(cursor) — then persist again.
 *   IDLE                    (also entered from boot/wipe via TOPUP)
 *
 * ── Erase policy (JP, 2026-09-28 — the SGL2 requirement) ───────────
 *
 *   Run length is UNKNOWN at start (an athlete's descent; a future
 *   streaming sensor producer). So erase work is never sized for the
 *   incoming record. Right AFTER a run (and at boot, and after wipe)
 *   we free enough space for a MAXIMUM-length run:
 *
 *     U(cursor) = [cursor, cursor+RUNWAY_TARGET)         when it fits
 *               = [cursor, LOG_END) ∪ [LOG_START,
 *                 LOG_START+RUNWAY_TARGET)               near the end
 *
 *   A run that used less than the max leaves the rest of the runway
 *   ready for the run after — "even better", exactly as ordered.
 *   Two persisted frontiers track coverage: erased_hi (from cursor)
 *   and erased_lo (bottom reserve). Absolute math only — NO ring
 *   arithmetic (the SGL1 wrap bug was a ring-composition error;
 *   ledger 2026-09-28).
 *
 * ── Concurrency: the non-blocking producer ─────────────────────────
 *
 *   Erases take tens of ms per sector and a top-up can need dozens.
 *   The producer is a small STATE MACHINE driven by a delayable work
 *   item — one sector erase / one 4 KB payload chunk per pass,
 *   rescheduled with K_MSEC(1). The sysworkq is never held long.
 *
 * ── Crash windows (documented, bench-era accepted) ─────────────────
 *
 *   (a) mid-TOPUP:  index on flash still lists victims whose sectors
 *       are partially erased → their CRC fails at the app — loud, not
 *       silent. Catalog of untouched runs is intact.
 *   (b) after close-persist, before TOPUP-persist: runway on flash is
 *       short of what RAM believes → boot re-erases the delta —
 *       harmless (idempotent).
 *   (c) mid-RECORD: no entry is committed, but the partial bytes sit
 *       INSIDE what the persisted index claims is an erased runway.
 *       Boot heals this: verify_runway() reads the first bytes of the
 *       claimed runway — anything not 0xFF resets the frontier and
 *       forces a full re-top-up.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include "store.h"
#include "store_layout.h"

/* ── Flash geometry & index placement (map above) ─────────────────── */
#define INDEX_ADDR       0x1FD000u
#define SELFTEST_ADDR    0x1FE000u
#define SGL3_MAGIC       0x334C4753u   /* "SGL3" little-endian        */

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

/* ── Index structures (device-internal — SGL3 format) ───────────────
 * SGL1 → SGL2 (2026-09-28): two-frontier runway. SGL2 → SGL3 (same
 * day, JP GO): run_budget added — the ACTIVE discipline budget drives
 * the runway target. Older indexes fail the magic check and start
 * fresh — loud, not silent. */
struct __attribute__((packed)) sgl_header {
	uint32_t magic;
	uint32_t write_cursor;    /* next run starts here (sector-adj.)  */
	uint32_t erased_hi;       /* runway end from cursor              */
	uint32_t erased_lo;       /* bottom reserve end                  */
	uint32_t run_budget;      /* ACTIVE budget, payload bytes (SGL3) */
	uint16_t entry_count;
	uint16_t next_run_id;
};

struct __attribute__((packed)) sgl_entry {
	uint32_t addr;            /* absolute flash address of the header */
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
static uint32_t erased_hi;
static uint32_t erased_lo;
static uint32_t run_budget = RUN_BUDGET_DEFAULT;

/* ── Run timestamps: MONOTONIC BY CONSTRUCTION (2026-09-30) ────────
 * BUILD_EPOCH + uptime jumps BACKWARD on every reflash (uptime resets)
 * — bench reality: four reflashes yesterday produced out-of-order and
 * colliding stamps, confusing the app's downloaded-marking. Guard:
 * ts = max(last_ts + 1, computed), last_ts seeded from the loaded
 * index at boot and persisted implicitly via entries. Proper fix =
 * ABC0 epoch sync (next step); this makes the bench behave until then. */
static uint32_t last_run_ts;

static uint32_t next_run_ts(void)
{
	uint32_t computed = BUILD_EPOCH + k_uptime_get_32() / 1000;

	if (computed <= last_run_ts) {
		computed = last_run_ts + 1;
	}
	last_run_ts = computed;
	return computed;
}

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
		.magic = SGL3_MAGIC,
		.write_cursor = write_cursor,
		.erased_hi = erased_hi,
		.erased_lo = erased_lo,
		.run_budget = run_budget,
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
	    hdr.magic != SGL3_MAGIC ||
	    hdr.entry_count > MAX_INDEXED_RUNS) {
		return false;
	}
	if (hdr.entry_count > 0 &&
	    flash_read(flashdev, INDEX_ADDR + sizeof(hdr),
		       entries, hdr.entry_count * sizeof(entries[0])) != 0) {
		return false;
	}

	/* Cursor/frontier sanity: a corrupt cursor would poison every
	 * future write, so we validate against the region; hi must
	 * never trail the cursor (invariant of every persisted state). */
	if (hdr.write_cursor < LOG_START || hdr.write_cursor > LOG_END ||
	    hdr.erased_hi < LOG_START || hdr.erased_hi > LOG_END ||
	    hdr.erased_lo < LOG_START || hdr.erased_lo > LOG_END ||
	    hdr.erased_hi < hdr.write_cursor) {
		return false;
	}

	if (hdr.run_budget == 0 || hdr.run_budget > MAX_RUN_BUDGET) {
		return false;
	}
	entry_count = hdr.entry_count;
	next_run_id = hdr.next_run_id;
	write_cursor = hdr.write_cursor;
	erased_hi = hdr.erased_hi;
	erased_lo = hdr.erased_lo;
	run_budget = hdr.run_budget;

	/* Monotonic-timestamp seed: the newest entry's stamp (entries are
	 * age-ordered, but stamps predate the guard — take the max). */
	for (uint16_t i = 0; i < entry_count; i++) {
		if (entries[i].ts_utc > last_run_ts) {
			last_run_ts = entries[i].ts_utc;
		}
	}
	return true;
}

/* ════════════════════════════════════════════════════════════════════
 * RECORDING PRODUCER — the real lifecycle, chunked & non-blocking
 *
 * store_record_start(len) validates, plans the address, and guards
 * the runway invariant. Each work pass does ONE bounded piece of work
 * (one sector erase OR one 4 KB payload chunk) and reschedules.
 * ════════════════════════════════════════════════════════════════════ */

enum rec_state { REC_IDLE, REC_WRITING, REC_CLOSING, REC_TOPUP };

static struct k_work_delayable rec_work;
static enum rec_state rec_state = REC_IDLE;

/* Plan, filled by store_record_start: */
static struct {
	uint32_t addr;         /* where this run's header goes          */
	uint32_t span;         /* sector-padded total occupancy         */
	uint32_t payload_len;
	uint32_t written;      /* payload bytes emitted so far          */
	uint32_t crc;          /* running CRC (payload only!)           */
	bool     wrapped;      /* record placed at LOG_START            */
	uint32_t ts;
	uint8_t  side;
} rec;

/* Top-up walk state (REC_TOPUP): */
static struct {
	struct runway_plan plan;
	uint8_t  seg;         /* 0 = segment 1, 1 = segment 2          */
	uint32_t from, to;    /* current segment walk cursors          */
} prep;

/* Streaming-append record state (M3): separate from the bench 'w' rec
 * so the proven path stays untouched; mutually exclusive via busy(). */
static struct {
	bool     active;
	bool     truncated;
	bool     wrapped;       /* placed at LOG_START (decided at begin) */
	uint32_t addr;         /* header location (plan)                */
	uint32_t ts;           /* run timestamp (set at begin, reused)  */
	uint32_t written;      /* payload bytes committed               */
	uint32_t crc;
	/* 256 B RAM staging; flushed to flash on fill. STATIC (stack
	 * discipline — the 09-26 MPU lesson). Producer-paced: at the doc
	 * average (8 B @ 100 Hz) a flush lands every ~3.2 s. */
	uint8_t  buf[256];
	uint16_t fill;
} srec;

bool store_busy(void)
{
	return rec_state != REC_IDLE || srec.active;
}

/* Does [a1,a2) intersect [b1,b2)? (Mirror of the host-test helper.) */
static bool overlaps(uint32_t a1, uint32_t a2, uint32_t b1, uint32_t b2)
{
	return a1 < b2 && b1 < a2;
}

/* RECLAMATION: drop oldest entries whose extents intersect the erase
 * segments — the entire garbage policy, mirroring the host test. */
static void reclaim_victims(const struct runway_plan *p)
{
	while (entry_count > 0 &&
	       (overlaps(entries[0].addr,
			 entries[0].addr + run_span(entries[0].payload_len),
			 p->s1_from, p->s1_to) ||
		overlaps(entries[0].addr,
			 entries[0].addr + run_span(entries[0].payload_len),
			 p->s2_from, p->s2_to))) {
		printk("rec: reclaim id=%u (%lu B, oldest)\n",
		       entries[0].run_id,
		       (unsigned long)entries[0].payload_len);
		memmove(&entries[0], &entries[1],
			(entry_count - 1) * sizeof(entries[0]));
		entry_count--;
	}
}

/* Enter REC_TOPUP: plan the top-up, reclaim victims inside it, walk
 * one sector per pass. Zero work → straight back to IDLE. */
static void schedule_topup(const char *why)
{
	runway_plan_topup(write_cursor, erased_hi, erased_lo,
			  runway_target(run_budget), &prep.plan);

	if (prep.plan.sectors == 0) {
		rec_state = REC_IDLE;
		return;
	}

	reclaim_victims(&prep.plan);

	prep.seg = 0;
	prep.from = prep.plan.s1_from;
	prep.to = prep.plan.s1_to;
	if (prep.from >= prep.to) {
		/* segment 1 already clean — start at the bottom reserve */
		prep.seg = 1;
		prep.from = prep.plan.s2_from;
		prep.to = prep.plan.s2_to;
	}

	printk("prep: %s — %u sectors (cursor 0x%lx hi 0x%lx lo 0x%lx)\n",
	       why, (unsigned)prep.plan.sectors,
	       (unsigned long)write_cursor,
	       (unsigned long)erased_hi, (unsigned long)erased_lo);

	rec_state = REC_TOPUP;
	k_work_schedule(&rec_work, K_NO_WAIT);
}

static void rec_work_handler(struct k_work *w)
{
	switch (rec_state) {
	case REC_TOPUP: {
		/* One sector per pass. Segments are walked in ring order
		 * (s1 toward LOG_END, then the bottom reserve s2); the
		 * frontiers are committed as each segment completes. */
		uint32_t from = prep.from;

		if (from < prep.to) {
			flash_erase(flashdev, from, LOG_SECTOR_SIZE);
			prep.from = from + LOG_SECTOR_SIZE;
			k_work_schedule(&rec_work, K_MSEC(1));
			return;
		}

		if (prep.seg == 0) {
			erased_hi = prep.plan.s1_to;
			prep.seg = 1;
			prep.from = prep.plan.s2_from;
			prep.to = prep.plan.s2_to;
			if (prep.from < prep.to) {
				k_work_schedule(&rec_work, K_MSEC(1));
				return;
			}
			/* s2 already clean — fall through to finish */
		} else {
			erased_lo = prep.plan.s2_to;
		}

		/* Coverage reached U(cursor) — persist (crash window (b)
		 * closes here) and go idle. */
		persist_index();
		printk("prep: runway ready (hi=0x%lx lo=0x%lx)\n",
		       (unsigned long)erased_hi, (unsigned long)erased_lo);
		rec_state = REC_IDLE;
		return;
	}

	case REC_WRITING: {
		/* First pass: the 16 B run header at the record start.
		 * (SGL1 wrote it at the erase-phase exit; the SGL2 rework
		 *  moved erase out of the record path and DROPPED this write
		 *  — caught by stream readback 09-28: all-FF header, CRC still
		 *  green because it covers payload only. Fixed in v0.8.1.) */
		if (rec.written == 0) {
			struct run_header hdr = {
				.format_ver = 2, .arm_side = rec.side,
				.ts_utc = rec.ts, .baro_temp = 125,
				.data_size = 0, .frame_count = 0,
				.cal_accuracy = 3, ._pad = 0,
			};

			flash_write(flashdev, (off_t)rec.addr, &hdr, sizeof(hdr));
		}

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

	/* NOTE (bug 09-26, fixed v0.7.4): BLOB_OVERHEAD (22) is SIZE math
	 * (header+trailer). Byte OFFSETS use sizeof(run_header) — the 4b
	 * producer wrote payload at +22 instead of +16, pushing the trailer
	 * 6 bytes past the payload end. Size vs offset: never mix them. */
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

		persist_index();          /* crash window: entry committed */
		printk("rec: done id=%u payload=%lu total=%lu runs=%u\n",
		       (unsigned)(next_run_id - 1),
		       (unsigned long)rec.payload_len,
		       (unsigned long)(BLOB_OVERHEAD + rec.payload_len),
		       entry_count);

		/* SGL2: right after the run, free space for a max-length
		 * run. Wrap transition first: the bottom reserve becomes
		 * the runway (it was pre-erased for exactly this). */
		if (rec.wrapped) {
			erased_hi = erased_lo;
			erased_lo = LOG_START;
		}
		schedule_topup("post-run");
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
	    payload_len + BLOB_OVERHEAD > run_budget) {
		return false;
	}

	uint32_t span = run_span(payload_len);

	/* Plan the address (wrap rule: never straddle LOG_END). */
	bool wrapped;
	uint32_t addr = next_run_addr(write_cursor, span, &wrapped);

	/* SGL2: no erase and no reclamation at start — the runway was
	 * topped up right after the PREVIOUS run. Defense in depth:
	 * verify coverage and fail LOUDLY if the invariant broke. */
	if ((!wrapped && addr + span > erased_hi) ||
	    (wrapped && LOG_START + span > erased_lo)) {
		printk("ERROR: runway invariant violated "
		       "(cursor 0x%lx hi 0x%lx lo 0x%lx)\n",
		       (unsigned long)write_cursor,
		       (unsigned long)erased_hi,
		       (unsigned long)erased_lo);
		return false;
	}

	rec.addr = addr;
	rec.span = span;
	rec.payload_len = payload_len;
	rec.written = 0;
	rec.crc = 0xFFFFFFFFu;
	rec.ts = next_run_ts();
	rec.side = (uint8_t)(next_run_id & 1);   /* alternate L/R for bench */
	rec.wrapped = wrapped;

	printk("rec: start %lu B%s at 0x%lx (hi 0x%lx lo 0x%lx)\n",
	       (unsigned long)payload_len, wrapped ? " [wrap]" : "",
	       (unsigned long)addr,
	       (unsigned long)erased_hi, (unsigned long)erased_lo);

	rec_state = REC_WRITING;
	k_work_schedule(&rec_work, K_NO_WAIT);
	return true;
}

/* ── Streaming append API (M3 producer path) ────────────────────── */

static void srec_flush(void)
{
	if (srec.fill == 0) {
		return;
	}
	flash_write(flashdev,
		    (off_t)(srec.addr + sizeof(struct run_header)
			    + srec.written),
		    srec.buf, srec.fill);
	srec.written += srec.fill;
	srec.fill = 0;
}

bool store_record_begin(void)
{
	if (store_busy()) {
		return false;
	}

	/* Plan: wrap rule uses the budget-sized TARGET as the fit test
	 * (conservative vs. dynamic wrap — rejected 2026-09-28). The
	 * coverage guard then guarantees a FULL budget run fits. */
	uint32_t target = runway_target(run_budget);
	bool wrapped;
	uint32_t addr = next_run_addr(write_cursor, target, &wrapped);

	if ((!wrapped && addr + target > erased_hi) ||
	    (wrapped && LOG_START + target > erased_lo)) {
		printk("ERROR: runway invariant violated at begin "
		       "(hi 0x%lx lo 0x%lx)\n",
		       (unsigned long)erased_hi,
		       (unsigned long)erased_lo);
		return false;
	}

	/* Header now (the v0.8.1 lesson: header write is explicit). */
	uint32_t ts0 = next_run_ts();

	srec.ts = ts0;
	struct run_header hdr = {
		.format_ver = 2,
		.arm_side = (uint8_t)(next_run_id & 1),
		.ts_utc = ts0,
		.baro_temp = 125,
		.data_size = 0, .frame_count = 0,
		.cal_accuracy = 3, ._pad = 0,
	};

	flash_write(flashdev, (off_t)addr, &hdr, sizeof(hdr));

	srec.active = true;
	srec.truncated = false;
	srec.wrapped = wrapped;
	srec.addr = addr;
	srec.written = 0;
	srec.fill = 0;
	srec.crc = 0xFFFFFFFFu;
	printk("rec: stream begin at 0x%lx (budget %lu B)\n",
	       (unsigned long)addr, (unsigned long)run_budget);
	return true;
}

int32_t store_record_append(const uint8_t *src, uint32_t len)
{
	if (!srec.active) {
		return -1;
	}

	uint32_t room = run_budget - srec.written - srec.fill;
	uint32_t take = (len < room) ? len : room;

	if (take < len) {
		srec.truncated = true;   /* budget cap hit: caller must end */
	}
	for (uint32_t i = 0; i < take; i++) {
		srec.buf[srec.fill] = src[i];
		srec.crc = crc32_byte(srec.crc, src[i]);
		if (++srec.fill == sizeof(srec.buf)) {
			srec_flush();
		}
	}
	return (int32_t)take;
}

bool store_record_end(void)
{
	if (!srec.active) {
		return false;
	}

	srec_flush();

	/* Trailer at 16 + payload (frozen wire format). */
	uint32_t crc = srec.crc ^ 0xFFFFFFFFu;
	uint8_t trailer[6] = { 0xC3, 0x32,
			       (uint8_t)crc, (uint8_t)(crc >> 8),
			       (uint8_t)(crc >> 16),
			       (uint8_t)(crc >> 24) };

	flash_write(flashdev,
		    (off_t)(srec.addr + sizeof(struct run_header)
			    + srec.written),
		    trailer, sizeof(trailer));

	/* Index entry — cal_accuracy bit 7 = truncated (JP 2026-09-29).
	 * ENTRY only: the run HEADER cannot carry the flag (NOR cannot
	 * SET bits after write — header cal stays 3). The entry is RAM +
	 * index-sector state written at close, where the flag is set
	 * BEFORE the sector rewrite — verified live (ledger 2026-09-30).
	 * App-facing truncated display = future protocol extension. */
	if (entry_count == MAX_INDEXED_RUNS) {
		memmove(&entries[0], &entries[1],
			(entry_count - 1) * sizeof(entries[0]));
		entry_count--;
	}
	entries[entry_count] = (struct sgl_entry){
		.addr = srec.addr,
		.payload_len = srec.written,
		.ts_utc = srec.ts,
		.run_id = next_run_id++,
		.arm_side = (uint8_t)((next_run_id - 1) & 1),
		.cal_accuracy = (uint8_t)(3 | (srec.truncated ? 0x80 : 0)),
	};
	entry_count++;

	uint32_t span = run_span(srec.written);

	write_cursor = srec.addr + span;

	printk("rec: stream done payload=%lu total=%lu%s runs=%u\n",
	       (unsigned long)srec.written,
	       (unsigned long)(BLOB_OVERHEAD + srec.written),
	       srec.truncated ? " [TRUNCATED]" : "", entry_count);

	srec.active = false;
	persist_index();

	/* Wrap transition (shared semantics with the 'w' path): the
	 * bottom reserve becomes the runway. */
	if (srec.wrapped) {
		erased_hi = (erased_lo > LOG_START) ? erased_lo : LOG_START;
		erased_lo = LOG_START;
	}
	schedule_topup("post-run");
	return true;
}

/* ── Active run budget (SGL3) ──────────────────────────────────────
 * Discipline-driven: the app maps SL 90 s / GS+SG 120 s / DH 150 s /
 * Custom 60–600 s → bytes at 1 KB/s and pushes the value here. The
 * device only validates bytes (1 sector's worth .. MAX_RUN_BUDGET).
 * Changing the budget re-tops the runway when idle — the planner is
 * fully parametric, nothing else moves. */
bool store_set_run_budget(uint32_t payload_bytes)
{
	if (payload_bytes < LOG_SECTOR_SIZE - BLOB_OVERHEAD ||
	    payload_bytes > MAX_RUN_BUDGET) {
		return false;
	}
	if (payload_bytes == run_budget) {
		return true;
	}
	run_budget = payload_bytes;
	if (store_busy()) {
		/* No re-top now (recording/top-up in flight); the next
		 * top-up after the current cycle derives the new target
		 * automatically. Persist so a crash keeps the setting. */
		persist_index();
		return true;
	}
	persist_index();
	schedule_topup("budget-change");
	return true;
}

uint32_t store_run_budget(void)
{
	return run_budget;
}

/* Athlete-facing capacity estimate: how many FULL budget runs still
 * fit without evicting anything live. Free space = what's left after
 * the live set and the standing reserve. */
uint32_t store_free_full_runs(void)
{
	uint32_t target = runway_target(run_budget);
	uint32_t live = 0;

	for (uint16_t i = 0; i < entry_count; i++) {
		live += run_span(entries[i].payload_len);
	}
	if (live + target > LOG_SIZE) {
		return 0;
	}
	return (LOG_SIZE - live - target) / target;
}

/* ════════════════════════════════════════════════════════════════════
 * BOOT
 * ════════════════════════════════════════════════════════════════════ */

/* Crash window (c) healer: a mid-record crash programs bytes into
 * what the persisted index claims is an erased runway. Sniff the
 * first bytes of each claimed region — anything not 0xFF resets the
 * frontier and forces a full re-top-up at boot. */
static void verify_runway(void)
{
	static uint8_t sniff[64];
	bool dirty = false;

	flash_read(flashdev, (off_t)write_cursor, sniff, sizeof(sniff));
	for (uint32_t i = 0; i < sizeof(sniff); i++) {
		if (sniff[i] != 0xFF) { dirty = true; break; }
	}
	if (!dirty && erased_lo > LOG_START) {
		flash_read(flashdev, (off_t)LOG_START, sniff, sizeof(sniff));
		for (uint32_t i = 0; i < sizeof(sniff); i++) {
			if (sniff[i] != 0xFF) { dirty = true; break; }
		}
	}
	if (dirty) {
		printk("store: stale partial record — runway reset\n");
		erased_hi = write_cursor;
		erased_lo = LOG_START;
	}
}

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

	k_work_init_delayable(&rec_work, rec_work_handler);

	/* Load the log index — or start fresh. Fresh means: cursor at
	 * LOG_START, nothing erased yet; the boot top-up below erases
	 * the first RUNWAY_TARGET sectors (covers dirty flash after a
	 * corrupt/absent index). Legacy slot-era data is consumed by
	 * reclamation as the log grows — no bulk erase, no boot delay. */
	if (!load_index()) {
		write_cursor = LOG_START;
		erased_hi = LOG_START;
		erased_lo = LOG_START;
		entry_count = 0;
		next_run_id = 0;
		printk("store: fresh log (legacy slot data reclaims lazily)\n");
		persist_index();
	} else {
		verify_runway();
		printk("store: %u runs, cursor 0x%lx, runway hi 0x%lx lo 0x%lx, budget %lu B\n",
		       entry_count, (unsigned long)write_cursor,
		       (unsigned long)erased_hi,
		       (unsigned long)erased_lo,
		       (unsigned long)run_budget);
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

	/* SGL2: boot = one of the two moments the runway is topped up
	 * (the other: right after each run). Background work — the
	 * first command during it gets a busy refusal (retry). */
	schedule_topup("boot");
}

/* ── B4 step-0: ring drain-load simulation ─────────────────────────
 * Mirrors the intended pre-roll architecture: page-buffered 20 B slot
 * writes, forward fill, no erase while logging. Runs EVERY logging
 * tick (real drain only lasts ~10 s) — deliberately harsher. */
#define RING_SIM_BASE   0x00000u
#define RING_SIM_BYTES  (5u * 4096u)
#define RING_SIM_PAGE   256u
static struct {
	uint8_t  buf[RING_SIM_PAGE];
	uint16_t fill;
	uint32_t cursor;       /* next flash byte */
	uint32_t pages;
	uint32_t flush_ms_max;
} rsim;

void store_ring_sim_reset(void)
{
	memset(&rsim, 0, sizeof(rsim));
	for (uint32_t a = RING_SIM_BASE; a < RING_SIM_BASE + RING_SIM_BYTES;
	     a += 4096u) {
		flash_erase(flashdev, a, 4096u);
	}
	printk("ring_sim: erased %u B\n", RING_SIM_BYTES);
}

static void rsim_flush(void)
{
	uint32_t t0 = k_uptime_get_32();

	flash_write(flashdev, RING_SIM_BASE + rsim.cursor, rsim.buf,
		    RING_SIM_PAGE);
	uint32_t dt = k_uptime_get_32() - t0;

	if (dt > rsim.flush_ms_max) {
		rsim.flush_ms_max = dt;
	}
	rsim.cursor += RING_SIM_PAGE;
	rsim.pages++;
	rsim.fill = 0;
	memset(rsim.buf, 0xFF, sizeof(rsim.buf));
	if (rsim.cursor + RING_SIM_PAGE > RING_SIM_BYTES) {
		rsim.cursor = 0;   /* wrap (sim only; real ring wraps per slot) */
	}
}

void store_ring_sim_tick(void)
{
	static uint8_t rdiscard[20];

	/* 2 pops = 2 x 20 B reads (bus load, results discarded) */
	flash_read(flashdev, RING_SIM_BASE + rsim.cursor, rdiscard, 20);
	flash_read(flashdev, RING_SIM_BASE + rsim.cursor + 20, rdiscard, 20);
	/* 1 push = 20 B write into the page buffer (3 writes total per
	 * tick incl. the 2 pop-replays a real drain would append) */
	for (int i = 0; i < 3; i++) {
		if (rsim.fill + 20 > RING_SIM_PAGE) {
			rsim_flush();
		}
		/* pattern: counter so readback is verifiable */
		*(uint32_t *)&rsim.buf[rsim.fill] = rsim.cursor + rsim.fill;
		rsim.fill += 20;
	}
}

uint32_t store_ring_sim_flush_ms_max(void) { return rsim.flush_ms_max; }
uint32_t store_ring_sim_pages(void) { return rsim.pages; }

void store_wipe(void)
{
	if (store_busy()) {
		return;
	}
	flash_erase(flashdev, INDEX_ADDR, LOG_SECTOR_SIZE);
	write_cursor = LOG_START;
	erased_hi = LOG_START;
	erased_lo = LOG_START;
	entry_count = 0;
	next_run_id = 0;
	printk("store: wiped (budget %lu B kept)\n",
	       (unsigned long)run_budget);
	schedule_topup("post-wipe");
}
