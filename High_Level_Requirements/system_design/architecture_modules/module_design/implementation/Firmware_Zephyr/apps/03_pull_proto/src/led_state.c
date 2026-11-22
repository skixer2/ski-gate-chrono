/*
 * led_state.c — LED as status indicator, TEMPORAL encoding
 * ========================================================
 * F41 (JP 2026-11-22): every state is encoded by FLASH RHYTHM, not by a
 * steady colour. A blinking LED is identifiable in direct sun and at a
 * distance where a fixed hue is not. Rhythm is the primary cue; colour is
 * secondary.
 *
 *   ARMED       ORANGE  1 Hz    50 % duty  (500 on / 500 off)
 *   SLEEP       BLUE    pulse   100 ms every 2 s (5 %)
 *   advertising BLUE    0.5 Hz  50 % duty  (1000 on / 1000 off)
 *   connected   GREEN   0.4 Hz  33 % duty  (slow breathe-ish blip)
 *   calib >= 2  GREEN   1 Hz    20 % duty  (double-flash feel, see note)
 *   uncalib     BLUE    0.33 Hz 45 % duty  (slow flow)
 *   LOGGING     RED     4 Hz    50 % duty  (125 on / 125 off)
 *   error/lowb  YELLOW  8 Hz    50 % duty  (62 on / 62 off)
 *
 * Implementation: one repeating timer whose period is the state's
 * HALF-period (so the handler toggles on/off at 50 %), plus a one-shot
 * "off" timer for duties below 50 %. The old design used a fixed 1 s
 * tick; a 1 s tick cannot produce 4 Hz or 8 Hz, and rate is what
 * carries urgency.
 *
 * Phase reset on state change is mandatory: without it a glance during
 * the OFF half could be misread as "no state" / idle. `cur_state` tracks
 * the rendered state and forces an ON edge whenever it changes.
 *
 * Context: LED writes are I²C and MUST NOT run in ISR/timer-expiry
 * context — the expiry handlers only submit work to sysworkq.
 *
 * Nicla colour quirk (verified vs zephyr v4.4): the board DTS maps the
 * IS31FL3194's three channels <B,G,R> and the driver writes our array
 * to channels AS-IS — index 0 = BLUE die, index 2 = red die.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include "led_state.h"
#include "ble_lifecycle.h"
#include "recorder.h"

static const struct device *led =
	DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led0)));

/* ---- colour constants (array maps <B,G,R>) -------------------------- */
static const uint8_t C_OFF[3]    = { 0x00, 0x00, 0x00 };
static const uint8_t C_BLUE[3]   = { 0xFF, 0x00, 0x00 };
static const uint8_t C_GREEN[3]  = { 0x00, 0xFF, 0x00 };
/* R-heavy orange: an equal-mix yellow READ as green on the SMLP34 because
 * the green die dominates (JP 2026-11-08). */
static const uint8_t C_ORANGE[3] = { 0x00, 0x40, 0xFF };
static const uint8_t C_RED[3]    = { 0x00, 0x00, 0xFF };
static const uint8_t C_YELLOW[3] = { 0x00, 0xC0, 0xFF };

/* ---- pattern table ------------------------------------------------- */
/* period_ms = HALF period. duty_ms = ON duration within that half-period
 * slot; when duty_ms == period_ms the state is a 50 % square wave driven
 * purely by the repeating timer. duty_ms < period_ms additionally arms the
 * one-shot off-timer to cut the LED early. */
struct led_pattern {
	uint16_t period_ms;   /* toggle interval (= half of blink period) */
	uint16_t duty_ms;     /* ON time (<= period_ms)                   */
	const uint8_t *rgb;
};

enum led_state_id {
	LS_ARMED = 0,
	LS_LOGGING,
	LS_SLEEP,
	LS_ADVERTISING,
	LS_CONNECTED,
	LS_ERROR,
	LS_COUNT
};

static const struct led_pattern PATTERNS[LS_COUNT] = {
	/* ARMED       1 Hz, 50 %  -> 500 ms half  */
	[LS_ARMED]       = {  500, 500, C_ORANGE },
	/* LOGGING     4 Hz, 50 %  -> 125 ms half  */
	[LS_LOGGING]     = {  125, 125, C_RED    },
	/* SLEEP       100 ms pulse every 2 s -> 1000 ms half, 100 ms on */
	[LS_SLEEP]       = { 1000, 100, C_BLUE   },
	/* ADVERTISING 0.5 Hz, 50 % -> 1000 ms half */
	[LS_ADVERTISING] = { 1000, 1000, C_BLUE  },
	/* CONNECTED   0.4 Hz, ~33 % -> 1250 ms half, 400 ms on */
	[LS_CONNECTED]   = { 1250, 400, C_GREEN  },
	/* ERROR       8 Hz, 50 %   -> 62 ms half  */
	[LS_ERROR]       = {   62, 62, C_YELLOW  },
};

