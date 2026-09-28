/*
 * test_layout.c — host-side tests for the circular run log (SGL3)
 * ==================================================================
 * THE FOUNDING SUITE. Compiles on the PC (gcc), never touches
 * hardware, and proves the store's arithmetic BEFORE it meets flash.
 *
 * SGL2 (2026-09-28): erase policy = TOP-UP for a max-length run right
 * after each run; flagship = the bit-level sector simulation proving
 * every programmed byte lands in an erased sector (the composed proof
 * whose absence shipped the v0.7.x wrap bug).
 *
 * SGL3 (same day, JP GO): the run budget is RUNTIME state (discipline:
 * SL 90 s / GS-SG 120 s / DH 150 s / custom 60–600 s at 1 KB/s). The
 * runway target derives from the ACTIVE budget — so the whole suite
 * sweeps every discipline budget, and the capacity formula (athlete-
 * facing free-run estimate) is asserted too.
 *
 * Build & run (from the app folder):
 *   gcc -Wall -Wextra -Isrc tests/test_layout.c -o build/test_layout
 *   ./build/test_layout
 * Exit code 0 = all green.
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

/* ── 3. Listing budgets + runway targets per discipline ───────────── */
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

	/* Runway targets at 1 KB/s discipline budgets (SGL3). */
	CHECK(runway_target(92160u)   == 23  * LOG_SECTOR_SIZE,
	      "SL 90 KiB target = 23 sectors");
	CHECK(runway_target(122880u)  == 31  * LOG_SECTOR_SIZE,
	      "GS/SG 120 KiB target = 31 sectors");
	CHECK(runway_target(RUN_BUDGET_DEFAULT) == 38 * LOG_SECTOR_SIZE,
	      "DH default 150 KiB target = 38 sectors");
	CHECK(runway_target(614400u)  == 151 * LOG_SECTOR_SIZE,
	      "Custom 600 KiB target = 151 sectors");
	CHECK(runway_target(MAX_RUN_BUDGET) < LOG_SIZE,
	      "max-budget runway target fits the 2 MB region");
	return 0;
}

/* ── 4. Runway top-up planning (parametric target) ──────────────────
 * Prevents: the SGL1 wrap-composition bug (inverted erase ranges). */
static int test_runway_plan(void)
{
	struct runway_plan p;
	uint32_t T = 61 * LOG_SECTOR_SIZE;   /* classic 244 KiB-scale T */

	/* Cold log: everything from scratch → T sectors, one segment. */
	runway_plan_topup(LOG_START, LOG_START, LOG_START, T, &p);
	CHECK(p.s1_from == LOG_START && p.s1_to == LOG_START + T,
	      "cold: single segment [base, base+T)");
	CHECK(p.s2_from == 0 && p.s2_to == 0, "cold: no bottom segment");
	CHECK(p.sectors == 61, "cold: 61 sectors of work");

	/* Delta: runway partially pre-erased → only the difference. */
	runway_plan_topup(LOG_START, LOG_START + 10 * LOG_SECTOR_SIZE,
			  LOG_START, T, &p);
	CHECK(p.s1_from == LOG_START + 10 * LOG_SECTOR_SIZE &&
	      p.sectors == 51, "delta: 51 sectors after 10 pre-erased");

	/* Steady state: coverage complete → zero work. */
	runway_plan_topup(LOG_START, LOG_START + T, LOG_START, T, &p);
	CHECK(p.sectors == 0, "covered: zero work");

	/* Exact boundary: remaining == T → single segment to LOG_END. */
	{
		uint32_t c = LOG_END - T;

		runway_plan_topup(c, c, LOG_START, T, &p);
		CHECK(p.s1_to == LOG_END && p.s2_to == 0 && p.sectors == 61,
		      "remaining==T: single segment to LOG_END");
	}

	/* One sector INSIDE the target: remaining < T → two-segment. */
	{
		uint32_t c = LOG_END - T + LOG_SECTOR_SIZE;

		runway_plan_topup(c, c, LOG_START, T, &p);
		CHECK(p.s1_from == c && p.s1_to == LOG_END,
		      "inside target: segment 1 sweeps the whole stub");
		CHECK(p.s2_from == LOG_START && p.s2_to == LOG_START + T,
		      "inside target: segment 2 reserves the bottom");
		CHECK(p.sectors == 60 + 61,
		      "inside target: stub (60) + bottom (61)");
	}

	/* One sector shy of the end but runway still fits → single seg. */
	{
		uint32_t c = LOG_END - T - LOG_SECTOR_SIZE;

		runway_plan_topup(c, c, LOG_START, T, &p);
		CHECK(p.s1_to == c + T && p.s2_to == 0,
		      "remaining>T: still single segment (no reserve)");
	}

	/* Bottom partially reserved: segment 2 starts at lo. */
	{
		uint32_t c = LOG_END - T + LOG_SECTOR_SIZE;

		runway_plan_topup(c, c, LOG_START + 20 * LOG_SECTOR_SIZE,
				  T, &p);
		CHECK(p.s2_from == LOG_START + 20 * LOG_SECTOR_SIZE &&
		      p.sectors == 60 + 41,
		      "partial bottom: 41 remaining + stub (60)");
	}

	/* hi beyond the target (post-advance state): no negative work. */
	{
		uint32_t c = LOG_START + 5 * LOG_SECTOR_SIZE;

		runway_plan_topup(c, c + T, LOG_START, T, &p);
		CHECK(p.sectors == 0, "hi past target: zero, no underflow");
	}

	/* Smaller budget (SL): the plan follows the smaller target. */
	{
		uint32_t t23 = 23 * LOG_SECTOR_SIZE;

		runway_plan_topup(LOG_START, LOG_START, LOG_START, t23, &p);
		CHECK(p.sectors == 23, "SL budget: 23-sector cold top-up");
	}
	return 0;
}

