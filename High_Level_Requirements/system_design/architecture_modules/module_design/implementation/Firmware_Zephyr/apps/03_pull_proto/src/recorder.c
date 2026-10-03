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
 * frame). Start trigger is the manual 'g' (production: 2.0 m drop
 * detector, F04); end is 'e' (production: 5 s flatline < 2 m).
 *
 * PRE-ROLL ARCHITECTURE (documented, F02 + v6.0 — lands with the real
 * sensor frames): while ARMED, each 10 ms tick pushes 1 new frame into
 * a SMALL RAM staging buffer and pops up to 2 frames to the FLASH
 * pre-roll ring (4000×20 B at 0x0000–0x13FFF — the region SGL reserved
 * by placing LOG_START at 0x14000). Net −1/tick ⇒ a full buffer drains
 * in ~20 s; ARM fills at most 3000 slots (30 s cap). At run start the
 * most recent 1000 frames (10 s) become the run's pre-roll and the
 * backlog drains interleaved during LOGGING. The RAM staging buffer is
 * small by design (static, bounded — v4.60's lesson was heap-era; JP
 * 2026-09-29: static RAM is fine with headroom, and this design needs
 * only the staging window, not the ring).
 *
 * Synthetic step (this file, now): frames are 8 B placeholders at the
 * documented packing average (v2.4: 8.0 B/frame → 90 s ≈ 72 KB);
 * ARMED-path frames are DISCARDED until the ring + staging land with
 * the BHI260 driver.
 *
 * Concurrency: k_timer → sysworkq (the port-wide pattern). One frame
 * per tick = ~80 B/s of work — noise.
 */
#include <zephyr/kernel.h>
#include <string.h>
#include "recorder.h"
#include "store.h"
#include "ble_lifecycle.h"
#include "sensor_prod.h"
#include "bitpacker.h"

/* DEDICATED workqueue (v0.10.1). Historical finding: the SHARED
 * sysworkq lost ~25% of 100 Hz ticks during LOGGING when the phone's
 * auto-reconnect + listing reads loaded it (measured pre-v0.10.3).
 * Since v0.10.3 ARM tears the BLE link down (JP), that load source is
 * gone in production — but the dedicated queue STAYS: an acquisition
 * loop must never share a queue with BLE-class work, whichever path
 * re-introduces it ('w' bench, future services). Own 1 KB stack. */
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

/* B3: REAL producer — pull latest sensor snapshot each tick (hold-last,
 * ArduinoFW parity: one frame per tick, millis()-style timestamp),
 * pack adaptive (T1/T2/T3), append to SGL3. Synthetic producer retired. */
static struct sgc_bitpacker rc_packer;

/* Run stats (printed at POST_RUN — JP rule: values, not just counters). */
static uint32_t st_frames, st_bytes, st_t1, st_t2, st_t3;
static uint32_t st_skipped;         /* ticks with no snapshot yet */

static void rc_stats_reset(void)
{
	st_frames = st_bytes = st_t1 = st_t2 = st_t3 = st_skipped = 0;
	sgc_bitpacker_reset(&rc_packer);
}

static void rc_stats_print(const char *ev)
{
	uint32_t avg_x10 = st_frames ? (st_bytes * 10u) / st_frames : 0;

	printk("{\"rec_stats\":\"%s\",\"frames\":%u,\"bytes\":%u,"
	       "\"avg_b_x10\":%u,\"t1\":%u,\"t2\":%u,\"t3\":%u,"
	       "\"skipped\":%u}\n",
	       ev, st_frames, st_bytes, avg_x10,
	       st_t1, st_t2, st_t3, st_skipped);
}

/* 10 Hz decimated detector hook — the architecture point where the
 * production 2.0 m baro-drop check will live (F04). Synthetic: nop. */
static void detector_10hz(void)
{
}

static void rc_work_handler(struct k_work *w)
{
	static uint32_t ticks;
	int intent = atomic_set(&cmd_intent, 0);

	ticks++;

	/* Commands execute in-order with the machine, never beside it. */
	switch (intent) {
	case 1:
		if (rc_state == RC_IDLE && !store_busy()) {
			rc_state = RC_ARMED;
			ticks = 0;
			rc_stats_reset();
			ble_lifecycle_suspend();   /* JP: no phone while armed */
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
			rc_stats_print("manual_end");
		} else {
			printk("{\"rec\":\"ERR\",\"why\":\"not_logging\"}\n");
		}
		break;
	default:
		break;
	}

	switch (rc_state) {
	case RC_ARMED: {
		struct sgc_raw_frame fr;

		/* acquire + discard (pre-roll ring lands in B4) */
		if (sensor_prod_get_frame(&fr)) {
			st_frames++;
		} else {
			st_skipped++;
		}
		if (ticks % 10 == 0) {
			detector_10hz();
		}
		if (ticks >= 100 * 120) {   /* 120 s arm timeout (JP) */
			printk("{\"rec\":\"IDLE\",\"ev\":\"arm_timeout\"}\n");
			rc_state = RC_IDLE;
			ble_lifecycle_resume();
			k_timer_stop(&rc_tick);
		}
		break;
	}   /* RC_ARMED */

	case RC_LOGGING: {
		struct sgc_raw_frame fr;
		bool have = sensor_prod_get_frame(&fr);

		if (!have) {
			st_skipped++;
			break;
		}
		uint8_t sz = sgc_bitpacker_encode(&rc_packer, &fr,
						 k_uptime_get_32());
		st_frames++; st_bytes += sz;
		if (rc_packer.last_type == SGC_PKT_T1) {
			st_t1++;
		} else if (rc_packer.last_type == SGC_PKT_T2) {
			st_t2++;
		} else {
			st_t3++;
		}
		int32_t n = store_record_append(rc_packer.buf, sz);

		if (n < (int32_t)sz) {
			/* n == -1: record already closed (e.g. manual end
			 * raced in) — just move on. 0 <= n < sz with an
			 * active record = budget cap: truncate + close (JP). */
			if (n >= 0) {
				printk("{\"rec\":\"POST_RUN\",\"ev\":\"budget_reached\"}\n");
				rc_stats_print("budget_reached");
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
			ble_lifecycle_resume();
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
