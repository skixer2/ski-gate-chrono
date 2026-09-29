/*
 * recorder.c — M3 step 1: the recording state machine (JP GO 2026-09-29)
 * ====================================================================
 * Owns the ATHLETE-VISIBLE states; the SGL store stays a dumb storage
 * lifecycle. Drives the store's streaming append API.
 *
 *   IDLE ──'a'──► ARMED ──'g'──► LOGGING ──end──► POST_RUN ──► IDLE
 *                 │ 120 s timeout        │ 'e' or budget (truncate+flag)
 *                 ▼                      ▼
 *               IDLE                  POST_RUN
 *
 * ACQUISITION runs at 100 Hz from ARM through LOGGING (JP: even while
 * armed); the start-detector check is DECIMATED to 10 Hz (every 10th
 * frame). Synthetic step: frames are 8 B placeholders at the documented
 * packing average (v2.4: 8.0 B/frame → 1 KB/s ceiling ⇒ 90 s ≈ 72 KB);
 * ARMED discards them (pre-roll = sensor step, flash ring in the
 * reserved region — never RAM, the v4.60 lesson). Start trigger is the
 * manual 'g' (production: 2.0 m drop detector, F04); end is 'e'
 * (production: 5 s flatline < 2 m).
 *
 * Concurrency: k_timer → sysworkq (the port-wide pattern). One frame
 * per tick = ~80 B/s of work — noise.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include "recorder.h"
#include "store.h"

enum rec_state { RC_IDLE, RC_ARMED, RC_LOGGING, RC_POST_RUN };

static enum rec_state rc_state = RC_IDLE;
static struct k_timer rc_tick;
static struct k_work rc_work;

/* Synthetic frame: deterministic (counter + ramp) for CRC habits. */
#define SYNTH_FRAME_LEN 8
static uint32_t synth_seq;

static void synth_frame(uint8_t *f)
{
	f[0] = (uint8_t)(synth_seq);
	f[1] = (uint8_t)(synth_seq >> 8);
	f[2] = (uint8_t)(synth_seq >> 16);
	f[3] = (uint8_t)(synth_seq >> 24);
	for (int i = 4; i < SYNTH_FRAME_LEN; i++) {
		f[i] = (uint8_t)((synth_seq + i) & 0xFF);
	}
	synth_seq++;
}

/* 10 Hz decimated detector hook — the architecture point where the
 * production 2.0 m baro-drop check will live (F04). Synthetic: nop. */
static void detector_10hz(void)
{
}

static void rc_work_handler(struct k_work *w)
{
	static uint32_t ticks;
	uint8_t f[SYNTH_FRAME_LEN];

	ticks++;

	switch (rc_state) {
	case RC_ARMED:
		synth_frame(f);          /* acquire + discard (pre-roll later) */
		if (ticks % 10 == 0) {
			detector_10hz();
		}
		if (ticks >= 100 * 120) {   /* 120 s arm timeout (JP) */
			printk("{\"rec\":\"IDLE\",\"ev\":\"arm_timeout\"}\n");
			rc_state = RC_IDLE;
			k_timer_stop(&rc_tick);
		}
		break;

	case RC_LOGGING: {
		synth_frame(f);
		int32_t n = store_record_append(f, sizeof(f));

		if (n < (int32_t)sizeof(f)) {
			/* Budget cap: truncate-at-budget + close (JP). */
			printk("{\"rec\":\"POST_RUN\",\"ev\":\"budget_reached\"}\n");
			store_record_end();
			rc_state = RC_POST_RUN;
			/* keep the timer: POST_RUN polls store idle */
		}
		break;
	}

	case RC_POST_RUN:
		if (!store_busy()) {
			printk("{\"rec\":\"IDLE\",\"ev\":\"run_ready\"}\n");
			rc_state = RC_IDLE;
			k_timer_stop(&rc_tick);
		}
		break;

	default:
		k_timer_stop(&rc_tick);
		break;
	}
}

static void rc_timer_fn(struct k_timer *t)
{
	k_work_submit(&rc_work);
}

const char *recorder_state_str(void)
{
	static const char *const names[] = {
		"IDLE", "ARMED", "LOGGING", "POST_RUN"
	};

	return names[rc_state];
}

void recorder_arm(void)
{
	if (rc_state != RC_IDLE || store_busy()) {
		printk("{\"rec\":\"ERR\",\"why\":\"not_idle\"}\n");
		return;
	}
	rc_state = RC_ARMED;
	printk("{\"rec\":\"ARMED\"}\n");
	k_timer_start(&rc_tick, K_MSEC(10), K_MSEC(10));
}

void recorder_start(void)
{
	if (rc_state != RC_ARMED) {
		printk("{\"rec\":\"ERR\",\"why\":\"not_armed\"}\n");
		return;
	}
	if (!store_record_begin()) {
		printk("{\"rec\":\"ERR\",\"why\":\"store\"}\n");
		return;
	}
	rc_state = RC_LOGGING;
	printk("{\"rec\":\"LOGGING\"}\n");
}

void recorder_end(void)
{
	if (rc_state != RC_LOGGING) {
		printk("{\"rec\":\"ERR\",\"why\":\"not_logging\"}\n");
		return;
	}
	store_record_end();
	rc_state = RC_POST_RUN;
	printk("{\"rec\":\"POST_RUN\",\"ev\":\"manual_end\"}\n");
}

void recorder_init(void)
{
	k_work_init(&rc_work, rc_work_handler);
	k_timer_init(&rc_tick, rc_timer_fn, NULL);
	printk("recorder: ready (a=arm g=go e=end)\n");
}
