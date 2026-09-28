/*
 * test_layout.c — host-side tests for the circular run log (SGL2)
 * ==================================================================
 * THE FOUNDING SUITE. Compiles on the PC (msvcrt/gcc), never touches
 * hardware, and proves the store's arithmetic BEFORE it meets flash.
 * Each test names the bug class it prevents.
 *
 * SGL2 (2026-09-28): erase policy is TOP-UP for a max-length run
 * right after each run (run length is unknown at start — JP). The
 * flagship is test 5: a bit-level sector simulation that proves
 * EVERY programmed byte lands in an erased sector, wraps included —
 * the composed-lifecycle proof whose absence let the v0.7.x wrap bug
 * ship (erase_from > erase_to → zero sectors erased → programming
 * over first-lap data; ledger 2026-09-28).
 *
 * Build & run (from the app folder):
 *   gcc -Wall -Wextra -Isrc tests/test_layout.c -o build/test_layout
 *   ./build/test_layout
 * Exit code 0 = all green. Any failure prints the line and aborts.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "store_layout.h"

static int failures = 0;

#define CHECK(cond, name) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (name)); \
		failures++; \
		return 1; \
	} \
	printf("ok   %s\n", (name)); \
} while (0)

/* ── 1. Span padding ────────────────────────────────────────────────
 * Prevents: off-sector run starts (would make reclamation ambiguous). */
static int test_span_padding(void)
{
	CHECK(run_span(0)       == LOG_SECTOR_SIZE,   "empty run still pads to one sector");
	CHECK(run_span(1)       == LOG_SECTOR_SIZE,   "1-byte run = one sector");
	CHECK(run_span(4074)    == LOG_SECTOR_SIZE,   "4074 B (22+4074=4096) exactly one sector");
	CHECK(run_span(4075)    == 2 * LOG_SECTOR_SIZE, "one byte over = two sectors");
	CHECK(run_span(39350)   == 10 * LOG_SECTOR_SIZE, "typical 39350 B payload = 10 sectors (40980 B)");
	return 0;
}

/* ── 2. Wrap boundary ────────────────────────────────────────────────
 * Prevents: runs straddling LOG_END (blob reads are linear). */
static int test_wrap(void)
{
	bool wrapped = true;
	uint32_t span = 5 * LOG_SECTOR_SIZE;
	uint32_t cursor = LOG_END - span;

	CHECK(next_run_addr(cursor, span, &wrapped) == cursor && !wrapped,
	      "exact fit at region end: no wrap");
	CHECK(next_run_addr(cursor, span + 1, &wrapped) == LOG_START && wrapped,
	      "one byte over: wraps to LOG_START");
	CHECK(next_run_addr(LOG_START, run_span(1), &wrapped) == LOG_START && !wrapped,
	      "first run at base: no wrap");
	CHECK(next_run_addr(LOG_END, run_span(1), &wrapped) == LOG_START && wrapped,
	      "cursor at LOG_END itself: wraps (edge after exact fit)");
	return 0;
}

/* ── 3. THE FOUNDING TEST: listing budgets fit the policy domain ────
 * Prevents: the 160-byte bug — buffers sized from test data instead
 * of MAX_INDEXED_RUNS × worst-case entry. */
static int test_listing_budget(void)
{
	char worst[96];
	int n = snprintf(worst, sizeof(worst),
			 "{\"id\":%u,\"ts\":%lu,\"size\":%lu,\"side\":\"right\"},",
			 (unsigned)(MAX_INDEXED_RUNS - 1),
			 4294967295UL, (unsigned long)MAX_RUN_BUDGET);

	CHECK(n > 0 && n < 96, "worst entry fits the local buffer");
	CHECK(MAX_INDEXED_RUNS * (uint32_t)n + 2 <= RUN_LIST_JSON_SIZE,
	      "MAX_INDEXED_RUNS worst-case entries fit the JSON buffer");
	CHECK(RUNWAY_TARGET < LOG_SIZE,
	      "runway target fits the region (policy sanity)");
	CHECK(RUNWAY_TARGET == 61 * LOG_SECTOR_SIZE,
	      "runway target = 61 sectors (244 KiB)");
	return 0;
}

