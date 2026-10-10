/*
 * sensor_synth.c — synthetic FRAME producer for bench (CONFIG_SGC_BENCH).
 * =====================================================================
 * Feeds the recorder lifecycle with generated sgc_raw_frames so a
 * complete, realistic run lands in the store through the IDENTICAL
 * production path (BitPacker → ring → store → phone decode).
 *
 * Model (deterministic, seed fixed → reproducible ground truth):
 *   - GS turn rhythm: 1.2 s turn at 35°/s every 4.0 s, alternating
 *     direction. Heading integrates in integer °×100; the quaternion
 *     is exact yaw (libc cos/sin, bench build only) → |q| = 1.0000.
 *   - During turns: la_x = dir × CENTRIPETAL_MG (v²/r @ 12 m/s, r 13 m)
 *     ± noise; between turns: noise only.
 *   - Pole impact at every k·gate_interval (k = 1, 2, …), timed from
 *     recorder START (after the 2 s preroll): 3 samples at 100 Hz —
 *     amp (2.2–3.5 g, LCG-varied), then 40 %, then 15 % — on la_y.
 *   - Baro: 889.0 hPa ± 1 LSB.
 *
 * Ground truth prints the moment it enters the data:
 *   {"synth":"start","secs":N,"gate_s":M,"seed":42}
 *   {"synth":"impact","n":k,"t_ms":…,"amp_mg":…}   (t_ms from run start)
 *   {"synth":"done","impacts":N}
 *
 * Concurrency: 'S' (cmd_work) initializes all state, then arms the
 * recorder; from then on the ONLY writer of t_ms is the generator
 * itself, called from the recorder workqueue at 100 Hz. Lifecycle
 * pacing runs on one delayable work (sysworkq), which only submits
 * recorder intents (atomic by design).
 */
#include <zephyr/kernel.h>
#include <math.h>
#include "sensor_synth.h"
#include "bitpacker.h"
#include "recorder.h"
#include "store.h"

#if IS_ENABLED(CONFIG_SGC_BENCH)

#define PREROLL_MS       2000u
#define TURN_DUR_MS      1200u
#define TURN_PERIOD_MS   4000u
#define TURN_STEP_X100   35u     /* 0.35° per 10 ms tick = 35°/s */
#define CENTRIPETAL_MG   1100
#define BARO_DIV2        44450u  /* 889.0 hPa as Pa/2 */
#define SYNTH_SEED       42u
#define SYNTH_PI         3.14159265358979323846

static volatile bool syn_active;
static uint32_t t_ms;            /* since synth start (frame time)  */
static uint32_t syn_secs, syn_gate_s;
static uint32_t next_impact_k;   /* k of the next due impact        */
static int32_t  impact_amp;      /* amplitude of the last impact    */
static uint32_t impact_t0;       /* its first-sample time           */
static int32_t  heading_x100;    /* degrees ×100, wraps ±18000      */
static uint32_t lcg = SYNTH_SEED;

static struct k_work_delayable syn_work;
static uint32_t syn_stage_ms;    /* orchestrator clock              */

static int32_t lcg_range(int32_t lo, int32_t hi)
{
	lcg = lcg * 1103515245u + 12345u;
	return lo + (int32_t)((lcg >> 16) % (uint32_t)(hi - lo + 1));
}

/* Run-relative time of impact k (first sample). */
static uint32_t impact_time(uint32_t k)
{
	return PREROLL_MS + k * syn_gate_s * 1000u;
}

bool sensor_synth_get_frame(struct sgc_raw_frame *f)
{
	if (!syn_active) {
		return false;
	}

	uint32_t phase = t_ms % TURN_PERIOD_MS;
	uint32_t period = t_ms / TURN_PERIOD_MS;
	bool turning = phase < TURN_DUR_MS;
	int dir = (period & 1u) ? -1 : 1;

	if (turning && t_ms > 0) {
		heading_x100 += dir * TURN_STEP_X100;
		if (heading_x100 > 18000) {
			heading_x100 -= 36000;
		} else if (heading_x100 < -18000) {
			heading_x100 += 36000;
		}
	}

	/* Exact yaw quaternion: |q| = 1 up to Q14 rounding. */
	double half = (double)heading_x100 * (SYNTH_PI / 18000.0) / 2.0;

	f->q_w = (int16_t)lround(cos(half) * 16384.0);
	f->q_x = 0;
	f->q_y = 0;
	f->q_z = (int16_t)lround(sin(half) * 16384.0);

	f->la_x = (int16_t)(turning ? dir * CENTRIPETAL_MG : 0)
		  + (int16_t)lcg_range(-15, 15);
	f->la_y = (int16_t)lcg_range(-15, 15);
	f->la_z = (int16_t)lcg_range(-15, 15);

	/* Impacts: 3-sample transient on la_y at each gate interval. */
	uint32_t dt = t_ms - impact_t0;

	if (syn_gate_s && t_ms == impact_time(next_impact_k)) {
		impact_amp = lcg_range(2200, 3500);
		impact_t0 = t_ms;
		printk("{\"synth\":\"impact\",\"n\":%u,\"t_ms\":%u,"
		       "\"amp_mg\":%d}\n", next_impact_k,
		       t_ms - PREROLL_MS, impact_amp);
		f->la_y = (int16_t)impact_amp;
		next_impact_k++;
	} else if (dt == 10u) {
		f->la_y = (int16_t)(impact_amp * 40 / 100);
	} else if (dt == 20u) {
		f->la_y = (int16_t)(impact_amp * 15 / 100);
	}

	f->baro_pa_div2 = (uint16_t)(BARO_DIV2 + lcg_range(0, 1));

	t_ms += 10u;
	return true;
}

static void syn_work_handler(struct k_work *w)
{
	if (!syn_active) {
		return;
	}
	if (syn_stage_ms == 0) {
		/* Preroll captured; run exactly syn_secs, then close. */
		syn_stage_ms = PREROLL_MS;
		recorder_start();
		k_work_reschedule(&syn_work, K_MSEC(syn_secs * 1000u));
		return;
	}
	recorder_end();
	syn_active = false;
	printk("{\"synth\":\"done\",\"impacts\":%u}\n", next_impact_k);
}

bool sensor_synth_start(uint32_t secs, uint32_t gate_s)
{
	if (syn_active || recorder_state_idx() != 0 || store_busy()) {
		return false;
	}
	syn_secs = secs;
	syn_gate_s = gate_s;
	t_ms = 0;
	next_impact_k = 1;
	impact_amp = 0;
	impact_t0 = 0xFFFFFFF0u;   /* dt never 10/20 before first impact */
	heading_x100 = 0;
	lcg = SYNTH_SEED;
	syn_stage_ms = 0;

	printk("{\"synth\":\"start\",\"secs\":%u,\"gate_s\":%u,"
	       "\"seed\":%u}\n", secs, gate_s, SYNTH_SEED);

	recorder_arm();            /* prints ARMED; BLE torn down */
	syn_active = true;         /* frames start flowing next tick */
	k_work_init_delayable(&syn_work, syn_work_handler);
	k_work_reschedule(&syn_work, K_MSEC(PREROLL_MS));
	return true;
}

#endif /* CONFIG_SGC_BENCH */