/* ── 5. FULL SIMULATION: erase-before-program, every budget ────────
 * The store's DECISION LAYER, mirrored: same planner, wrap rule,
 * reclaim rule, plus a per-sector erase bitmap as ground truth. */
#define NSECT  (LOG_SIZE / LOG_SECTOR_SIZE)

struct sim_run { uint32_t addr; uint32_t span; };

static uint8_t erased[NSECT];
static struct sim_run ring[MAX_INDEXED_RUNS];
static uint16_t count;
static uint32_t sim_cursor, sim_hi, sim_lo;
static uint32_t sim_budget = RUN_BUDGET_DEFAULT;
static uint32_t sim_target;

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
	printf("FAIL(sim) run %u: %s (cursor=0x%x hi=0x%x lo=0x%x T=0x%x)\n",
	       run_no, what, sim_cursor, sim_hi, sim_lo, sim_target);
	failures++;
	return 1;
}

static int sim_record(uint32_t payload, uint32_t run_no)
{
	uint32_t span = run_span(payload);
	bool wrapped;
	uint32_t addr = next_run_addr(sim_cursor, span, &wrapped);
	struct runway_plan p;

	if (payload > sim_budget) {
		return sim_fail("payload exceeds ACTIVE budget", run_no);
	}

	/* THE guard: program only into known-erased sectors. */
	if (!wrapped && sim_cursor + span > sim_hi) {
		return sim_fail("program past hi (coverage)", run_no);
	}
	if (wrapped && LOG_START + span > sim_lo) {
		return sim_fail("wrapped program past lo (coverage)", run_no);
	}

	for (uint32_t a = addr; a < addr + span; a += LOG_SECTOR_SIZE) {
		if (!erased[sec_of(a)]) {
			return sim_fail("PROGRAM INTO UNERASED SECTOR", run_no);
		}
		erased[sec_of(a)] = 0;
	}

	if (count == MAX_INDEXED_RUNS) {
		memmove(&ring[0], &ring[1], (count - 1) * sizeof(ring[0]));
		count--;
	}
	ring[count].addr = addr;
	ring[count].span = span;
	count++;
	sim_cursor = addr + span;

	if (wrapped) {
		sim_hi = sim_lo;
		sim_lo = LOG_START;
	}
	runway_plan_topup(sim_cursor, sim_hi, sim_lo, sim_target, &p);

	while (count > 0 &&
	       (overlaps(ring[0].addr, ring[0].addr + ring[0].span,
			 p.s1_from, p.s1_to) ||
		overlaps(ring[0].addr, ring[0].addr + ring[0].span,
			 p.s2_from, p.s2_to))) {
		memmove(&ring[0], &ring[1], (count - 1) * sizeof(ring[0]));
		count--;
	}

	for (uint32_t a = p.s1_from; a < p.s1_to; a += LOG_SECTOR_SIZE) {
		erased[sec_of(a)] = 1;
	}
	for (uint32_t a = p.s2_from; a < p.s2_to; a += LOG_SECTOR_SIZE) {
		erased[sec_of(a)] = 1;
	}

	if (LOG_END - sim_cursor >= sim_target) {
		sim_hi = sim_cursor + sim_target;
	} else {
		sim_hi = LOG_END;
		sim_lo = LOG_START + sim_target;
	}

	/* Invariants, every record. */
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
	sim_target = runway_target(sim_budget);

	/* Boot top-up precedes any record. */
	{
		struct runway_plan p;

		runway_plan_topup(sim_cursor, sim_hi, sim_lo, sim_target, &p);
		for (uint32_t a = p.s1_from; a < p.s1_to; a += LOG_SECTOR_SIZE) {
			erased[sec_of(a)] = 1;
		}
		for (uint32_t a = p.s2_from; a < p.s2_to; a += LOG_SECTOR_SIZE) {
			erased[sec_of(a)] = 1;
		}
		sim_hi = sim_cursor + sim_target;
	}
}

