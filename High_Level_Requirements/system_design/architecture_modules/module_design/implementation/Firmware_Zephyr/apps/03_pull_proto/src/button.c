/*
 * button.c — piezo/monostable pushbutton arming (F03, JP 2026-10-14)
 * =================================================================
 *   SLEEP + HOLD ≥ 1 s → ARMED   (same path as serial 'a')
 *   ARMED/LOGGING/POST_RUN + press → ignored
 *
 * Hardware: monostable switch (JP's bench piece) or sealed piezo,
 * P0.02 to GND, internal pull-up. Zephyr DT gpio pin numbers are
 * PHYSICAL nRF pins — P0.02 is index 2 (the old Arduino-pin trap).
 * Debounce: falling edge → verify still low after 20 ms (F03).
 * Hold gate (JP 2026-10-15): arming needs 1 s of CONTINUOUS press —
 * pole knocks / packing bumps are short and must not arm the device.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "button.h"
#include "recorder.h"

/* F03 wiring: monostable switch from P0.02 to GND. Pin is a design
 * constant (the physical-pin rule: nRF P0.02 = Zephyr gpio0 pin 2). */
#define BTN_PORT DT_NODELABEL(gpio0)
#define BTN_PIN  2

static const struct device *btn = DEVICE_DT_GET(BTN_PORT);

static struct gpio_callback btn_cb;
static struct k_timer debounce_timer;
static struct k_timer hold_timer;

/* Hold gate: sample every 100 ms while pressed; 10 ticks = 1 s. */
#define HOLD_TICK_MS       100
#define HOLD_TICKS_NEEDED  10

static uint8_t btn_hold_ticks;

/* The resting state is SLEEP (0) since v0.23.0 — the hold-gate arms
 * straight out of it (F13 wake path; no re-init, < 100 ms to ISR→work
 * even right after a tickless idle wake). */
static void hold_tick(struct k_timer *t)
{
	if (gpio_pin_get_raw(btn, BTN_PIN) != 0) {
		btn_hold_ticks = 0;
		k_timer_stop(t);
		return;   /* released before 1 s — bump, not an arm intent */
	}
	if (++btn_hold_ticks < HOLD_TICKS_NEEDED) {
		return;
	}
	btn_hold_ticks = 0;
	k_timer_stop(t);

	uint8_t st = recorder_state_idx();

	if (st == 0) {          /* SLEEP → ARMED after a deliberate 1 s hold */
		printk("{\"ev\":\"button_arm\",\"hold_ms\":1000}\n");
		recorder_arm();
	} else {
		printk("button press in state %u ignored\n", st);
	}
}

static void debounce_expired(struct k_timer *t)
{
	int val = gpio_pin_get_raw(btn, BTN_PIN);
	if (val != 0) {
		return;   /* released within the window — bounce, not a press */
	}

	uint8_t st = recorder_state_idx();
	if (st != 0) {
		printk("button press in state %u ignored\n", st);
		return;
	}
	/* Real press in SLEEP — start the 1 s hold gate (arm at 10 ticks). */
	btn_hold_ticks = 0;
	k_timer_start(&hold_timer, K_MSEC(HOLD_TICK_MS), K_MSEC(HOLD_TICK_MS));
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
	gpio_pin_configure(btn, BTN_PIN, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure(btn, BTN_PIN, GPIO_INT_EDGE_FALLING);
	gpio_init_callback(&btn_cb, btn_isr, BIT(BTN_PIN));
	gpio_add_callback(btn, &btn_cb);
	k_timer_init(&debounce_timer, debounce_expired, NULL);
	k_timer_init(&hold_timer, hold_tick, NULL);
	printk("button: arming input P0.%02u ready (F03, hold 1 s)\n", BTN_PIN);
	return 0;
}

void button_arm_systemoff_wake(void)
{
	/* System OFF wake (F12/F13): only the SENSE (level) pin config
	 * survives System OFF — PIN_CNF.SENSE drives the DETECT signal
	 * that wakes the nRF52 into a cold boot (rr:4). The edge config
	 * above would NOT survive. Same recipe as the Zephyr
	 * samples/boards/nordic/system_off GPIO wake ("input + interrupt
	 * as level active to allow wake-up"); this button is active-LOW
	 * (pull-up, switch to GND) → GPIO_INT_LEVEL_LOW. The wake cold
	 * boot runs button_init() again and re-arms the normal edge. */
	gpio_pin_configure(btn, BTN_PIN, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure(btn, BTN_PIN, GPIO_INT_LEVEL_LOW);
}