/* ── 4. Runway top-up planning ───────────────────────────────────────
 * Prevents: the SGL1 wrap-composition bug (inverted erase ranges). */
static int test_runway_plan(void)
{
	struct runway_plan p;

	/* Cold log: everything from scratch → 61 sectors, one segment. */
	runway_plan_topup(LOG_START, LOG_START, LOG_START, &p);
	CHECK(p.s1_from == LOG_START && p.s1_to == LOG_START + RUNWAY_TARGET,
	      "cold: single segment [base, base+TARGET)");
	CHECK(p.s2_from == 0 && p.s2_to == 0, "cold: no bottom segment");
	CHECK(p.sectors == 61, "cold: 61 sectors of work");

	/* Delta: runway partially pre-erased → only the difference. */
	runway_plan_topup(LOG_START, LOG_START + 10 * LOG_SECTOR_SIZE,
			  LOG_START, &p);
	CHECK(p.s1_from == LOG_START + 10 * LOG_SECTOR_SIZE &&
	      p.sectors == 51, "delta: 51 sectors after 10 pre-erased");

	/* Steady state: coverage complete → zero work. */
	runway_plan_topup(LOG_START, LOG_START + RUNWAY_TARGET,
			  LOG_START, &p);
	CHECK(p.sectors == 0, "covered: zero work");

	/* Exact boundary: remaining == TARGET → single segment to LOG_END. */
	{
		uint32_t c = LOG_END - RUNWAY_TARGET;

		runway_plan_topup(c, c, LOG_START, &p);
		CHECK(p.s1_to == LOG_END && p.s2_to == 0 && p.sectors == 61,
		      "remaining==TARGET: single segment to LOG_END");
	}

	/* One sector INSIDE the target: remaining < TARGET → two-segment. */
	{
		uint32_t c = LOG_END - RUNWAY_TARGET + LOG_SECTOR_SIZE;

		runway_plan_topup(c, c, LOG_START, &p);
		CHECK(p.s1_from == c && p.s1_to == LOG_END,
		      "inside target: segment 1 sweeps the whole stub");
		CHECK(p.s2_from == LOG_START &&
		      p.s2_to == LOG_START + RUNWAY_TARGET,
		      "inside target: segment 2 reserves the bottom");
		CHECK(p.sectors == 60 + 61,
		      "inside target: stub (60) + bottom (61)");
	}

	/* One sector shy of the end but runway still fits → single seg. */
	{
		uint32_t c = LOG_END - RUNWAY_TARGET - LOG_SECTOR_SIZE;

		runway_plan_topup(c, c, LOG_START, &p);
		CHECK(p.s1_to == c + RUNWAY_TARGET && p.s2_to == 0,
		      "remaining>target: still single segment (no reserve)");
	}

	/* Bottom partially reserved: segment 2 starts at lo. */
	{
		uint32_t c = LOG_END - RUNWAY_TARGET + LOG_SECTOR_SIZE;

		runway_plan_topup(c, c, LOG_START + 20 * LOG_SECTOR_SIZE, &p);
		CHECK(p.s2_from == LOG_START + 20 * LOG_SECTOR_SIZE &&
		      p.sectors == 60 + 41,
		      "partial bottom: 41 remaining + stub (60)");
	}

	/* hi beyond the target (post-advance state): no negative work. */
	{
		uint32_t c = LOG_START + 5 * LOG_SECTOR_SIZE;

		runway_plan_topup(c, c + RUNWAY_TARGET, LOG_START, &p);
		CHECK(p.sectors == 0, "hi past target: zero, no underflow");
	}
	return 0;
}

/* ── 5. FULL SIMULATION: erase-before-program, wraps included ──────
 * Prevents: the v0.7.x class — composed policies that look right in
 * isolation but program unerased flash once the log rotates. This is
 * the store's DECISION LAYER, mirrored line for line: same planner,
 * same wrap rule, same reclaim rule, plus a per-sector erase bitmap
 * as ground truth. If firmware and simulation ever disagree, the bug
 * is in the translation, and this file is the reference. */
#define NSECT  (LOG_SIZE / LOG_SECTOR_SIZE)

struct sim_run { uint32_t addr; uint32_t span; };

