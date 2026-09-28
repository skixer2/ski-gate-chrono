/*
 * store_layout.h — circular run log: the pure arithmetic (step 4b)
 * ================================================================
 * THE shared brain of the store. Everything here is PURE MATH on
 * constants and state values — no hardware, no Zephyr calls — so the
 * exact same code compiles into the firmware AND into the host tests
 * (tests/test_layout.c). If the wrap or reclaim arithmetic is wrong,
 * the tests catch it on the PC before it ever meets your flash.
 *
 * ── The design (why it looks like this) ────────────────────────────
 *
 * The old Opt-A store gave each run a fixed 244 KiB slot: bulletproof,
 * but a typical 50 s run (~40 KB) used one sixth of its slot and the
 * rest sat idle. JP's packing insight + his requirement for retained
 * history ("dead phone, more than 8 slalom runs") led here:
 *
 * ┌──────────────────────── 1.9 MB data region ────────────────────────┐
 * │ [run1][pad][run2][pad]……[runN][pad][ erased runway …… ← erase_end ]│
 * │  oldest ─────────────────────────────→ write_cursor                │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 *   • Runs pack sequentially, each padded UP to a 4 KiB erase sector
 *     (average waste ~2 KB per run — noise against 40 KB payloads).
 *   • Reclamation happens ONLY at the head: when the erased runway
 *     runs low, we erase the OLDEST run's padded extent. Because runs
 *     are contiguous and oldest-first, this can never fragment —
 *     there is no garbage collection, ever. (This is the property
 *     that makes a packed log safe on NOR flash.)
 *   • A run never wraps mid-run (blob reads are single linear
 *     flash_reads). If a run would cross the region end, we skip to
 *     the start (leaving a bounded "stub" that later joins the runway).
 *   • The hot recording path stays PROGRAM-ONLY: erases happen while
 *     preparing (POST_RUN cooldown / boot), never during descent.
 *
 * ── The policy constants (dimensioned per the standing rule:
 *    worst case of the production domain, never today's test data) ──
 *
 *   MAX_RUN_BUDGET   244 KiB — the largest run we ever record. Parity
 *                    with the old slot size; enforced by append refusing
 *                    writes past it. Prevents one pathological recording
 *                    from eating the entire runway.
 *   MAX_INDEXED_RUNS 64     — how many live runs the index/directory can
 *                    track. NOT geometry: region ÷ minimum-run-size is
 *                    unbounded (a 1 KB run is legal), so this is an
 *                    explicit policy number. All listing buffers are
 *                    sized FROM this constant (see the founding test).
 *   Capacity check:  1.9 MB ÷ (40 KB + ~2 KB pad) ≈ 45 typical runs —
 *                    and 45 < 64, so typical operation never even hits
 *                    the index cap. The cap exists for many-tiny-runs.
 */
#ifndef SGC_STORE_LAYOUT_H
#define SGC_STORE_LAYOUT_H

#include <stdint.h>
#include <stdbool.h>

/* ── Flash geometry (Opt-A map, classic 2 MB — unchanged from 4a) ── */
#define LOG_SECTOR_SIZE   4096u      /* proven: selftest 4K erase OK   */
#define LOG_START         0x14000u   /* data region base (after pre-roll) */
#define LOG_END           0x1FC000u  /* config region starts here      */
#define LOG_SIZE          (LOG_END - LOG_START)

/* ── Policy constants (see header essay above) ────────────────────── */
#define MAX_RUN_BUDGET    0x3D000u   /* 244 KiB — old slot size, now a cap */
#define MAX_INDEXED_RUNS  64u

/* ── Runway policy (SGL2, JP 2026-09-28) ──────────────────────────
 * Run length is UNKNOWN at record start (an athlete's descent — or a
 * future streaming sensor producer). So we never size erase work for
 * the incoming record. Instead, right AFTER a run (and at boot/wipe)
 * we free enough space for a MAXIMUM-length run:
 *
 *   coverage U(cursor) = [cursor, cursor+RUNWAY_TARGET)            when it fits
 *                      = [cursor, LOG_END) ∪ [LOG_START,
 *                        LOG_START+RUNWAY_TARGET)                  near the end
 *
 * The second, two-segment form matters: a wrapping max-run needs
 * RUNWAY_TARGET bytes clean AT THE BOTTOM — a plain ring interval of
 * TARGET from a high cursor covers less than that (counter-example:
 * remaining=100K, span=244K → interval reaches 144K past LOG_START,
 * the run needs 244K). Plain absolute math only — NO ring arithmetic
 * anywhere in this header: ring-distance ambiguity is the bug class
 * that started SGL2 (ledger 2026-09-28).
 */
