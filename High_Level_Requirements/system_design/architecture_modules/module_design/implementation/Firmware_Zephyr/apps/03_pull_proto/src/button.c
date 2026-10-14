/*
 * button.c — piezo/monostable pushbutton arming (F03, JP 2026-10-14)
 * =================================================================
 *   IDLE + press → ARMED   (same path as serial 'a')
 *   ARMED/LOGGING/POST_RUN + press → ignored
 *
 * Hardware: monostable switch (JP's bench piece) or sealed piezo,
 * P0.02 to GND, internal pull-up. Zephyr DT gpio pin numbers are
 * PHYSICAL nRF pins — P0.02 is index 2 (the old Arduino-pin trap).
 * Debounce: falling edge → verify still low after 20 ms (F03).
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "button.h"
#include "recorder.h"

#define WAKE_BUTTON_NODE DT_ALIAS(wake_button)
#if !DT_NODE_HAS_STATUS(WAKE_BUTTON_NODE, okay)
#error "wake-button alias missing — add to app.overlay (gpio0 pin 2)"
#endif

static const struct device *btn =
	DEVICE_DT_GET(DT_GPIO_CTLR(WAKE_BUTTON_NODE, gpios));
static const uint8_t btn_pin = DT_GPIO_PIN(WAKE_BUTTON_NODE, gpios);

static struct gpio_callback btn_cb;
static struct k_timer debounce_timer;

static void debounce_expired(struct k_timer *t)
{
	int val = gpio_pin_get_raw(btn, btn_pin);
	if (val != 0) {
		return;   /* released within the window — bounce, not a press */
	}

	uint8_t st = recorder_state_idx();
	if (st == 0) {          /* IDLE → ARMED */
		printk("{\"ev\":\"button_arm\"}\n");
		recorder_arm();
	} else {
		printk("button press in state %u ignored\n", st);
	}
}

static void btn_isr(const struct device *port,
		    struct gpio_callback *cb, uint32_t pins)
{
	/* Edge now; the timer re-reads the line after the 20 ms window. */
	k_timer_start(&debounce_timer, K_MSEC(20), K_NO_WAIT);
}

int button_init(void)
{
	if (!device_is_ready(btn)) {
		printk("ERROR: button GPIO device not ready\n");
		return -1;
	}
	/* Internal pull-up, switch closes to GND (F03 wiring). */
	gpio_pin_configure(btn, btn_pin, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure(btn, btn_pin, GPIO_INT_EDGE_FALLING);
	gpio_init_callback(&btn_cb, btn_isr, BIT(btn_pin));
	gpio_add_callback(btn, &btn_cb);
	k_timer_init(&debounce_timer, debounce_expired, NULL);
	printk("button: arming input P0.%02u ready (F03)\n", btn_pin);
	return 0;
}