static uint8_t erased[NSECT];          /* ground truth per sector    */
static struct sim_run ring[MAX_INDEXED_RUNS];
static uint16_t count;
static uint32_t sim_cursor, sim_hi, sim_lo;

static uint32_t sec_of(uint32_t addr)
{
	return (addr - LOG_START) / LOG_SECTOR_SIZE;
}

static bool overlaps(uint32_t a1, uint32_t a2, uint32_t b1, uint32_t b2)
{
	return a1 < b2 && b1 < a2;
}

static int sim_fail(const char *what, uint32_t run_no)
{
	printf("FAIL(sim) run %u: %s (cursor=0x%x hi=0x%x lo=0x%x)\n",
	       run_no, what, sim_cursor, sim_hi, sim_lo);
	failures++;
	return 1;
}

/* One full record cycle: plan → coverage guard → program → index →
 * wrap transition → top-up (reclaim + erase) → coverage targets. */
static int sim_record(uint32_t payload, uint32_t run_no)
{
	uint32_t span = run_span(payload);
	bool wrapped;
	uint32_t addr = next_run_addr(sim_cursor, span, &wrapped);
	struct runway_plan p;

	/* THE guard: program only into known-erased sectors. */
	if (!wrapped && sim_cursor + span > sim_hi) {
		return sim_fail("program past hi (coverage)", run_no);
	}
	if (wrapped && LOG_START + span > sim_lo) {
		return sim_fail("wrapped program past lo (coverage)", run_no);
	}

	/* Program the sectors — must all be erased, then dirty. */
	for (uint32_t a = addr; a < addr + span; a += LOG_SECTOR_SIZE) {
		if (!erased[sec_of(a)]) {
			return sim_fail("PROGRAM INTO UNERASED SECTOR", run_no);
		}
		erased[sec_of(a)] = 0;
	}

	/* Index: append at age-ordered tail; cap drops the oldest. */
	if (count == MAX_INDEXED_RUNS) {
		memmove(&ring[0], &ring[1], (count - 1) * sizeof(ring[0]));
		count--;
	}
	ring[count].addr = addr;
	ring[count].span = span;
	count++;
	sim_cursor = addr + span;

	/* Close: wrap transition, then the SGL2 top-up. */
	if (wrapped) {
		sim_hi = sim_lo;
		sim_lo = LOG_START;
	}
	runway_plan_topup(sim_cursor, sim_hi, sim_lo, &p);

	/* Reclaim: victims intersect the erase segments (oldest first). */
	while (count > 0 &&
	       (overlaps(ring[0].addr, ring[0].addr + ring[0].span,
			 p.s1_from, p.s1_to) ||
		overlaps(ring[0].addr, ring[0].addr + ring[0].span,
			 p.s2_from, p.s2_to))) {
		memmove(&ring[0], &ring[1], (count - 1) * sizeof(ring[0]));
		count--;
	}

	/* Erase the planned sectors. */
	for (uint32_t a = p.s1_from; a < p.s1_to; a += LOG_SECTOR_SIZE) {
		erased[sec_of(a)] = 1;
	}
	for (uint32_t a = p.s2_from; a < p.s2_to; a += LOG_SECTOR_SIZE) {
		erased[sec_of(a)] = 1;
	}

	/* Coverage targets after the top-up. */
	if (LOG_END - sim_cursor >= RUNWAY_TARGET) {
		sim_hi = sim_cursor + RUNWAY_TARGET;
	} else {
		sim_hi = LOG_END;
		sim_lo = LOG_START + RUNWAY_TARGET;
	}

	/* Invariants, every record:
	 *  (a) runway coverage really is erased;
	 *  (b) every live entry lives in programmed (dirty) sectors. */
	for (uint32_t a = sim_cursor; a < sim_hi; a += LOG_SECTOR_SIZE) {
		if (!erased[sec_of(a)]) {
			return sim_fail("runway sector not erased", run_no);
		}
	}
	if (sim_lo > LOG_START) {
		for (uint32_t a = LOG_START; a < sim_lo; a += LOG_SECTOR_SIZE) {
			if (!erased[sec_of(a)]) {
				return sim_fail("bottom reserve not erased",
						run_no);
			}
		}
	}
	for (uint16_t i = 0; i < count; i++) {
		for (uint32_t a = ring[i].addr;
		     a < ring[i].addr + ring[i].span;
		     a += LOG_SECTOR_SIZE) {
			if (erased[sec_of(a)]) {
				return sim_fail("live entry on erased flash",
						run_no);
			}
		}
	}
	if (sim_cursor > LOG_END) {
		return sim_fail("cursor past LOG_END", run_no);
	}
	return 0;
}

