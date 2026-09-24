/*
 * test_layout.c — host-side tests for the circular run log (step 4b)
 * ==================================================================
 * THE FOUNDING SUITE. Compiles on the PC (msys64 gcc), never touches
 * hardware, and proves the store's arithmetic BEFORE it meets flash.
 * Each test names the bug class it prevents — starting with the one
 * that started this policy: "sized from test data, not the domain."
 *
 * Build & run (from the app folder, tools on PATH):
 *   gcc -Wall -Wextra -Isrc tests/test_layout.c -o build/test_layout
 *   build\test_layout.exe
 * Exit code 0 = all green. Any failure prints the line and aborts.
 */
#include <stdio.h>
#include <string.h>
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
	CHECK(run_span(39350)   == run_span(39350),   "typical run deterministic");
	CHECK(run_span(39350)   == 10 * LOG_SECTOR_SIZE, "typical 39350 B payload = 10 sectors (40980 B)");
	return 0;
}

/* ── 2. Wrap boundary ────────────────────────────────────────────────
 * Prevents: runs straddling LOG_END (blob reads are linear). */
static int test_wrap(void)
{
	bool wrapped = true;

	/* A run ending EXACTLY at LOG_END does not wrap. */
	uint32_t span = 5 * LOG_SECTOR_SIZE;
	uint32_t cursor = LOG_END - span;

	CHECK(next_run_addr(cursor, span, &wrapped) == cursor && !wrapped,
	      "exact fit at region end: no wrap");
	CHECK(next_run_addr(cursor, span + 1, &wrapped) == LOG_START && wrapped,
	      "one byte over: wraps to LOG_START");
	CHECK(next_run_addr(LOG_START, run_span(1), &wrapped) == LOG_START && !wrapped,
	      "first run at base: no wrap");
	return 0;
}

/* ── 3. THE FOUNDING TEST: listing budgets fit the policy domain ────
 * Prevents: the 160-byte bug — buffers sized from test data instead
 * of MAX_INDEXED_RUNS × worst-case entry. */
static int test_listing_budget(void)
{
	/* Worst-case entry, constructed to the JSON grammar the app parses:
	 * {"id":NN,"ts":4294967295,"size":249856,"side":"right"},
	 * with id at the cap (MAX_INDEXED_RUNS-1 = 63, two digits). */
	char worst[96];
	int n = snprintf(worst, sizeof(worst),
			 "{\"id\":%u,\"ts\":%lu,\"size\":%lu,\"side\":\"right\"},",
			 (unsigned)(MAX_INDEXED_RUNS - 1),
			 4294967295UL, (unsigned long)MAX_RUN_BUDGET);

	CHECK(n > 0 && n < 96, "worst entry fits the local buffer");
	/* +2: opening '[' and NUL. Comma on the last entry is dropped —
	 * the bound stays valid because we budget it for every entry. */
	CHECK(MAX_INDEXED_RUNS * (uint32_t)n + 2 <= RUN_LIST_JSON_SIZE,
	      "MAX_INDEXED_RUNS worst-case entries fit the JSON buffer");
	return 0;
}

/* ── 4. Erase-work bookkeeping ───────────────────────────────────────
 * Prevents: re-erasing already-erased sectors (wear + latency). */
static int test_erase_work(void)
{
	CHECK(sectors_to_erase(LOG_START, LOG_START + 8192, LOG_START) == 2,
	      "cold erase of 2 sectors");
	CHECK(sectors_to_erase(LOG_START, LOG_START + 8192,
			       LOG_START + 4096) == 1,
	      "half-erased range: only 1 sector left");
	CHECK(sectors_to_erase(LOG_START, LOG_START + 4096,
			       LOG_START + 8192) == 0,
	      "already fully erased: zero work");
	return 0;
}

/* ── 5. Full simulation: fill, rotate, capacity ────────────────────
 * Prevents: reclaim bugs that only appear after the region is full
 * (the state a bench rarely reaches by hand). Mirrors the store policy:
 * pack at the cursor, wrap when the span doesn't fit, and drop OLDEST
 * entries ONLY when their extent actually intersects the range that
 * must be erased (virgin territory is prep-erased, never "reclaimed").
 */
