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

/* DEDICATED workqueue (v0.10.1): k_timer→SHARED sysworkq lost ~25% of
 * 10 ms ticks during LOGGING — the phone's auto-reconnect + listing
 * reads (rebuild_listing in GATT context) load the sysworkq and pending
 * k_work submissions coalesce. A 100 Hz acquisition cannot share a
 * queue with BLE. Own stack: 1 KB is ample (append path is tiny). */
static K_KERNEL_STACK_DEFINE(rec_q_stack, 1024);
static struct k_work_q rec_q;

enum rec_state { RC_IDLE, RC_ARMED, RC_LOGGING, RC_POST_RUN };

static enum rec_state rc_state = RC_IDLE;
static struct k_timer rc_tick;
static struct k_work rc_work;

/* Command intents (v0.10.2): ALL state transitions happen in the rec_q
 * thread — the console thread only sets intents + pokes the work. The
 * v0.10.1 two-thread race (console 'e' vs tick close interleaving:
 * double end(), stuck POST_RUN) is structurally impossible now. */
static atomic_t cmd_intent;   /* 0=none 1=arm 2=start 3=end */

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
	int intent = atomic_set(&cmd_intent, 0);

	ticks++;

	/* Commands execute in-order with the machine, never beside it. */
	switch (intent) {
	case 1:
		if (rc_state == RC_IDLE && !store_busy()) {
			rc_state = RC_ARMED;
			ticks = 0;
			printk("{\"rec\":\"ARMED\"}\n");
			k_timer_start(&rc_tick, K_MSEC(10), K_MSEC(10));
		} else {
			printk("{\"rec\":\"ERR\",\"why\":\"not_idle\"}\n");
		}
		break;
	case 2:
		if (rc_state == RC_ARMED) {
			if (store_record_begin()) {
				rc_state = RC_LOGGING;
				printk("{\"rec\":\"LOGGING\"}\n");
			} else {
				printk("{\"rec\":\"ERR\",\"why\":\"store\"}\n");
			}
		} else {
			printk("{\"rec\":\"ERR\",\"why\":\"not_armed\"}\n");
		}
		break;
	case 3:
		if (rc_state == RC_LOGGING) {
			store_record_end();
			rc_state = RC_POST_RUN;
			printk("{\"rec\":\"POST_RUN\",\"ev\":\"manual_end\"}\n");
		} else {
			printk("{\"rec\":\"ERR\",\"why\":\"not_logging\"}\n");
		}
		break;
	default:
		break;
	}

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
			/* n == -1: record already closed (e.g. manual end
			 * raced in) — just move on. n < 8 with an active
			 * record = budget cap: truncate + close (JP). */
			if (n >= 0) {
				printk("{\"rec\":\"POST_RUN\",\"ev\":\"budget_reached\"}\n");
				store_record_end();
			}
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
	k_work_submit_to_queue(&rec_q, &rc_work);
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
	atomic_set(&cmd_intent, 1);
	k_work_submit_to_queue(&rec_q, &rc_work);
}

void recorder_start(void)
{
	atomic_set(&cmd_intent, 2);
	k_work_submit_to_queue(&rec_q, &rc_work);
}

void recorder_end(void)
{
	atomic_set(&cmd_intent, 3);
	k_work_submit_to_queue(&rec_q, &rc_work);
}

void recorder_init(void)
{
	k_work_queue_start(&rec_q, rec_q_stack,
			   K_KERNEL_STACK_SIZEOF(rec_q_stack),
			   K_PRIO_PREEMPT(4), NULL);
	k_thread_name_set(&rec_q.thread, "recorder");
	k_work_init(&rc_work, rc_work_handler);
	k_timer_init(&rc_tick, rc_timer_fn, NULL);
	printk("recorder: ready (a=arm g=go e=end, own workqueue)\n");
}
