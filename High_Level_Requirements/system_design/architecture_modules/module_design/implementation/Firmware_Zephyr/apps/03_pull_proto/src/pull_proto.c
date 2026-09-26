/*
 * pull_proto.c — product console service + pull protocol (step 3c)
 * ==================================================================
 * The wire protocol App 1.46 speaks, verbatim from ArduinoFW 5.89
 * (pull_transfer.cpp):
 *
 *   service  53470000-0000-1000-8000-00805F9B34FB  ("SG" family)
 *   0xABC8   run info  : count[2 LE] + age[4] (read)
 *   0xABC9   run list  : [{"id":N,"ts":N,"size":PAYLOAD,"side":"l|r"}]
 *   0xABCA   console   : phone WRITES ASCII commands, device NOTIFYs
 *                        text lines / binary frames back
 *
 *   "D"                  → one {"run":id,"ts":..,"sz":TOTAL,..} line
 *                          per run + {"d_end":n}
 *   "s <id> <off> <len>" → [seq:2B LE][len:1B][payload ≤240 B] frames
 *                          paced 25 ms, END marker FF FF 00
 *   "E n"                → 1 bad run · 2 bad off/len · 3 malformed
 *                          · 4 unknown/busy
 *
 * UUID WARNING (bench 09-22): these are FULL 128-bit UUIDs, NOT 16-bit
 * forms. ArduinoFW's SGC_UUID() emits 5347ABCX-… on air; a BT_UUID_16
 * declaration means 0000ABCX-… — same digits, different namespace, and
 * the app resolves by string (nRF hides this by working per-handle).
 *
 * Engineering rules (Arduino bench era, all paid for):
 *   - write callback only CAPTURES (single slot, copy-out, drop-if-
 *     busy — V5.16 lesson); processing happens in cmd_work.
 *   - notify return codes are CHECKED: text failure = logged, data
 *     frame failure = clean stream ABORT (never silent corruption).
 *   - stream frames self-reschedule at STREAM_GAP_MS — the LL queue
 *     stays shallow (5.81: flooding is what wedged the S22 push era).
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/uart.h>   /* serial command input (JP 09-25) */
#include "pull_proto.h"
#include "store.h"
#include "store_layout.h"
#include "ble_lifecycle.h"

#define REQ_MAX         80      /* same cap as ArduinoFW g_req[80]   */
#define STREAM_PAYLOAD  240     /* PULL_STREAM_PAYLOAD in config.h   */
#define STREAM_GAP_MS   25      /* PULL_STREAM_GAP_MS (5.81 pacing)  */

/* Full 128-bit UUIDs, byte-identical to ArduinoFW's SGC_UUID() strings
 * (see header warning — no 16-bit forms, ever). */
struct bt_uuid_128 product_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x53470000, 0x0000, 0x1000,
					    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 run_info_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC8, 0x0000, 0x1000,
					    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 run_list_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC9, 0x0000, 0x1000,
					    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 console_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABCA, 0x0000, 0x1000,
					    0x8000, 0x00805F9B34FB));

static bool console_subscribed;

/* ── Listing surface (ABC8/ABC9, built from the store) ───────────── */

static uint8_t run_info_buf[6];    /* count LE + age LE (age=0: no RTC
				      until the epoch-sync port, ABC0) */
static char run_list_json[RUN_LIST_JSON_SIZE];   /* sized from MAX_INDEXED_RUNS by construction (founding test) */

static ssize_t read_buf(const void *data, uint16_t data_len,
			void *buf, uint16_t len, uint16_t offset)
{
	if (offset > data_len) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	uint16_t frag = MIN(len, data_len - offset);

	memcpy(buf, (const uint8_t *)data + offset, frag);
	return frag;
}

static ssize_t read_run_info(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return read_buf(run_info_buf, sizeof(run_info_buf), buf, len, offset);
}

static ssize_t read_run_list(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return read_buf(run_list_json, strlen(run_list_json), buf, len, offset);
}

/* ── Request capture (write callback = BT RX context) ────────────── */

static char           req_buf[REQ_MAX];
static volatile bool  req_pending;
/* Which transport captured the pending request? Replies route back the
 * same way (ArduinoFW g_req_via_ble parity): BLE replies go out as
 * notifications, serial replies echo as text/hex lines. */
