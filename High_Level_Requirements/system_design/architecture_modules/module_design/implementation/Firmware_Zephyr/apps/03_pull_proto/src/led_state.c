/*
 * led_state.c — LED as link-state indicator (step 3c)
 * ====================================================
 *   connected    -> solid green
 *   advertising  -> blue blink (toggle each 1 s tick = 0.5 Hz)
 *
 * Heri­tage: the blink was app-02's dumb heartbeat (v0.2.0), became
 * state-driven in v0.2.1, lived inside the demo counter's tick until
 * 3c gave it its own timer — visual behavior identical, ownership
 * clean (the demo service no longer renders hardware).
 *
 * Nicla color quirk (verified vs zephyr v4.4): the board DTS maps the
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

static struct k_work        led_work;
static struct k_timer       led_timer;
/* SLEEP pulse off-edge (JP 2026-10-27: 100 ms blue every 2 s). Timer
 * expiry runs in ISR context — I2C writes must go through sysworkq. */
static struct k_work        led_off_work;
static struct k_timer       led_off_timer;

static void led_off_work_handler(struct k_work *w)
{
	const uint8_t off[3] = { 0x00, 0x00, 0x00 };

	led_set_color(led, 0, 3, off);
}

static void led_work_handler(struct k_work *w)
{
	/* Array maps <B,G,R> (index 0 = BLUE die — verified quirk).
	 * Run state outranks link state (JP 2026-10-14):
	 *   red    = LOGGING   (hard evidence — record in progress)
	 *   yellow = ARMED     (waiting for the drop; distinct from
	 *                       green = BLE connected)
	 *   green  = connected (no run activity)
	 *   blue blink = advertising
	 */
	uint8_t st = recorder_state_idx();
	if (st == 2) {                       /* LOGGING */
		const uint8_t red[3] = { 0x00, 0x00, 0xFF };
		led_set_color(led, 0, 3, red);
	} else if (st == 1) {                /* ARMED */
		const uint8_t yellow[3] = { 0x00, 0xFF, 0xFF };
		led_set_color(led, 0, 3, yellow);
	} else if (ble_lifecycle_connected()) {
		const uint8_t green[3] = { 0x00, 0xFF, 0x00 };
		led_set_color(led, 0, 3, green);
	} else {
		/* SLEEP heartbeat (JP 2026-10-27): 100 ms blue pulse every
		 * 2 s (5% duty — findable in the dark, ~negligible current).
		 * 1 s tick alternates pulse/dark slots -> 2 s period; the
		 * 100 ms off-edge fires via the one-shot off-timer. */
		static bool pulse_slot;
		const uint8_t blue[3] = { 0xFF, 0x00, 0x00 };

		if (pulse_slot) {
			led_set_color(led, 0, 3, blue);
			k_timer_start(&led_off_timer, K_MSEC(100), K_FOREVER);
		} else {
			led_set_color(led, 0, 3, (const uint8_t[3]){ 0 });
		}
		pulse_slot = !pulse_slot;
	}
}

static void led_timer_expiry(struct k_timer *t)
{
	k_work_submit(&led_work);
}

static void led_off_timer_expiry(struct k_timer *t)
{
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
	k_timer_start(&led_timer, K_SECONDS(1), K_SECONDS(1));
	return 0;
}
