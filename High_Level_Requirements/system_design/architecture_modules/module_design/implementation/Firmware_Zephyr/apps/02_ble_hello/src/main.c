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
 * v0.2.1 adds the connection lifecycle:
 *
 *   4. bt_conn_cb callbacks (connected / disconnected) — the stack calls
 *      us from its RX thread, exactly like an interrupt for the link.
 *   5. LED now shows STATE, not liveness: blue blink = advertising,
 *      solid green = phone connected. (Real SGC UX will follow this.)
 *   6. Self-recovery: after any disconnect we re-advertise on our own —
 *      no reboot, no user action. The device must ALWAYS be findable
 *      again; the pull protocol (step 3) depends on this pattern.
 *
 * Demo on the phone (nRF Connect app):
 *   scan -> connect "SGC-Dev"  (LED: blue blink -> solid green)
 *   -> service 8d5a0001-... -> subscribe: counter ticks every second
 *   -> press the button: immediate extra notification
 *   -> disconnect: LED blinks blue again, "SGC-Dev" re-appears in the
 *      scanner on its own.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define APP_VERSION "0.2.1"

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

/* Called when the phone reads the characteristic. Manual implementation:
 * honor offset/len (BLE reads can be fragmented) and copy out the value.
 * (The bt_gatt_attr_read() helper is Kconfig-gated out in this build.) */
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

/* Called when the phone writes the Client Characteristic Configuration
 * (the "subscribe to notifications" switch in nRF Connect).
 * Note: also fires with value 0 when the phone DISCONNECTS — the stack
 * clears subscriptions when the link drops, so notify_enabled always
 * reflects reality without extra bookkeeping in the disconnect path. */
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

/* Link state. Written from the BT stack's RX thread (callbacks below),
 * read from the sysworkq thread (tick_work_handler). One byte with a
 * single writer — plain volatile is honest here. The day state grows to
 * multiple fields, switch to atomics or a mutex (two volatiles can be
 * torn against each other). */
static volatile bool connected;

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

	/* LED = link state (was a dumb heartbeat in v0.2.0):
	 *   connected   -> solid green
	 *   advertising -> blue blink (toggle each 1 s tick = 0.5 Hz)
	 * Nicla color quirk: this board's DTS maps the IS31FL3194's three
	 * channels as <B,G,R>, and the driver writes our array to the
	 * channels AS-IS — index 0 = blue die, 2 = red die. */
	if (connected) {
		const uint8_t green[3] = { 0x00, 0xFF, 0x00 };
		led_set_color(led, 0, 3, green);
	} else {
		static bool on;
		const uint8_t blue[3] = { on ? 0xFF : 0x00, 0x00, 0x00 };
		led_set_color(led, 0, 3, blue);
		on = !on;
	}

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
/* Bluetooth bring-up + connection lifecycle                          */
/* ------------------------------------------------------------------ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* One helper, three callers (bt_ready / failed connect / disconnect):
 * (re)start connectable advertising. Legacy advertising stops by itself
 * the moment a connection lands — so after ANY disconnect we must call
 * this again or the device turns invisible until reboot. -EALREADY just
 * means "already advertising" — fine. */
static void start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err && err != -EALREADY) {
		printk("ERROR: adv start -> %d\n", err);
	}
}

/* Connection callbacks run in the BT stack's RX thread — treat like an
 * ISR: keep it short, only set flags / printk / start advertising. */
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		/* Rare: the link failed while forming. Recover by
		 * advertising again — never get stuck invisible. */
		printk("connect FAILED from %s (err %u), re-advertising\n",
		       addr, err);
		start_advertising();
		return;
	}

	connected = true;
	printk("connected: %s (LED -> solid green)\n", addr);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	connected = false;

	/* reason codes worth recognizing (same numbers nRF Connect shows):
	 *   0x13 remote terminated connection  — phone hung up cleanly
	 *   0x08 connection supervision timeout — link died, our old
	 *        S22 "wedge" friend from the Arduino bench sessions */
	printk("disconnected: %s (reason 0x%02x), re-advertising\n",
	       addr, reason);

	start_advertising();
}

/* Compile-time registration: this macro places the struct in a linker
 * section the BT stack walks at boot — no bt_conn_cb_register() call
 * needed. (The runtime variant exists if callbacks must be dynamic.) */
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected_cb,
	.disconnected = disconnected_cb,
};

static void bt_ready(int err)
{
	if (err) {
		printk("ERROR: bt_enable -> %d\n", err);
		return;
	}
	printk("Bluetooth initialized\n");

	start_advertising();
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