static bool           req_via_ble;

static struct k_work          cmd_work;
static struct k_work_delayable stream_work;

static ssize_t write_console(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	/* ArduinoFW on_ft_request parity: drop anything that smells
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
	req_via_ble = true;
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

/* attrs[] indices: [0] svc · [1,2] run info · [3,4] run list ·
 * [5,6] console (+declaration) · [7] CCC. console value = attrs[6]
 * for GATT ops — but notify via attrs[5] is the proven form (see
 * console_emit note). */
BT_GATT_SERVICE_DEFINE(product_svc,
	BT_GATT_PRIMARY_SERVICE(&product_svc_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&run_info_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_run_info, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&run_list_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_run_list, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&console_uuid.uuid,
			       BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP |
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       NULL, write_console, NULL),
	BT_GATT_CCC(console_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static uint32_t job_off;    /* next blob byte to send            */
static uint32_t job_end;    /* off + len                         */
static uint16_t job_seq;
static uint8_t  job_run;
static bool     job_active;
static uint32_t job_t0;     /* k_uptime_get_32() at stream start */
static bool     job_via_ble;  /* stream origin: serial streams must not
			       * depend on the radio link at all */

static uint8_t frame[3 + STREAM_PAYLOAD];

/* ── Output paths (notify + serial mirror; rc ALWAYS checked) ───── */

static void console_emit(const char *s)
{
	if (req_via_ble) {
		printk("<phone %s\n", s);
	} else {
		printk("%s\n", s);       /* serial origin: plain echo */
	}
	if (req_via_ble && console_subscribed) {
		int err = bt_gatt_notify(NULL, &product_svc.attrs[5],
					 s, strlen(s));

		if (err) {
			printk("ERROR: notify \"%s\" -> %d (MTU?)\n", s, err);
		}
	}
}

static void console_emit_bin(const uint8_t *d, size_t n)
{
	if (!req_via_ble) {
		/* Serial origin: human-readable hex, ArduinoFW style:
		 * [seq n HEX…] — lets a terminal (or zcmd) eyeball/verify
		 * the stream without any radio. END marker special-cased. */
		if (n == 3 && d[0] == 0xFF && d[1] == 0xFF) {
			printk("[FFFF end]\n");
			return;
		}
		printk("[%u %u ", d[0] | (d[1] << 8), d[2]);
		for (size_t i = 3; i < n; i++) {
			printk("%02X", d[i]);
		}
		printk("]\n");
		return;
	}
	if (console_subscribed) {
		int err = bt_gatt_notify(NULL, &product_svc.attrs[5], d, n);

		if (err) {
			/* Failed DATA frame = potential silent corruption —
			 * abort the stream (stateless: phone re-requests). */
			printk("ERROR: frame notify -> %d, ABORT stream\n", err);
			job_active = false;
		}
	}
}

/* ── Stream job (self-rescheduling pacer) ────────────────────────── */

static void stream_work_handler(struct k_work *w)
{
	/* Abort rule is transport-aware: BLE streams die with the link or
	 * the subscription; serial streams have no such dependency. */
	if (job_via_ble &&
	    (!ble_lifecycle_connected() || !console_subscribed)) {
		printk("{\"ev\":\"s_abort\",\"seq\":%u}\n", job_seq);
		job_active = false;
		return;
	}

	uint32_t remain = job_end - job_off;

	if (remain == 0) {
		uint8_t end[3] = { 0xFF, 0xFF, 0x00 };   /* seq=0xFFFF, len=0 */

		console_emit_bin(end, 3);
		printk("{\"ev\":\"s_done\",\"seq\":%u,\"ms\":%u}\n",
		       job_seq, k_uptime_get_32() - job_t0);
		job_active = false;
		return;
	}

	uint32_t take = MIN(remain, STREAM_PAYLOAD);

	frame[0] = (uint8_t)(job_seq & 0xFF);       /* seq: 2 B LE */
	frame[1] = (uint8_t)(job_seq >> 8);
	frame[2] = (uint8_t)take;                   /* len: 1 B     */
	store_read_blob(job_run, job_off, &frame[3], take);

	console_emit_bin(frame, 3 + take);
	printk("frame seq=%u len=%lu off=%lu\n",
	       job_seq, (unsigned long)take, (unsigned long)job_off);

	job_off += take;
	job_seq++;

	/* Reschedule for 25 ms — even after the LAST data frame (the
	 * next pass sees remain==0 and emits the END marker). */
	k_work_schedule(&stream_work, K_MSEC(STREAM_GAP_MS));
}

/* Parse " <id> <off> <len>" tail. 0 on success. */
static int parse_three_l(const char *p, long *id, long *off, long *len)
{
	char *end;

	*id  = strtol(p, &end, 10);  if (end == p) return -1;  p = end;
	*off = strtol(p, &end, 10);  if (end == p) return -1;  p = end;
	*len = strtol(p, &end, 10);  if (end == p) return -1;
	return 0;
}

static void cmd_work_handler(struct k_work *w)
{
	/* Copy out FIRST, then free the slot: a write arriving while we
	 * parse may not mutate the buffer under our feet. */
	char req[REQ_MAX];

	strcpy(req, req_buf);
	req_pending = false;

	char cmd = req[0];

	if (cmd == 'D') {
		uint8_t n = store_run_count();

		for (uint8_t i = 0; i < n; i++) {
			char line[96];

			snprintf(line, sizeof(line),
				 "{\"run\":%u,\"ts\":%lu,\"sz\":%lu,\"side\":%u}",
				 i, (unsigned long)store_run_ts(i),
				 (unsigned long)store_run_total(i),
				 store_run_side(i));
			console_emit(line);
		}
		char tail_line[24];

		snprintf(tail_line, sizeof(tail_line), "{\"d_end\":%u}", n);
		console_emit(tail_line);
	} else if (cmd == 's') {
		long id, off, len;

		if (parse_three_l(&req[1], &id, &off, &len) != 0 ||
		    id < 0 || off < 0 || len <= 0) {
			console_emit("E 3");            /* malformed */
		} else if ((uint32_t)id >= store_run_count()) {
			console_emit("E 1");            /* bad run */
		} else if ((uint32_t)off >= store_run_total((uint8_t)id)) {
			console_emit("E 2");            /* bad off/len */
		} else if (job_active) {
			/* one stream at a time; the phone never does this */
			console_emit("E 4");
		} else {
			uint32_t total = store_run_total((uint8_t)id);
			uint32_t remain = total - (uint32_t)off;

			if ((uint32_t)len > remain) {
				len = remain;           /* clamp, as ArduinoFW */
			}
			job_run = (uint8_t)id;
			job_off = (uint32_t)off;
			job_end = job_off + (uint32_t)len;
			job_seq = 0;
			job_t0 = k_uptime_get_32();
			job_via_ble = req_via_ble;
			job_active = true;
			printk("stream start id=%ld off=%ld len=%ld\n",
			       id, off, len);
			k_work_schedule(&stream_work, K_NO_WAIT);
		}
	} else if (cmd == 'x') {
		/* Bench reset: wipe the index sector and reboot the log to
		 * empty (data sectors reclaim lazily on the next writes).
		 * The escape hatch for "the store state is garbage". */
		store_wipe();
		console_emit("OK wiped");
	} else if (cmd == 'w') {
		/* 4b bench producer: record a synthetic run through the REAL
		 * lifecycle (erase → write → close → index). nRF Connect:
		 *   write "w 40960"   → one typical training run
		 *   write "w 249834"  → one max-budget run (forces rotation)
		 * Progress and the done-line arrive on serial; D updates. */
		long len = strtol(&req[1], NULL, 10);

		if (len <= 0) {
			console_emit("E 3");            /* malformed */
		} else if (!store_record_start((uint32_t)len)) {
			console_emit("E 5");            /* busy or out of policy */
		} else {
			printk("w: accepted %ld B\n", len);
		}
	} else {
		console_emit("E 4");                    /* unknown command */
	}
}

/* ── Serial transport (JP directive 09-25: "different data sources,
 *    1 data treatment path" — the ArduinoFW FWR-1/SR-5 doctrine) ──────
 * THIRD APPROACH after the console_getline/getchar path proved broken on
 * this nRF-UARTE (wild function-pointer jumps, three boot faults): we
 * own the UART RX interrupt directly — the pattern Nordic's own
 * peripheral_uart sample uses. printk TX is unaffected (poll mode).
 *
 * The ISR assembles lines into the SAME single-slot capture the BLE
 * write callback uses — one parser, two transports, zero drift. */
static const struct device *const console_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static uint8_t  rx_chunk[32];        /* DMA window the driver fills   */
static uint16_t rx_line_len;         /* line under assembly           */

static void serial_rx_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (!uart_irq_update(console_uart) ||
	    !uart_irq_rx_ready(console_uart)) {
		return;
	}
	for (;;) {
		int n = uart_fifo_read(console_uart, rx_chunk,
				       sizeof(rx_chunk));

		if (n <= 0) {
			break;
		}
		for (int i = 0; i < n; i++) {
			uint8_t c = rx_chunk[i];

			if (c == '\n' || c == '\r') {
				if (rx_line_len == 0) {
					continue;
				}
				/* Line complete → the shared capture. */
				if (!req_pending &&
				    (uint8_t)req_buf[0] >= 0x20 &&
				    (uint8_t)req_buf[0] <= 0x7E) {
					req_buf[rx_line_len] = '\0';
					req_via_ble = false;
					req_pending = true;
					k_work_submit(&cmd_work);
				} else if (req_pending) {
					printk("req dropped (busy)\n");
				}
				rx_line_len = 0;
				continue;
			}
			if (rx_line_len < REQ_MAX - 1) {
				req_buf[rx_line_len++] = (char)c;
			}
		}
	}
}

/* ── Advertisement payloads (product service UUID lives here) ───── */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_UUID128_ALL, &product_svc_uuid.val,
		sizeof(product_svc_uuid.val)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

const struct bt_data *pull_proto_ad(void)   { return ad; }
const struct bt_data *pull_proto_sd(void)   { return sd; }
uint8_t pull_proto_ad_len(void)             { return ARRAY_SIZE(ad); }
uint8_t pull_proto_sd_len(void)             { return ARRAY_SIZE(sd); }

/* ── Lifecycle hooks ─────────────────────────────────────────────── */

void pull_proto_on_disconnect(void)
{
	/* Dead link aborts any stream; request slot is void (the phone
	 * will re-send after reconnecting — stateless, SR-2). */
	k_work_cancel_delayable(&stream_work);
	job_active = false;
	req_pending = false;
}

void pull_proto_start(void)
{
	k_work_init(&cmd_work, cmd_work_handler);
	k_work_init_delayable(&stream_work, stream_work_handler);

	/* Serial transport bring-up: our RX ISR replaces the console's
	 * (single callback slot — printk TX is poll-mode, unaffected). */
	if (device_is_ready(console_uart)) {
		uart_irq_callback_user_data_set(console_uart,
						serial_rx_isr, NULL);
		uart_irq_rx_enable(console_uart);
		printk("serial: rx isr armed\n");
	} else {
		printk("ERROR: console uart not ready\n");
	}

	/* Listing surface from the store: ABC8 count+age, ABC9 JSON
	 * (build_run_list format — "size" = payload ONLY, without the
	 * 16 B header and 6 B trailer; D's sz is the full total). */
	run_info_buf[0] = (uint8_t)(store_run_count() & 0xFF);
	run_info_buf[1] = (uint8_t)(store_run_count() >> 8);
	memset(&run_info_buf[2], 0, 4);

	{
		size_t pos = 0;
		uint8_t n = store_run_count();

		pos += snprintf(&run_list_json[pos],
				sizeof(run_list_json) - pos, "[");
		for (uint8_t i = 0; i < n; i++) {
			pos += snprintf(&run_list_json[pos],
					sizeof(run_list_json) - pos,
					"%s{\"id\":%u,\"ts\":%lu,\"size\":%lu,"
					"\"side\":\"%s\"}",
					(i ? "," : ""), i,
					(unsigned long)store_run_ts(i),
					(unsigned long)store_run_payload_len(i),
					store_run_side(i) ? "right" : "left");
		}
		snprintf(&run_list_json[pos], sizeof(run_list_json) - pos, "]");
		printk("run list: %s\n", run_list_json);
	}
}