static struct k_work  led_work;
static struct k_timer led_timer;
static struct k_work  led_off_work;
static struct k_timer led_off_timer;

/* Render state, touched only from sysworkq (single-writer). */
static enum led_state_id rendered = LS_COUNT;   /* LS_COUNT = "nothing yet" */
static bool led_is_on;

/* ---- state resolution ---------------------------------------------- */
/* Run state outranks link state (JP 2026-10-14). recorder_state_idx():
 * 0=SLEEP 1=ARMED 2=LOGGING 3=POST_RUN. SLEEP is not reachable while the
 * LED renders (the render tick is stopped on power-off), so state 0 and
 * POST_RUN fall through to the link-state branch. */
static enum led_state_id resolve_state(void)
{
	uint8_t st = recorder_state_idx();

	if (st == 2) {
		return LS_LOGGING;
	}
	if (st == 1) {
		return LS_ARMED;
	}
	if (ble_lifecycle_connected()) {
		return LS_CONNECTED;
	}
	return LS_ADVERTISING;
}

/* ---- work handlers (sysworkq: I²C allowed here) --------------------- */
static void led_off_work_handler(struct k_work *w)
{
	ARG_UNUSED(w);
	const struct led_pattern *p = &PATTERNS[rendered];

	/* Only cut the LED if this pattern is a sub-50 % duty one. */
	if (rendered < LS_COUNT && p->duty_ms < p->period_ms) {
		led_set_color(led, 0, 3, C_OFF);
		led_is_on = false;
	}
}

static void led_work_handler(struct k_work *w)
{
	ARG_UNUSED(w);

	enum led_state_id want = resolve_state();

	if (want != rendered) {
		/* PHASE RESET: on any state change force an ON edge immediately,
		 * so a mid-off glance can never be mistaken for "idle". Also
		 * re-time the repeating timer to the new half-period. */
		rendered = want;
		led_is_on = false;
		k_timer_start(&led_timer, K_MSEC(PATTERNS[want].period_ms),
			      K_MSEC(PATTERNS[want].period_ms));
	}

	const struct led_pattern *p = &PATTERNS[rendered];

	if (led_is_on) {
		led_set_color(led, 0, 3, C_OFF);
		led_is_on = false;
		return;
	}

	led_set_color(led, 0, 3, p->rgb);
	led_is_on = true;

	/* Sub-50 % duty: schedule the early OFF edge. */
	if (p->duty_ms < p->period_ms) {
		k_timer_start(&led_off_timer, K_MSEC(p->duty_ms), K_FOREVER);
	}
}

static void led_timer_expiry(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_work_submit(&led_work);
}

static void led_off_timer_expiry(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_work_submit(&led_off_work);
}

int led_state_init(void)
{
	if (!device_is_ready(led)) {
		printk("ERROR: LED device not ready\n");
		return -1;
	}
	k_work_init(&led_work, led_work_handler);
	k_work_init(&led_off_work, led_off_work_handler);
	k_timer_init(&led_timer, led_timer_expiry, NULL);
	k_timer_init(&led_off_timer, led_off_timer_expiry, NULL);

	/* Seed with the advertising cadence; the first work item immediately
	 * re-times to whatever state is actually current. */
	k_timer_start(&led_timer, K_MSEC(PATTERNS[LS_ADVERTISING].period_ms),
		      K_MSEC(PATTERNS[LS_ADVERTISING].period_ms));
	return 0;
}

void led_state_all_off(void)
{
	/* 0.23.5 System-Off latch fix (bench 2026-10-28: solid blue, no
	 * BLE, "dead" device after the 1 h timer fired): the IS31FL3194
	 * is powered from the rail and RETAINS its last I²C state when
	 * the SoC dies — if sys_poweroff() lands inside a lit pulse, the
	 * die burns until VDD drops. Called from the power-off path: clear
	 * + stop BOTH timers so no edge can land between this and
	 * sys_poweroff(). */
	k_timer_stop(&led_timer);
	k_timer_stop(&led_off_timer);
	/* drop any queued render works — they would re-light the die */
	k_work_cancel(&led_work);
	k_work_cancel(&led_off_work);
	led_set_color(led, 0, 3, C_OFF);
	led_is_on = false;
	rendered = LS_COUNT;
}
