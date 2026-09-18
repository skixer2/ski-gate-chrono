/*
 * SGC Zephyr — App 02: BLE GATT skeleton (event-driven)
 * =====================================================
 * New concepts on top of app 01:
 *
 *   1. Bluetooth: enable, advertise (connectable), custom GATT service
 *   2. A notifiable characteristic the phone subscribes to (CCC)
 *   3. The SGC concurrency pattern:
 *          k_timer (ISR ctx)  ->  k_work_submit  ->  work handler (thread ctx)
 *      Nothing blocks. main() just inits and sleeps forever.
 *
 * Demo on the phone (nRF Connect app):
 *   scan -> connect "SGC-Dev" -> service 8d5a0001-... -> subscribe
 *   counter ticks every second; pressing the button notifies immediately.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define APP_VERSION "0.2.0"

/* ------------------------------------------------------------------
 * SGC UUID family — FIXED FOREVER (do not regenerate, ever):
 *   base  8d5aXXXX-84a4-4c5e-8e4f-1c2b3d4f5a6c
 *   0001  service (this app), 0002 counter characteristic
 * The real pull-protocol characteristics will continue this family.
 * ------------------------------------------------------------------ */
/* Note: values inlined (a #define would collapse the macro's 5 args into 1) */
static struct bt_uuid_128 sgc_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8d5a0001, 0x84a4, 0x4c5e,
					   0x8e4f, 0x1c2b3d4f5a6c));
static struct bt_uuid_128 sgc_counter_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8d5a0002, 0x84a4, 0x4c5e,
					   0x8e4f, 0x1c2b3d4f5a6c));

/* Devicetree lookups (same pattern as app 01) */
static const struct device *led  = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led0)));
static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

/* ------------------------------------------------------------------ */
/* GATT service definition                                            */
/* ------------------------------------------------------------------ */

static uint32_t counter;                 /* the value the phone reads   */
static bool     notify_enabled;          /* phone subscribed? (CCC bit) */

/* Called when the phone (re)connects and reads the characteristic */
static ssize_t read_counter(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &counter, sizeof(counter));
}

/* Called when the phone writes the Client Characteristic Configuration
 * (the "subscribe to notifications" switch in nRF Connect) */
static void counter_ccc_changed(const struct bt_gatt_attr *attr,
				uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("phone %s\n", notify_enabled ? "SUBSCRIBED" : "unsubscribed");
}

/* Static, compile-time GATT database. attrs[] indices:
 *   [0] = primary service, [1] = characteristic, [2] = CCC descriptor */
BT_GATT_SERVICE_DEFINE(sgc_svc,
	BT_GATT_PRIMARY_SERVICE(&sgc_svc_uuid),
	BT_GATT_CHARACTERISTIC(&sgc_counter_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_counter, NULL, NULL),
	BT_GATT_CCC(counter_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* ------------------------------------------------------------------ */
/* Concurrency: timers defer to work items, work runs in thread ctx   */
/* ------------------------------------------------------------------ */

static struct k_work  tick_work;         /* 1 Hz: counter + LED + notify */
static struct k_work  button_work;       /* button press -> notify       */
static struct k_timer tick_timer;

static void notify_counter(void)
{
	/* NULL conn = notify every subscriber (we allow 1 anyway) */
	if (notify_enabled) {
		bt_gatt_notify(NULL, &sgc_svc.attrs[1],
			       &counter, sizeof(counter));
	}
}

static void tick_work_handler(struct k_work *w)
{
	counter++;
	notify_counter();

	/* LED heartbeat: on 1 of every 2 ticks (0.5 Hz blink) */
	static bool on;
	uint8_t rgb[3] = { on ? 0xFF : 0x00, 0, 0 };   /* red blink */
	led_set_color(led, 0, 3, rgb);
	on = !on;

	if ((counter % 30) == 0) {
		printk("alive, counter=%u\n", counter);
	}
}

static void button_work_handler(struct k_work *w)
{
	/* Thread context: safe to do real work here */
	printk("button -> notify counter=%u\n", counter);
	notify_counter();
}

/* k_timer expiry runs in CLOCK ISR context: schedule, never work */
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

/* ------------------------------------------------------------------ */
/* Bluetooth bring-up                                                 */
/* ------------------------------------------------------------------ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void bt_ready(int err)
{
	if (err) {
		printk("ERROR: bt_enable -> %d\n", err);
		return;
	}
	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("ERROR: adv start -> %d\n", err);
		return;
	}
	printk("Advertising as %s\n", CONFIG_BT_DEVICE_NAME);
}

int main(void)
{
	printk("SGC app-02 ble_hello v%s\n", APP_VERSION);

	if (!device_is_ready(led)) {
		printk("ERROR: LED device not ready\n");
		return 0;
	}

	/* Button (same as app 01) */
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
		return 0;
	}

	/* Wire the timer->work chain and start it */
	k_work_init(&tick_work, tick_work_handler);
	k_work_init(&button_work, button_work_handler);
	k_timer_init(&tick_timer, tick_timer_expiry, NULL);
	k_timer_start(&tick_timer, K_SECONDS(1), K_SECONDS(1));

	/* bt_enable is async: bt_ready callback fires when the stack is up */
	int err = bt_enable(bt_ready);
	if (err) {
		printk("ERROR: bt_enable -> %d\n", err);
	}

	/* Event-driven: this thread has nothing left to do */
	printk("init complete, entering idle\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