#define RUNWAY_TARGET     (((MAX_RUN_BUDGET) + LOG_SECTOR_SIZE - 1) \
                           / LOG_SECTOR_SIZE * LOG_SECTOR_SIZE)   /* 244 KiB */

/* ── Blob arithmetic (wire-format facts, frozen) ────────────────────
 * A stored run occupies: 16 B header + payload + 6 B CRC trailer,
 * then padding to the next 4 KiB sector boundary.
 */
#define BLOB_OVERHEAD     22u        /* RunHeader(16) + trailer(6)     */

static inline uint32_t run_span(uint32_t payload_len)
{
	/* Total flash bytes consumed by one run, sector-padded. */
	uint32_t raw = BLOB_OVERHEAD + payload_len;

	return (raw + LOG_SECTOR_SIZE - 1) / LOG_SECTOR_SIZE * LOG_SECTOR_SIZE;
}

/* ── Cursor movement (the heart of the log) ─────────────────────────
 * Where does the next run of `span` bytes start, given the current
 * write cursor? Handles the wrap rule: a run may not straddle LOG_END,
 * so if it doesn't fit in the remaining space we restart at LOG_START
 * and the leftover tail ("stub") is reported so the caller can erase
 * it into the runway. EXACT fit does NOT wrap (boundary case tested).
 */
static inline uint32_t next_run_addr(uint32_t cursor, uint32_t span,
				     bool *wrapped)
{
	if (cursor + span <= LOG_END) {
		if (wrapped) { *wrapped = false; }
		return cursor;
	}
	if (wrapped) { *wrapped = true; }
	return LOG_START;
}

/* ── Erase top-up planning (SGL2) ─────────────────────────────────
 * How much erase work until coverage reaches U(cursor), given the
 * two persisted frontiers:
 *   hi — erased end of the runway that starts AT the cursor
 *        (hi == cursor → nothing ahead is known-erased)
 *   lo — erased end of the bottom reserve [LOG_START, lo)
 *        (LOG_START → no bottom reserve)
 * Pure bookkeeping; the hardware loop lives in store_flash.c.
 * Segments are [from, to); an empty segment has from == to == 0.
 */
struct runway_plan {
	uint32_t s1_from, s1_to;   /* toward LOG_END                        */
	uint32_t s2_from, s2_to;   /* bottom reserve (0,0 if none)          */
	uint32_t sectors;          /* total sectors to erase                */
};

static inline void runway_plan_topup(uint32_t cursor, uint32_t hi,
				     uint32_t lo, struct runway_plan *p)
{
	uint32_t remaining = LOG_END - cursor;
	uint32_t target_hi = (remaining >= RUNWAY_TARGET)
			       ? cursor + RUNWAY_TARGET : LOG_END;

	p->s1_from = (hi > cursor) ? hi : cursor;
	p->s1_to = (target_hi > p->s1_from) ? target_hi : p->s1_from;

	p->s2_from = p->s2_to = 0;
	if (remaining < RUNWAY_TARGET) {
		uint32_t from = (lo > LOG_START) ? lo : LOG_START;
		uint32_t to = LOG_START + RUNWAY_TARGET;

		p->s2_from = from;
		p->s2_to = (to > from) ? to : from;
	}

	p->sectors = ((p->s1_to - p->s1_from) + LOG_SECTOR_SIZE - 1)
		     / LOG_SECTOR_SIZE
		   + ((p->s2_to - p->s2_from) + LOG_SECTOR_SIZE - 1)
		     / LOG_SECTOR_SIZE;
}

/* ── Directory-listing budgets (the founding test asserts these) ────
 * Worst-case ONE directory entry, character by character:
 *   {"id":63,"ts":4294967295,"size":249856,"side":"right"},
 * The sizes below are generous upper bounds, re-derived by the test.
 */
#define RUN_LIST_JSON_SIZE  4096     /* 64 × ~60 B + brackets, ample  */

#endif /* SGC_STORE_LAYOUT_H */
