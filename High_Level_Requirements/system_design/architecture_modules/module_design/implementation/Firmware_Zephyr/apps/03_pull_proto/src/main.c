/*
 * SGC Zephyr — App 03: pull-protocol skeleton (step 3a)
 * =====================================================
 * Everything from app-02 v0.2.2 is still here (demo service, LED-as-state,
 * deferred adv restart). On top of it: the REAL product protocol service,
 * verbatim from ArduinoFW 5.89 (pull_transfer.cpp) — the wire format the
 * unchanged phone App 1.46 already speaks.
 *
 * ── The product service (from the ArduinoFW SGC_UUID macro) ──────────
 *
 *   service  0x5347  "SG"  (16-bit UUID on the Bluetooth base UUID —
 *                          exactly what ArduinoFW advertises)
 *   char     0xABCA  console: phone WRITES ASCII command lines here,
 *                          device NOTIFYies text lines / binary frames back
 *
 * ── Commands (one ASCII line per write) ──────────────────────────────
 *
 *   "D"                    → run directory: one JSON line per run
 *                            {"run":id,"ts":..,"sz":..,"side":..}
 *                            then the terminator {"d_end":n}
 *   "s <id> <off> <len>"   → binary stream: frames of
 *                            [seq:2B LE][len:1B][payload ≤ 240 B]
 *                            paced 25 ms/frame, terminated by the
 *                            END marker FF FF 00 (seq=0xFFFF, len=0)
 *   replies "E <n>"        → E1 bad run · E2 bad off/len
 *                            E3 malformed · E4 unknown command
 *
 * Stateless by design: every request carries (id, off, len). A disconnect
 * costs one reconnect, not a session (SR-2 in the redesign doc).
 *
 * ── What is FAKE in 3a (the 3b upgrade list) ─────────────────────────
 *
 *   - run data = a RAM ramp pattern (byte = offset & 0xFF), 2 fake dir
 *     entries — so you can EYEBALL frames in nRF Connect. 3b reads real
 *     flash blobs (16 B header + payload + CRC32 trailer — the CRC the
 *     app verifies lives IN the blob, not computed on air).
 *   - no stream deadline / ghost-link watchdogs yet (5.83/5.87 lessons).
 *
 * ── Engineering rules carried over from the Arduino bench era ────────
 *
 *   - write handler only CAPTURES the request (single-slot buffer); all
 *     processing happens from work context, never in the BT callback
 *     (V5.16 lesson).
 *   - the write buffer we receive points at stack the stack reuses — we
 *     memcpy it out before returning (same disease as the Android
 *     GATT-callback buffer we cloned in App 1.39).
 *   - output: notify to the phone (only if subscribed) AND printk mirror
 *     (forensics parity with USB serial in ArduinoFW).
 *
 * Demo on the phone (nRF Connect):
 *   connect "SGC-Dev" → subscribe 0xABCA → write text "D" (3 lines back)
 *   → write "s 1 0 2048" (9 binary frames: 00 01 02 03.. ramp, then
 *   FF FF 00) → write "s 9 0 10" ("E 1").
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#define APP_VERSION "0.3.0"

/* ------------------------------------------------------------------ */
/* Demo service (unchanged from app-02) — 8d5a family                  */
/* ------------------------------------------------------------------ */

static struct bt_uuid_128 sgc_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8d5a0001, 0x84a4, 0x4c5e,
					   0x8e4f, 0x1c2b3d4f5a6c));
static struct bt_uuid_128 sgc_counter_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x8d5a0002, 0x84a4, 0x4c5e,
					   0x8e4f, 0x1c2b3d4f5a6c));

static const struct device *led  = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led0)));
static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static uint32_t counter;
static bool     notify_enabled;

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

/* ------------------------------------------------------------------ */
/* Product service — 0x5347 / console 0xABCA (ArduinoFW wire format)   */
/* ------------------------------------------------------------------ */

/* 16-bit UUIDs on the Bluetooth base — exactly what the SGC_UUID macro
 * in ArduinoFW builds: "5347" ABCA "-0000-1000-8000-00805F9B34FB". */
static struct bt_uuid_16 product_svc_uuid = BT_UUID_INIT_16(0x5347);
static struct bt_uuid_16 console_uuid     = BT_UUID_INIT_16(0xABCA);

#define REQ_MAX         80      /* same cap as ArduinoFW g_req[80]   */
#define PATTERN_SIZE    2048    /* fake run blob size (3a loopback)  */
#define STREAM_PAYLOAD  240     /* PULL_STREAM_PAYLOAD in config.h   */
#define STREAM_GAP_MS   25      /* PULL_STREAM_GAP_MS (5.81 pacing)  */

static bool console_subscribed;

/* Work items (declared early: write_console submits cmd_work before the
 * handlers below are defined) */
static struct k_work          cmd_work;      /* command interpreter   */
static struct k_work_delayable stream_work; /* frame pacer           */

