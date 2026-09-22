/*
 * demo_svc.c — 8d5a demo service: 1 Hz counter + button notify (3c)
 * ==================================================================
 * The teaching service from app-02, kept as a live canary: counter
 * ticking proves the k_timer→k_work chain, the button proves
 * ISR→work→notify, and the honest print reminds us BLE forbids
 * notifications to non-subscribers (v0.3.0 polish).
 *
 * UUID family 8d5aXXXX-84a4-4c5e-8e4f-1c2b3d4f5a6c (demo-only; the
 * product surface is pull_proto's 0x5347 family).
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include "demo_svc.h"

static struct bt_uuid_128 sgc_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8d5a0001, 0x84a4, 0x4c5e,
					   0x8e4f, 0x1c2b3d4f5a6c));
static struct bt_uuid_128 sgc_counter_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8d5a0002, 0x84a4, 0x4c5e,
					   0x8e4f, 0x1c2b3d4f5a6c));

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static uint32_t counter;
static bool     notify_enabled;

/* Manual offset-aware read (bt_gatt_attr_read helper stays
 * Kconfig-gated out — app-02 lesson). */
static ssize_t read_counter(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf, uint16_t len, uint16_t offset)
{
	if (offset > sizeof(counter)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	uint16_t frag = MIN(len, sizeof(counter) - offset);

	memcpy(buf, (const uint8_t *)&counter + offset, frag);
	return frag;
}

static void counter_ccc_changed(const struct bt_gatt_attr *attr,
				uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("phone %s (demo counter)\n",
	       notify_enabled ? "SUBSCRIBED" : "unsubscribed");
}

BT_GATT_SERVICE_DEFINE(demo_svc,
	BT_GATT_PRIMARY_SERVICE(&sgc_svc_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&sgc_counter_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_counter, NULL, NULL),
	BT_GATT_CCC(counter_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static void notify_counter(void)
{
	if (notify_enabled) {
		bt_gatt_notify(NULL, &demo_svc.attrs[1],
			       &counter, sizeof(counter));
	}
}

static struct k_work  tick_work;
static struct k_work  button_work;
static struct k_timer tick_timer;

static void tick_work_handler(struct k_work *w)
{
	counter++;
	notify_counter();
	if ((counter % 30) == 0) {
		printk("alive, counter=%u\n", counter);
	}
}

static void button_work_handler(struct k_work *w)
{
	if (notify_enabled) {
		printk("button -> notify counter=%u\n", counter);
		notify_counter();
	} else {
		/* honest since v0.3.0: no subscriber = nothing on air */
		printk("button (nobody subscribed), counter=%u\n", counter);
	}
}

static void tick_timer_expiry(struct k_timer *t)
{
	k_work_submit(&tick_work);
}

static struct gpio_callback button_cb;

static void button_isr(const struct device *port,
		       struct gpio_callback *cb, uint32_t pins)
{
	/* ISR context: only enqueue, nothing else */
	k_work_submit(&button_work);
}

int demo_svc_start(void)
{
	int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);

	if (ret == 0) {
		ret = gpio_pin_interrupt_configure_dt(&button,
						      GPIO_INT_EDGE_TO_ACTIVE);
	}
	if (ret == 0) {
		gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
		ret = gpio_add_callback(button.port, &button_cb);
	}
	if (ret != 0) {
		printk("ERROR: button setup -> %d\n", ret);
		return -1;
	}

	k_work_init(&tick_work, tick_work_handler);
	k_work_init(&button_work, button_work_handler);
	k_timer_init(&tick_timer, tick_timer_expiry, NULL);
	k_timer_start(&tick_timer, K_SECONDS(1), K_SECONDS(1));
	return 0;
}
