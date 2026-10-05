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
 * PRE-ROLL RING — SPEC v4-FINAL (JP, 2026-10-05, supersedes F02/v6.0):
 *   Region 0x0000–0x13FFF (20 sectors, 4096 linear slots × 20 B).
 *   Bank A 0x0–0x9FFF  = 20 s PRE-trigger  max (ARM ≤ 20 s, absolute:
 *                        no trigger at 20 s → back to IDLE).
 *   Bank B 0xA000–0x13FFF = 20 s POST-trigger: order-preserving buffer
 *     for post-trigger frames that CANNOT be written directly to the run
 *     while pre-trigger frames still drain (would break timestamp order).
 *   INVARIANT: entering IDLE ⇒ both banks erased (boot, post-run,
 *     arm-timeout — every ARM starts on clean banks).
 *   ARMED: 1 push/tick (forward fill, page-buffered, program-only).
 *   LOGGING: 2 pops/tick → pack(slot ts, arrival-stamped) → run append;
 *     then push 1 live frame — EXCEPT when the ring emptied after the
 *     pops (v4.77 lesson: push-1/pop-1 at count 1 livelocks) → encode
 *     live direct. Head catches tail exactly at drain end (2:1).
 *   Post-drain: live frames encode direct; the ring only ever carries
 *   the first ≤ 20 s of post-trigger data. Run length is bounded by the
 *   budget, not the ring.
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
#include "ring.h"

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

/* Tick-gap forensics (2026-10-05: 1 real >1 s stall seen in 30 s run —
 * release blocker, JP). Prints any inter-handler gap > 50 ms; localizes
 * the stall to BEFORE the handler (starvation/queue) vs INSIDE it
 * (flash append) when cross-checked with packed dms. */
static uint32_t rc_prev_tick_ms;
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
	uint32_t packed = st_t1 + st_t2 + st_t3;
	uint32_t avg_x10 = packed ? (st_bytes * 10u) / packed : 0;

	printk("{\"rec_stats\":\"%s\",\"frames\":%u,\"bytes\":%u,"
	       "\"avg_b_x10\":%u,\"t1\":%u,\"t2\":%u,\"t3\":%u,"
	       "\"skipped\":%u,\"ring_sim_pages\":%u,"
	       "\"ring_sim_flush_ms_max\":%u}\n",
	       ev, st_frames, st_bytes, avg_x10,
	       st_t1, st_t2, st_t3, st_skipped,
	       store_ring_sim_pages(), store_ring_sim_flush_ms_max());
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
	uint32_t now_ms = k_uptime_get_32();

	if (rc_prev_tick_ms && (now_ms - rc_prev_tick_ms) > 50u) {
		printk("{\"ev\":\"tick_gap\",\"ms\":%u}\n",
		       now_ms - rc_prev_tick_ms);
	}
	rc_prev_tick_ms = now_ms;

	ticks++;

	/* Commands execute in-order with the machine, never beside it. */
	switch (intent) {
	case 1:
		if (rc_state == RC_IDLE && !store_busy()) {
			rc_state = RC_ARMED;
			ticks = 0;
			rc_prev_tick_ms = 0;   /* no gap event across idle */
			rc_stats_reset();
			ring_arm_reset();   /* banks pre-erased (IDLE invariant) */
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

		/* acquire into the pre-trigger bank (arrival ts) */
		if (sensor_prod_get_frame(&fr)) {
			if (!ring_push(&fr, k_uptime_get_32())) {
				st_skipped++;   /* ring exhausted: spec bug */
			}
		} else {
			st_skipped++;
		}
		if (ticks % 10 == 0) {
			detector_10hz();
		}
		if (ticks >= 100 * 20) {    /* 20 s ARM cap (JP, spec v4) */
			printk("{\"rec\":\"IDLE\",\"ev\":\"arm_timeout\"}\n");
			rc_state = RC_IDLE;
			k_timer_stop(&rc_tick);
			ring_erase_banks();   /* IDLE invariant */
			ble_lifecycle_resume();
		}
		break;
	}   /* RC_ARMED */

	case RC_LOGGING: {
		struct sgc_raw_frame fr;
		uint32_t ts;
		bool abort = false;

		/* Drain up to 2 oldest ring slots → run. Slot ts =
		 * ARRIVAL stamp (pre-trigger frames keep their true time). */
		for (int i = 0; i < 2 && ring_count() > 0 && !abort; i++) {
			if (!ring_pop_oldest(&fr, &ts)) {
				break;
			}
			uint8_t sz = sgc_bitpacker_encode(&rc_packer, &fr, ts);
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
				if (n >= 0) {
					printk("{\"rec\":\"POST_RUN\",\"ev\":\"budget_reached\"}\n");
					rc_stats_print("budget_reached");
					store_record_end();
				}
				rc_state = RC_POST_RUN;
				abort = true;
			}
		}
		if (abort) {
			break;
		}

		/* Live frame: through the ring (order-preserving buffer)
		 * ONLY while backlog remains after the pops — otherwise
		 * direct (v4.77: push-1/pop-1 at count 1 livelocks). */
		if (sensor_prod_get_frame(&fr)) {
			if (ring_count() > 0) {
				ring_push(&fr, k_uptime_get_32());
			} else {
				uint8_t sz = sgc_bitpacker_encode(
					&rc_packer, &fr, k_uptime_get_32());
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
					if (n >= 0) {
						printk("{\"rec\":\"POST_RUN\",\"ev\":\"budget_reached\"}\n");
						rc_stats_print("budget_reached");
						store_record_end();
					}
					rc_state = RC_POST_RUN;
					break;
				}
			}
		} else {
			st_skipped++;
		}
		break;
	}

	case RC_POST_RUN:
		if (!store_busy()) {
			printk("{\"rec\":\"IDLE\",\"ev\":\"run_ready\"}\n");
			rc_state = RC_IDLE;
			k_timer_stop(&rc_tick);
			ring_erase_banks();   /* IDLE invariant */
			ble_lifecycle_resume();
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
