/*
 * SGC Zephyr — App 01: button + LED heartbeat + serial banner
 * ============================================================
 * Purpose: the smallest complete Zephyr app that touches every concept
 * we will reuse in the real SGC firmware:
 *
 *   1. Devicetree lookups  (LED on I2C chip, button on GPIO)
 *   2. Interrupt callbacks (button press, from P0.21)
 *   3. The main loop       (heartbeat LED + periodic status line)
 *   4. printk              (the console you watch with zserial.py)
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_version.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>

#define APP_VERSION "0.1.0"

/* Heartbeat timing: LED on 100 ms, off 1900 ms = one "beat" every 2 s. */
#define HB_ON_MS  100
#define HB_OFF_MS 1900

/*
 * Devicetree node references, resolved at BUILD time.
 * DT_ALIAS(led0) is the alias we added in app.overlay.
 * DT_ALIAS(sw0)  is the button, declared by the board file (P0.21).
 * These macros expand to compile-time constants, not string lookups.
 */
static const struct device *led  = DEVICE_DT_GET(DT_ALIAS(led0));
static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/*
 * Button interrupt: ARM GPIO callback object (must be static — it lives
 * in driver-owned memory after gpio_add_callback). Falling edge only,
 * because the button is active-low (pressed = wire pulled to GND).
 */
static struct gpio_callback button_cb;

static void button_pressed(const struct device *port,
			   struct gpio_callback *cb, uint32_t pins)
{
	/* Runs in interrupt context: keep it minimal, no busy loops. */
	printk(">> button pressed (P0.21)\n");
}

static int setup_button(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("ERROR: button device not ready\n");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}

	/* Internal pull-up: the button only connects the pin to GND. */
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return ret;
	}

	gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
	return gpio_add_callback(button.port, &button_cb);
}

int main(void)
{
	printk("SGC app-01 button_led v%s\n", APP_VERSION);
	/* Packed version: bits 31-24 major, 23-16 minor, 15-8 patch */
	uint32_t kv = sys_kernel_version_get();

	printk("Zephyr %u.%u.%u on %s\n",
	       (kv >> 24) & 0xFF, (kv >> 16) & 0xFF, (kv >> 8) & 0xFF,
	       CONFIG_BOARD);

	if (!device_is_ready(led)) {
		printk("ERROR: LED device not ready\n");
		return 0;
	}

	if (setup_button() != 0) {
		printk("ERROR: button setup failed\n");
		return 0;
	}

	uint32_t beats = 0;

	while (1) {
		led_on(led, 0);		/* LED index 0 = the RGB unit, all channels */
		k_msleep(HB_ON_MS);
		led_off(led, 0);
		k_msleep(HB_OFF_MS);

		beats++;
		if ((beats % 15) == 0) {	/* status line every 30 s */
			printk("alive, %u beats\n", beats);
		}
	}

	return 0;	/* unreachable */
}
