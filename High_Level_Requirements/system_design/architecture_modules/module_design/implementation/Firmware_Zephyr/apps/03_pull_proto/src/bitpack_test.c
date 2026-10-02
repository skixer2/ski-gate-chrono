/**
 * @file    bitpack_test.c
 * @brief   On-target differential test: sgc_bitpacker (C port) vs the
 *          ORIGINAL ArduinoFW BitPacker (C++, compiled alongside).
 *
 * Case 1 embeds the B2a golden frames (real BHI260 capture, 2026-10-02):
 *   RV  x=-1405 y=-6711 z=-74 w=14879 (all 5 identical, device still)
 *   LACC (4,-1,3) (10,1,9) (9,1,0) (8,-3,9) (6,1,4)
 *   BARO 114782 raw -> div2 57391
 * Cases 2-5 exercise classification boundaries, forced anchor (frame 50),
 * timestamp jump, baro-delta edges. Every frame must pack byte-identically
 * on both implementations.
 */

#include "bitpack_test.h"
#include "bitpacker.h"
#include "bp_ref/bp_ref_glue.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

struct tf {
	struct sgc_raw_frame f;
	uint32_t ts;
};

static struct tf case_golden[] = {
	{{14879, -1405, -6711, -74, 4, -1, 3, 0}, 10},
	{{14879, -1405, -6711, -74, 10, 1, 9, 57391}, 20},
	{{14879, -1405, -6711, -74, 9, 1, 0, 57391}, 30},
	{{14879, -1405, -6711, -74, 8, -3, 9, 57391}, 40},
	{{14879, -1405, -6711, -74, 6, 1, 4, 57391}, 50},
};

static struct tf case_coast[60];

static struct tf case_bound[] = {
	{{1000, 0, 0, 0, 0, 0, 0, 40000}, 0},
	{{1007, 0, 0, 0, 0, 0, 0, 40000}, 10},    /* +7: fits 4b */
	{{992, 0, 0, 0, 0, 0, 0, 40000}, 20},     /* -15 -> -8: fits 4b */
	{{1000, 8, 0, 0, 0, 0, 0, 40000}, 30},    /* +8: needs 8b */
	{{1000, 8, -128, 0, 0, 0, 0, 40000}, 40}, /* -128: 8b edge */
	{{1000, 8, -128, 127, 0, 0, 0, 40000}, 50},  /* anchor frame */
	{{1000, 8, -128, 127, 0, 200, 0, 40000}, 60}, /* +200: T3 */
};

static struct tf case_tjump[] = {
	{{500, 500, 500, 500, 0, 0, 0, 30000}, 0},
	{{500, 500, 500, 500, 0, 0, 0, 30000}, 10},
	{{500, 500, 500, 500, 0, 0, 0, 30000}, 5000},  /* >1023 ms -> T3 */
	{{500, 500, 500, 500, 0, 0, 0, 30000}, 5010},
};

static struct tf case_baro[] = {
	{{0, 0, 0, 16000, 0, 0, 0, 50000}, 0},
	{{0, 0, 0, 16000, 0, 0, 0, 50007}, 10},
	{{0, 0, 0, 16000, 0, 0, 0, 50014}, 20},   /* delta 7: fits 4b */
	{{0, 0, 0, 16000, 0, 0, 0, 50022}, 30},   /* delta 8: 8b */
	{{0, 0, 0, 16000, 0, 0, 0, 50152}, 40},   /* +130: T3 */
};

static uint32_t g_fail;

static void run_case(const char *name, const struct tf *frames, size_t n)
{
	struct sgc_bitpacker port;
	uint8_t rb[18], rtype;

	sgc_bitpacker_reset(&port);
	bp_ref_reset();
	uint32_t fails_before = g_fail;

	for (size_t i = 0; i < n; i++) {
		uint8_t ps = sgc_bitpacker_encode(&port, &frames[i].f, frames[i].ts);
		uint8_t rs = bp_ref_encode(frames[i].f.q_w, frames[i].f.q_x,
					   frames[i].f.q_y, frames[i].f.q_z,
					   frames[i].f.la_x, frames[i].f.la_y,
					   frames[i].f.la_z,
					   frames[i].f.baro_pa_div2,
					   frames[i].ts, rb, &rtype);
		bool ok = (ps == rs) && (port.last_type == rtype) &&
			  (memcmp(port.buf, rb, 18) == 0);
		if (!ok) {
			g_fail++;
			printk("{\"ev\":\"bp_fail\",\"case\":\"%s\",\"i\":%u,"
			       "\"ref\":%u/%u,\"port\":%u/%u}\n",
			       name, (unsigned)i, rs, rtype, ps, port.last_type);
		}
	}
	printk("{\"ev\":\"bp_case\",\"case\":\"%s\",\"n\":%u,\"ok\":%s}\n",
	       name, (unsigned)n,
	       (g_fail == fails_before) ? "true" : "false");
}

void bitpack_test_run(void)
{
	g_fail = 0;

	run_case("GOLDEN", case_golden,
		 sizeof(case_golden) / sizeof(case_golden[0]));

	for (int i = 0; i < 60; i++) {
		struct sgc_raw_frame f = {15000, 100, -200, 30000, 5, -5, 7, 50000};

		if (i) {
			f.q_w += (i & 1) ? 1 : -1;
			f.la_x += (i & 1) ? 1 : -1;
		}
		case_coast[i].f = f;
		case_coast[i].ts = (uint32_t)i * 10;
	}
	run_case("COAST", case_coast, 60);

	run_case("BOUND", case_bound, sizeof(case_bound) / sizeof(case_bound[0]));
	run_case("TJUMP", case_tjump, sizeof(case_tjump) / sizeof(case_tjump[0]));
	run_case("BARO", case_baro, sizeof(case_baro) / sizeof(case_baro[0]));

	printk("{\"ev\":\"bp_verdict\",\"pass\":%s,\"fails\":%u}\n",
	       g_fail == 0 ? "true" : "false", (unsigned)g_fail);
}