struct sim_run {
	uint32_t addr;
	uint32_t span;
};

/* Does [a1,a2) intersect [b1,b2)? Sector-aligned extents, disjoint. */
static bool overlaps(uint32_t a1, uint32_t a2, uint32_t b1, uint32_t b2)
{
	return a1 < b2 && b1 < a2;
}

static int simulate(uint32_t payload, int iterations, uint32_t *live_out,
		    uint32_t *written_out)
{
	struct sim_run ring[MAX_INDEXED_RUNS];
	uint16_t count = 0;
	uint32_t cursor = LOG_START;
	uint32_t written = 0;
	uint32_t span = run_span(payload);

	for (int i = 0; i < iterations; i++) {
		bool wrapped;
		uint32_t addr = next_run_addr(cursor, span, &wrapped);

		/* Erase range needed: the run itself, plus the tail stub
		 * when we wrap (the skipped remnant joins the runway). */
		uint32_t e1 = addr, e2 = addr + span;

		if (wrapped) {
			e2 += (LOG_END - cursor);
		}

		/* Reclaim: drop oldest entries whose extent intersects
		 * the erase range. Entries are disjoint and age-ordered;
		 * the drop loop is the whole garbage "policy". */
		while (count > 0 &&
		       overlaps(ring[0].addr, ring[0].addr + ring[0].span,
				e1, e2)) {
			memmove(&ring[0], &ring[1],
				(count - 1) * sizeof(ring[0]));
			count--;
		}

		/* Record (index cap: tiny-run edge — drop oldest if full). */
		if (count == MAX_INDEXED_RUNS) {
			memmove(&ring[0], &ring[1],
				(count - 1) * sizeof(ring[0]));
			count--;
		}
		ring[count].addr = addr;
		ring[count].span = span;
		count++;
		cursor = addr + span;
		written++;

		/* Invariants, every iteration. */
		if (cursor > LOG_END) {
			printf("FAIL cursor 0x%x past LOG_END (run %u)\n",
			       cursor, written);
			failures++;
			return 1;
		}
		if (count > MAX_INDEXED_RUNS) {
			printf("FAIL index overflow (run %u)\n", written);
			failures++;
			return 1;
		}
	}
	*live_out = count;
	*written_out = written;
	return 0;
}

static int test_fill_and_rotate(void)
{
	uint32_t live, written;

	/* Typical 50 s training runs, forever: the live set must settle
	 * at the region capacity, rotation must never deadlock. */
	if (simulate(39350, 200, &live, &written)) {
		return 1;
	}
	printf("     typical: live=%u written=%u expected~%u\n",
	       live, written, (unsigned)(LOG_SIZE / run_span(39350)));
	CHECK(live == LOG_SIZE / run_span(39350),
	      "typical-run live count == region capacity (48)");
	CHECK(written == 200, "all writes accepted (rotation, no deadlock)");

	/* Max-budget runs: 244 KiB each; only ~7 fit; rotation must hold. */
	if (simulate(MAX_RUN_BUDGET - BLOB_OVERHEAD, 12, &live, &written)) {
		return 1;
	}
	printf("     max-budget: live=%u written=%u\n", live, written);
	CHECK(live <= LOG_SIZE / run_span(MAX_RUN_BUDGET - BLOB_OVERHEAD),
	      "max-budget live set within region");
	CHECK(written == 12, "max-budget rotation, no deadlock");

	/* Tiny runs: index cap engages before the region fills. */
	if (simulate(1024, 200, &live, &written)) {
		return 1;
	}
	printf("     tiny: live=%u written=%u\n", live, written);
	CHECK(live == MAX_INDEXED_RUNS,
	      "tiny-run live count pins at the index cap");
	return 0;
}

int main(void)
{
	printf("SGC store layout host tests\n");
	printf("===========================\n");
	if (test_span_padding())  { return 1; }
	if (test_wrap())          { return 1; }
	if (test_listing_budget()) { return 1; }
	if (test_erase_work())    { return 1; }
	if (test_fill_and_rotate()) { return 1; }
	printf("===========================\n");
	printf("ALL GREEN (%d failures)\n", failures);
	return failures ? 1 : 0;
}
