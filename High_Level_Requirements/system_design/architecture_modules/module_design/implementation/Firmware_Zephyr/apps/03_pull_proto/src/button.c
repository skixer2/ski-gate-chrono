/*
 * button.c - piezo pushbutton arming (F03, piezo rework JP 2026-11-04)
 * =================================================================
 *   SLEEP + press -> ARMED          (single press, same path as serial 'a')
 *   ARMED/LOGGING/POST_RUN + press -> ignored
 *
 * JP 2026-11-04 (verbatim rule): "Piezo. We reject any pressure when
 * not idle or sleeping." The anti-bump mechanism is STATE-GATED, not
 * duration-gated: bumps and knocks happen while the device is awake
 * and armed, so they are rejected by state. The 1 s hold gate
 * (2026-10-15, written for the bench monostable switch) is REMOVED -
 * a piezo emits a 20-1000 ms pulse and physically cannot hold 1 s,
 * so on the real Langir button it would never arm.
 *
 * Hardware: sealed piezo button (Langir 16 mm) or monostable switch,
 * P0.02 to GND via the protection network (J5 -> R41 100k -> clamps,
 * sgc_p1 sch_connectors), internal pull-up. Zephyr DT gpio pin numbers
 * are PHYSICAL nRF pins - P0.02 is index 2.
 *
 * Debounce: falling edge -> verify still low after 20 ms (F03), then a
 * 200 ms quiet window so piezo ring/echo cannot double-trigger.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "button.h"
#include "recorder.h"

/* F03 wiring: monostable switch from P0.02 to GND. Pin is a design
 * constant (the physical-pin rule: nRF P0.02 = Zephyr gpio0 pin 2). */
#define BTN_PORT DT_NODELABEL(gpio0)
#define BTN_PIN  2

/* Piezo pulse is 20-1000 ms; after a validated press, swallow edges for
 * the quiet window so the release ring cannot count as a second press. */
#define DEBOUNCE_MS 20
#define QUIET_MS    200

static const struct device *btn = DEVICE_DT_GET(BTN_PORT);

static struct gpio_callback btn_cb;
static struct k_timer debounce_timer;
static int64_t last_press_ms;

static void debounce_expired(struct k_timer *t)
{
	int64_t now = k_uptime_get();

	if (gpio_pin_get_raw(btn, BTN_PIN) != 0) {
		return;   /* released within the window - bounce, not a press */
	}
	if (now - last_press_ms < QUIET_MS) {
		return;   /* piezo ring inside the quiet window - swallow */
	}

	uint8_t st = recorder_state_idx();

	if (st != 0) {
		/* JP rule: reject any pressure when not idle or sleeping.
		 * SLEEP(0) is the only idle state (ARMED=1 LOGGING=2 POST_RUN=3). */
		printk("button press in state %u ignored\n", st);
		return;
	}

	last_press_ms = now;
	printk("{\"ev\":\"button_arm\",\"src\":\"piezo\"}\n");
	recorder_arm();
}

static void btn_isr(const struct device *port,
		    struct gpio_callback *cb, uint32_t pins)
{
	/* Edge now; the timer re-reads the line after the debounce window. */
	k_timer_start(&debounce_timer, K_MSEC(DEBOUNCE_MS), K_NO_WAIT);
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
	last_press_ms = 0;
	printk("button: arming input P0.%02u ready (F03, piezo single-press, "
	       "state-gated)\n", BTN_PIN);
	return 0;
}

void button_arm_systemoff_wake(void)
{
	/* System OFF wake (F12/F13): only the SENSE (level) pin config
	 * survives System OFF - PIN_CNF.SENSE drives the DETECT signal
	 * that wakes the nRF52 into a cold boot (rr:4). The edge config
	 * above would NOT survive. Same recipe as the Zephyr
	 * samples/boards/nordic/system_off GPIO wake ("input + interrupt
	 * as level active to allow wake-up"); this button is active-LOW
	 * (pull-up, switch to GND) -> GPIO_INT_LEVEL_LOW. The wake cold
	 * boot runs button_init() again and re-arms the normal edge. */
	gpio_pin_configure(btn, BTN_PIN, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure(btn, BTN_PIN, GPIO_INT_LEVEL_LOW);
}