/* ── Request capture: single slot, drop-if-busy (V5.16 + parity with
 * the "phone must wait for the reply" rule). Write callbacks run in BT
 * RX context: copy out, set pending, return. Never work here. ────── */
static char           req_buf[REQ_MAX];
static volatile bool  req_pending;

static ssize_t write_console(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	/* Same filter as ArduinoFW on_ft_request: drop anything that smells
	 * binary (legacy push-FT command bytes). */
	if (len == 0 || data[0] < 0x20 || data[0] > 0x7E) {
		return len;
	}
	if (req_pending) {
		printk("req dropped (busy)\n");   /* phone must wait */
		return len;
	}

	uint16_t n = MIN(len, REQ_MAX - 1);
	memcpy(req_buf, data, n);
	req_buf[n] = '\0';
	req_pending = true;
	k_work_submit(&cmd_work);

	return len;
}

static void console_ccc_changed(const struct bt_gatt_attr *attr,
				uint16_t value)
{
	console_subscribed = (value == BT_GATT_CCC_NOTIFY);
	printk("phone %s (console ABCA)\n",
	       console_subscribed ? "SUBSCRIBED" : "unsubscribed");
}

/* attrs[] indices: [0] service, [1] characteristic, [2] CCC */
BT_GATT_SERVICE_DEFINE(product_svc,
	BT_GATT_PRIMARY_SERVICE(&product_svc_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&console_uuid.uuid,
			       BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP |
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       NULL, write_console, NULL),
	BT_GATT_CCC(console_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Text reply to the phone (notify) + serial mirror. Now that the service
 * exists we can reference its attrs. */
static void console_emit(const char *s)
{
	printk("<phone %s\n", s);
	if (console_subscribed) {
		bt_gatt_notify(NULL, &product_svc.attrs[1],
			       s, strlen(s));
	}
}

/* Binary frame to the phone. Serial mirrors only the frame header
 * (ArduinoFW stopped per-frame hex mirrors — 5.79 baud lesson). */
static void console_emit_bin(const uint8_t *d, size_t n)
{
	if (console_subscribed) {
		bt_gatt_notify(NULL, &product_svc.attrs[1], d, n);
	}
}

/* ------------------------------------------------------------------ */
/* Stream job: self-rescheduling work, one frame per 25 ms             */
/* ------------------------------------------------------------------ */

static struct k_work tick_work;
static struct k_work button_work;
static struct k_timer  tick_timer;

static volatile bool connected;

/* Job state (3a: one job at a time — the single-slot req queue enforces
 * "phone waits for the reply" already). */
static uint32_t job_off;    /* next byte of the pattern to send */
static uint32_t job_end;    /* off + len                         */
static uint16_t job_seq;
static bool     job_active;

static uint8_t frame[3 + STREAM_PAYLOAD];

/* The fake run blob: byte at offset o = o & 0xFF. In 3b this becomes
 * RawRunStore read_run_data(id, off, buf, take) — same shape. */
static void pattern_read(uint32_t off, uint8_t *dst, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		dst[i] = (uint8_t)(off + i);
	}
}

static void stream_work_handler(struct k_work *w)
{
	/* Abort on dead link: stateless design — no session to corrupt
	 * (ArduinoFW pull_abort "disc"). */
	if (!connected || !console_subscribed) {
		printk("{\"ev\":\"s_abort\",\"seq\":%u}\n", job_seq);
		job_active = false;
		return;
	}

	uint32_t remain = job_end - job_off;

	if (remain == 0) {
		/* END marker: seq=0xFFFF, len=0 */
		uint8_t end[3] = { 0xFF, 0xFF, 0x00 };
		console_emit_bin(end, 3);
		printk("{\"ev\":\"s_done\",\"seq\":%u}\n", job_seq);
		job_active = false;
		return;
	}

	uint32_t take = MIN(remain, STREAM_PAYLOAD);

	frame[0] = (uint8_t)(job_seq & 0xFF);       /* seq: 2 B little-endian */
	frame[1] = (uint8_t)(job_seq >> 8);
	frame[2] = (uint8_t)take;                   /* len: 1 B               */
	pattern_read(job_off, &frame[3], take);

	console_emit_bin(frame, 3 + take);
	printk("frame seq=%u len=%lu off=%lu\n",
	       job_seq, (unsigned long)take, (unsigned long)job_off);

	job_off += take;
	job_seq++;

	/* Pace: reschedule for 25 ms — even after the LAST data frame (the
	 * next pass sees remain==0 and emits the END marker). Same shape as
	 * the adv-restart retry: schedule-and-return keeps the queue free
	 * (5.81: flooding the LL queue is what wedged the S22 push era). */
	k_work_schedule(&stream_work, K_MSEC(STREAM_GAP_MS));
}

/* Parse "s <id> <off> <len>" tail. Returns 0 on ok. */
static int parse_three_ul(const char *p, long *id, long *off, long *len)
{
	char *end;
	*id  = strtol(p, &end, 10);  if (end == p) return -1;  p = end;
	*off = strtol(p, &end, 10);  if (end == p) return -1;  p = end;
	*len = strtol(p, &end, 10);  if (end == p) return -1;
	return 0;
}

static void cmd_work_handler(struct k_work *w)
{
	/* Copy the request out FIRST, then free the slot: a write arriving
	 * while we parse may not mutate the buffer under our feet. */
	char req[REQ_MAX];

	strcpy(req, req_buf);
	req_pending = false;

	char cmd = req[0];

	if (cmd == 'D') {
		/* Fake directory — 2 runs, same sizes as the pattern blob.
		 * 3b: real RunEntry walk, sz = header+payload+CRC trailer. */
		console_emit("{\"run\":0,\"ts\":1758500000,\"sz\":2070,\"side\":0}");
		console_emit("{\"run\":1,\"ts\":1758510000,\"sz\":2070,\"side\":1}");
		console_emit("{\"d_end\":2}");
	} else if (cmd == 's') {
		long id, off, len;

		if (parse_three_ul(&req[1], &id, &off, &len) != 0 ||
		    id < 0 || off < 0 || len <= 0) {
			console_emit("E 3");            /* malformed */
		} else if (id > 1) {
			console_emit("E 1");            /* bad run */
		} else if ((uint32_t)off >= PATTERN_SIZE) {
			console_emit("E 2");            /* bad off/len */
		} else if (job_active) {
			/* one stream at a time; the phone protocol never does this */
			console_emit("E 4");
		} else {
			uint32_t remain = PATTERN_SIZE - (uint32_t)off;
			if ((uint32_t)len > remain) {
				len = remain;           /* clamp, as ArduinoFW */
			}
			job_off = (uint32_t)off;
			job_end = job_off + (uint32_t)len;
			job_seq = 0;
			job_active = true;
			printk("stream start id=%ld off=%ld len=%ld\n",
			       id, off, len);
			k_work_schedule(&stream_work, K_NO_WAIT);
		}
	} else {
		console_emit("E 4");                    /* unknown command */
	}
}

/* ------------------------------------------------------------------ */
/* Demo tick + button (app-02 behavior, honest print)                  */
/* ------------------------------------------------------------------ */

static void notify_counter(void)
{
	if (notify_enabled) {
		bt_gatt_notify(NULL, &demo_svc.attrs[1],
			       &counter, sizeof(counter));
	}
}

static void tick_work_handler(struct k_work *w)
{
	counter++;
	notify_counter();

	/* LED = link state: connected -> solid green, else blue blink.
	 * Nicla quirk: IS31 channel order is <B,G,R> — index 0 = blue. */
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
	k_work_submit(&button_work);
}

/* ------------------------------------------------------------------ */
/* Bluetooth bring-up + connection lifecycle (unchanged from v0.2.2)   */
/* ------------------------------------------------------------------ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err && err != -EALREADY) {
		printk("ERROR: adv start -> %d\n", err);
	}
}

static struct k_work_delayable adv_work;

static void adv_work_handler(struct k_work *w)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	static uint8_t attempt;

	if (err == 0 || err == -EALREADY) {
		attempt = 0;
		printk("advertising up\n");
		return;
	}
	if (++attempt < 10) {
		printk("adv start -> %d, retry %u/10\n", err, attempt);
		k_work_schedule(&adv_work, K_MSEC(100));
	} else {
		printk("ERROR: adv start -> %d, giving up after retries\n", err);
		attempt = 0;
	}
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("connect FAILED from %s (err %u), re-advertising\n",
		       addr, err);
		k_work_schedule(&adv_work, K_NO_WAIT);
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

	/* Pull protocol: a dead link aborts any stream (stateless design). */
	k_work_cancel_delayable(&stream_work);
	job_active = false;
	req_pending = false;

	printk("disconnected: %s (reason 0x%02x), re-advertising\n",
	       addr, reason);

	k_work_schedule(&adv_work, K_NO_WAIT);
}

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
	printk("SGC app-03 pull_proto v%s\n", APP_VERSION);

	if (!device_is_ready(led)) {
		printk("ERROR: LED device not ready\n");
		return 0;
	}

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

	k_work_init(&tick_work, tick_work_handler);
	k_work_init(&button_work, button_work_handler);
	k_work_init(&cmd_work, cmd_work_handler);
	k_work_init_delayable(&stream_work, stream_work_handler);
	k_work_init_delayable(&adv_work, adv_work_handler);
	k_timer_init(&tick_timer, tick_timer_expiry, NULL);
	k_timer_start(&tick_timer, K_SECONDS(1), K_SECONDS(1));

	int err = bt_enable(bt_ready);
	if (err) {
		printk("ERROR: bt_enable -> %d\n", err);
	}

	printk("init complete, entering idle\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
