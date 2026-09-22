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

static const struct device *led =
	DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led0)));

static struct k_work        led_work;
static struct k_timer       led_timer;

static void led_work_handler(struct k_work *w)
{
	if (ble_lifecycle_connected()) {
		const uint8_t green[3] = { 0x00, 0xFF, 0x00 };
		led_set_color(led, 0, 3, green);
	} else {
		static bool on;
		const uint8_t blue[3] = { on ? 0xFF : 0x00, 0x00, 0x00 };

		led_set_color(led, 0, 3, blue);
		on = !on;
	}
}

static void led_timer_expiry(struct k_timer *t)
{
	k_work_submit(&led_work);
}

int led_state_init(void)
{
	if (!device_is_ready(led)) {
		printk("ERROR: LED device not ready\n");
		return -1;
	}
	k_work_init(&led_work, led_work_handler);
	k_timer_init(&led_timer, led_timer_expiry, NULL);
	k_timer_start(&led_timer, K_SECONDS(1), K_SECONDS(1));
	return 0;
}