static void sim_reset(void)
{
	memset(erased, 0, sizeof(erased));
	count = 0;
	sim_cursor = sim_hi = sim_lo = LOG_START;
}

static int test_lifecycle(void)
{
	uint32_t run_no = 0;

	/* Cold start: the boot top-up precedes any record. */
	sim_reset();
	{
		struct runway_plan p;

		runway_plan_topup(sim_cursor, sim_hi, sim_lo, &p);
		for (uint32_t a = p.s1_from; a < p.s1_to; a += LOG_SECTOR_SIZE) {
			erased[sec_of(a)] = 1;
		}
		for (uint32_t a = p.s2_from; a < p.s2_to; a += LOG_SECTOR_SIZE) {
			erased[sec_of(a)] = 1;
		}
		sim_hi = sim_cursor + RUNWAY_TARGET;
	}

	/* Typical 50 s training runs, long past rotation (200 laps). */
	for (int i = 0; i < 200; i++, run_no++) {
		if (sim_record(39350, run_no)) { return 1; }
	}
	printf("     typical x200: live=%u\n", count);
	{
		uint32_t floor_cap = (LOG_SIZE - RUNWAY_TARGET)
				     / run_span(39350);

		CHECK(count >= floor_cap - 1 && count <= floor_cap + 1,
		      "typical-run live count == capacity minus standing runway (±1)");
	}

	/* Continue with max-budget runs from mid-rotation: forces wraps
	 * with the LEAST bottom margin — the original bug's habitat. */
	for (int i = 0; i < 12; i++, run_no++) {
		if (sim_record(MAX_RUN_BUDGET - BLOB_OVERHEAD, run_no)) {
			return 1;
		}
	}
	printf("     +max x12: live=%u\n", count);
	CHECK(count <= (LOG_SIZE - RUNWAY_TARGET)
		      / run_span(MAX_RUN_BUDGET - BLOB_OVERHEAD) + 1,
	      "max-budget live set within runway-adjusted region");

	/* Adversarial: random mix, deterministic seed, incl. runs sized
	 * to land exactly on the LOG_END boundary. */
	{
		uint32_t seed = 0x5EEDC0DEu;
		for (int i = 0; i < 500; i++, run_no++) {
			uint32_t r = seed = seed * 1664525u + 1013904223u;
			uint32_t payload;

			if (i % 37 == 0) {
				/* Boundary hunter: fill exactly to LOG_END. */
				uint32_t remaining = LOG_END - sim_cursor;
				uint32_t fit = (remaining >= BLOB_OVERHEAD + 1)
					? run_span(remaining - BLOB_OVERHEAD - 1)
					: run_span(1);
				payload = fit - BLOB_OVERHEAD;
				if (payload > MAX_RUN_BUDGET - BLOB_OVERHEAD) {
					payload = MAX_RUN_BUDGET
					          - BLOB_OVERHEAD;
				}
			} else if (i % 5 == 0) {
				payload = MAX_RUN_BUDGET - BLOB_OVERHEAD;
			} else {
				payload = 1 + (r % (MAX_RUN_BUDGET
						    - BLOB_OVERHEAD));
			}
			if (sim_record(payload, run_no)) { return 1; }
		}
	}
	printf("     +random x500: live=%u total=%u\n", count, run_no);
	CHECK(count <= MAX_INDEXED_RUNS, "index cap respected throughout");
	return 0;
}

int main(void)
{
	printf("SGC store layout host tests (SGL2)\n");
	printf("==================================\n");
	if (test_span_padding())   { return 1; }
	if (test_wrap())           { return 1; }
	if (test_listing_budget()) { return 1; }
	if (test_runway_plan())    { return 1; }
	if (test_lifecycle())      { return 1; }
	printf("==================================\n");
	printf("ALL GREEN (%d failures)\n", failures);
	return failures ? 1 : 0;
}