/* Capacity formula — mirror of store_free_full_runs(). */
static uint32_t sim_free_full_runs(void)
{
	uint32_t live = 0;

	for (uint16_t i = 0; i < count; i++) {
		live += ring[i].span;
	}
	if (live + sim_target > LOG_SIZE) {
		return 0;
	}
	return (LOG_SIZE - live - sim_target) / sim_target;
}

static const uint32_t budgets[] = {
	92160u,               /* SL 90 s     */
	122880u,              /* GS/SG 120 s */
	RUN_BUDGET_DEFAULT,   /* DH 150 s    */
	614400u,              /* Custom 600 s */
};

static int test_lifecycle(void)
{
	for (size_t b = 0; b < sizeof(budgets) / sizeof(budgets[0]); b++) {
		uint32_t run_no = 0;
		uint32_t T;

		sim_budget = budgets[b];
		sim_reset();
		T = sim_target;
		printf("   -- budget %u B (target %u sectors, %u free full at fresh)\n",
		       (unsigned)budgets[b], (unsigned)(T / LOG_SECTOR_SIZE),
		       (unsigned)sim_free_full_runs());

		CHECK(sim_free_full_runs() == (LOG_SIZE - T) / T,
		      "fresh-log capacity == (LOG_SIZE - T)/T");

		/* Typical 50 s training runs, long past rotation. */
		for (int i = 0; i < 200; i++, run_no++) {
			if (sim_record(39350, run_no)) { return 1; }
		}
		{
			uint32_t floor_cap = (LOG_SIZE - T) / run_span(39350);

			printf("     typical x200: live=%u free=%u\n",
			       count, (unsigned)sim_free_full_runs());
			CHECK(count >= floor_cap - 1 && count <= floor_cap + 1,
			      "typical live count == capacity minus runway (±1)");
		}

		/* Max-budget runs from mid-rotation: least bottom margin. */
		for (int i = 0; i < 12; i++, run_no++) {
			if (sim_record(sim_budget, run_no)) { return 1; }
		}
		printf("     +max x12: live=%u free=%u\n",
		       count, (unsigned)sim_free_full_runs());
		{
			uint32_t live_bytes = 0;

			for (uint16_t i = 0; i < count; i++) {
				live_bytes += ring[i].span;
			}
			CHECK(live_bytes + T <= LOG_SIZE,
			      "live set (mixed sizes) + standing reserve within region");
		}

		/* Adversarial random mix with boundary hunters. */
		{
			uint32_t seed = 0x5EEDC0DEu;

			for (int i = 0; i < 500; i++, run_no++) {
				uint32_t r = seed = seed * 1664525u
						       + 1013904223u;
				uint32_t payload;

				if (i % 37 == 0) {
					uint32_t remaining =
						LOG_END - sim_cursor;
					uint32_t fit =
						(remaining >= BLOB_OVERHEAD + 1)
						? run_span(remaining
							   - BLOB_OVERHEAD - 1)
						: run_span(1);

					payload = fit - BLOB_OVERHEAD;
					if (payload > sim_budget) {
						payload = sim_budget;
					}
				} else if (i % 5 == 0) {
					payload = sim_budget;
				} else {
					payload = 1 + (r % sim_budget);
				}
				if (sim_record(payload, run_no)) { return 1; }
			}
		}
		printf("     +random x500: live=%u total=%u free=%u\n",
		       count, run_no, (unsigned)sim_free_full_runs());
		CHECK(count <= MAX_INDEXED_RUNS,
		      "index cap respected throughout");
		CHECK(sim_free_full_runs() <= (LOG_SIZE - T) / T,
		      "free-run estimate never exceeds fresh capacity");
	}
	return 0;
}

int main(void)
{
	printf("SGC store layout host tests (SGL3)\n");
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
