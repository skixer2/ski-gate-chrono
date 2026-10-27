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
 *   v0.22.1 (JP 2026-10-27): every wire id — ABC9 "id", D "run", the
 *   s/H/d arguments — is the run's STABLE run_id (assigned at record
 *   time, monotonic u16), never the entries[] position. Deletes leave
 *   holes; ids never shift. Requests resolve run_id → position via
 *   store_run_pos(); unknown ids take the existing not-found paths.
 *   "s <id> <off> <len>" → [seq:2B LE][len:1B][payload ≤240 B] frames
 *                          paced 25 ms, END marker FF FF 00
 *   "d <id>"             → delete run <id> (F46, v0.22.0): INDEX-only,
 *                          lazy data reclaim. "OK deleted N" |
 *                          "OK already gone" (idempotent retry) |
 *                          "E 5" busy (recording) | "E 4" transfer
 *                          | "E 3" malformed (digits only)
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
#include "sensor_synth.h"           /* bench 'S' synthetic runs */
#include "pull_proto.h"
#include "store.h"
#include "ble_config.h"
#include "recorder.h"
#include "store_layout.h"
#include "ble_lifecycle.h"

#define REQ_MAX         80      /* same cap as ArduinoFW g_req[80]   */
#define STREAM_PAYLOAD  240     /* PULL_STREAM_PAYLOAD in config.h   */
/* 20 ms (JP 2026-09-29: 25 ms proven clean, push one notch = 12 KB/s).
 * The app's idle watchdog (1.47+) makes flowing streams timeout-proof;
 * a genuine stall trips 2 s idle and retries. 25 ms = the known-good
 * fallback one constant away. */
#define STREAM_GAP_MS   20

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
static struct bt_uuid_128 time_sync_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC0, 0x0000, 0x1000,
					    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 console_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABCA, 0x0000, 0x1000,
					    0x8000, 0x00805F9B34FB));

/* v0.18.0 config/status chars (error-tolerance audit B3) - ArduinoFW
 * 5.x contract, base 5347ABxx-0000-1000-8000-00805F9B34FB. */
static struct bt_uuid_128 dev_name_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC1, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 arm_side_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC2, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 discipline_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC3, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 state_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC4, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 battery_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC5, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 flash_used_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABC7, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));
static struct bt_uuid_128 cal_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x5347ABD0, 0x0000, 0x1000,
				    0x8000, 0x00805F9B34FB));

static bool console_subscribed;

static uint8_t run_info_buf[6];    /* count LE + age LE (age=0: no RTC
				      until the epoch-sync port, ABC0) */
static char run_list_json[RUN_LIST_JSON_SIZE];   /* sized from MAX_INDEXED_RUNS by construction (founding test) */

/* ── Listing surface (ABC8/ABC9, built from the store) ─────────────
 * BUGFIX v0.9.1 (2026-09-28, found live with the phone): these
 * surfaces were built ONCE at boot — a run recorded after boot was
 * invisible to the app until the next reboot (it "worked" on the
 * bench only because flash/reset cycles preceded every listing).
 * Now every GATT read rebuilds from the live store (skipped while
 * the producer is mid-cycle: serve the last consistent snapshot). */
static void rebuild_listing(bool log_it)
{
	size_t pos = 0;
	uint8_t n = store_run_count();

	run_info_buf[0] = (uint8_t)(n & 0xFF);
	run_info_buf[1] = (uint8_t)(n >> 8);
	memset(&run_info_buf[2], 0, 4);

	/* APP CONTRACT (v1.1): the run-list characteristic delivers at most
	 * 512 bytes to the phone (FBP long-read cap). Fill NEWEST-FIRST with
	 * as many whole entries as fit; older runs fall off the tail. */
	{
		uint32_t budget = 510;   /* [ and ] */

		pos += snprintf(&run_list_json[pos],
				sizeof(run_list_json) - pos, "[");
		/* wire id = stable per-run id (holes after delete are
		 * expected — JP 2026-10-27); loop index i stays positional
		 * for the store accessors below. */
		for (int16_t i = (int16_t)n - 1; i >= 0; i--) {
			char entry[80];
			int el = snprintf(entry, sizeof(entry),
					  "%s{\"id\":%u,\"ts\":%lu,\"size\":%lu,"
					  "\"side\":\"%s\"}",
					  (pos > 1 ? "," : ""),
					  (unsigned)store_run_id((uint8_t)i),
					  (unsigned long)store_run_ts(i),
					  (unsigned long)store_run_payload_len(i),
					  store_run_side(i) ? "right" : "left");

			if (pos + el + 1 > budget) {
				break;   /* would not fit whole */
			}
			memcpy(&run_list_json[pos], entry, el);
			pos += el;
		}
	}
	snprintf(&run_list_json[pos], sizeof(run_list_json) - pos, "]");

	if (log_it) {
		printk("run list: %s\n", run_list_json);
	}
}

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
	if (!store_busy()) {
		rebuild_listing(false);
	}
	return read_buf(run_info_buf, sizeof(run_info_buf), buf, len, offset);
}

static ssize_t read_run_list(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	if (!store_busy()) {
		rebuild_listing(false);
	}
	ssize_t r = read_buf(run_list_json, strlen(run_list_json),
			     buf, len, offset);

	printk("{\"ev\":\"abc9_read\",\"len\":%u,\"off\":%u,\"ret\":%d,"
	       "\"json_len\":%u}\n",
	       len, offset, (int)r, (unsigned)strlen(run_list_json));
	return r;
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

/* ABC0 time sync — ArduinoFW parity: silent write, uint32 LE epoch
 * seconds, sanity >1e9 (reject pre-2001 garbage), no response. */
static ssize_t write_time_sync(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len,
			       uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;

	if (offset || len < 4) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	uint32_t epoch = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
			 ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

	if (epoch > 1000000000u) {
		store_epoch_sync(epoch);
		printk("{\"ev\":\"epoch_sync\",\"utc\":%u}\n", epoch);
	}
	return len;
}

static void console_ccc_changed(const struct bt_gatt_attr *attr,
				uint16_t value)
{
	console_subscribed = (value == BT_GATT_CCC_NOTIFY);
	printk("phone %s (console ABCA)\n",
	       console_subscribed ? "SUBSCRIBED" : "unsubscribed");
}

/* ---- v0.18.0 config/status read+write callbacks (audit B3) -------
 * Writes only touch RAM + schedule an async flash flush (ble_config);
 * NEVER erase in BT RX context. Reads compose fresh values. */
static ssize_t read_dev_name(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	const char *n = ble_config_dev_name();

	return read_buf(n, (uint16_t)strlen(n), buf, len, offset);
}

static ssize_t write_dev_name(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len,
			      uint16_t offset, uint8_t flags)
{
	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	ble_config_set_dev_name((const char *)buf, (uint8_t)len);
	/* Keep the GAP Device Name characteristic in step with the
	 * advertisement (generic scanners read either). */
	if (bt_set_name(ble_config_dev_name()) != 0) {
		printk("cfg: GAP name update failed\n");
	}
	printk("cfg: name <- '%s' (%u B)\n", ble_config_dev_name(), len);
	return len;
}

static ssize_t read_arm_side(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	uint8_t v = ble_config_arm_side();

	return read_buf(&v, 1, buf, len, offset);
}

static ssize_t write_arm_side(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len,
			      uint16_t offset, uint8_t flags)
{
	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	ble_config_set_arm_side(((const uint8_t *)buf)[0]);
	printk("cfg: arm <- %u\n", ble_config_arm_side());
	return len;
}

static ssize_t read_discipline(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       void *buf, uint16_t len, uint16_t offset)
{
	uint8_t v = ble_config_discipline();

	return read_buf(&v, 1, buf, len, offset);
}

static ssize_t write_discipline(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len,
				uint16_t offset, uint8_t flags)
{
	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	ble_config_set_discipline(((const uint8_t *)buf)[0]);
	printk("cfg: discipline <- %u\n", ble_config_discipline());
	return len;
}

static ssize_t read_state(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	uint8_t v = ble_config_state_byte();

	return read_buf(&v, 1, buf, len, offset);
}

static ssize_t read_battery(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf, uint16_t len, uint16_t offset)
{
	uint8_t v = ble_config_battery_byte();

	return read_buf(&v, 1, buf, len, offset);
}

static ssize_t read_flash_used(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       void *buf, uint16_t len, uint16_t offset)
{
	uint8_t v = ble_config_flash_used_pct();

	return read_buf(&v, 1, buf, len, offset);
}

static ssize_t read_cal(struct bt_conn *conn,
		       const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
	uint8_t v = ble_config_cal();

	return read_buf(&v, 1, buf, len, offset);
}

/* ABC0 readback (audit B2): last-synced epoch LE, 0 = never synced.
 * Lets the app VERIFY the write landed (epoch sanity failures are
 * otherwise silent on both sides). */
static ssize_t read_time_sync(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      void *buf, uint16_t len, uint16_t offset)
{
	uint32_t epoch = store_epoch_now();
	uint8_t v[4] = {
		(uint8_t)epoch, (uint8_t)(epoch >> 8),
		(uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24)
	};

	return read_buf(v, 4, buf, len, offset);
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
	/* ABC0 appended AFTER existing attrs — stream code references
	 * attrs[5] for console notify; indices must not shift. */
	BT_GATT_CHARACTERISTIC(&time_sync_uuid.uuid,
			       BT_GATT_CHRC_READ |
			       BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_time_sync, write_time_sync, NULL),
	/* v0.18.0 config/status chars — appended AFTER ABC0 so existing
	 * attr indices (console notify = attrs[5]) never shift. */
	BT_GATT_CHARACTERISTIC(&dev_name_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_dev_name, write_dev_name, NULL),
	BT_GATT_CHARACTERISTIC(&arm_side_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_arm_side, write_arm_side, NULL),
	BT_GATT_CHARACTERISTIC(&discipline_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_discipline, write_discipline, NULL),
	BT_GATT_CHARACTERISTIC(&state_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_state, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&battery_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_battery, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&flash_used_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_flash_used, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&cal_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_cal, NULL, NULL),
);

static uint32_t job_off;    /* next blob byte to send            */
static uint32_t job_end;    /* off + len                         */
static uint16_t job_seq;
static uint8_t  job_run;    /* entries[] POSITION — v0.22.1: resolved
			     * from the wire run_id at stream start */
static bool     job_active;
static uint32_t job_t0;     /* k_uptime_get_32() at stream start */
static bool     job_via_ble;  /* stream origin: serial streams must not
			       * depend on the radio link at all */

static uint8_t frame[3 + STREAM_PAYLOAD];

/* ── Output paths (notify + serial mirror; rc ALWAYS checked) ───── */

static int console_emit(const char *s)
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
			/* v0.17.8: -ENOMEM = ATT TX pool drained (burst outran
			 * the link; gatt.c bt_att_create_pdu fail-fast). Caller
			 * decides retry — dir emitter paces on this. */
			printk("ERROR: notify \"%s\" -> %d (pool?)\n", s, err);
		}
		return err;
	}
	return 0;
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

/* Parse " <id>" — STRICT digits-only (F46): optional leading spaces,
 * one or more decimal digits, optional trailing spaces, END. Any
 * sign, hex letter or other junk is malformed — a phone retry must
 * never half-delete. Saturates far past MAX_INDEXED_RUNS so huge
 * values simply answer "already gone" (v0.22.1: wire ids are the
 * stable u16 run_ids — anything past 0xFFFF matches nothing).
 * 0 on success. */
static int parse_run_id(const char *p, uint32_t *id)
{
	uint32_t v = 0;

	while (*p == ' ') {
		p++;
	}
	if (*p < '0' || *p > '9') {
		return -1;
	}
	while (*p >= '0' && *p <= '9') {
		v = v * 10u + (uint32_t)(*p - '0');
		if (v > 0xFFFFFFu) {
			v = 0xFFFFFFu;	/* saturate: past any real id */
		}
		p++;
	}
	while (*p == ' ') {
		p++;
	}
	if (*p != '\0') {
		return -1;
	}
	*id = v;
	return 0;
}

void sensor_prod_golden_arm(void);

/* ── Directory job (paced, TX-pool-safe) ───────────────────────────
 *
 * v0.17.8 root cause (bench 2026-10-09, logcat-proven): the old code
 * emitted ALL listing lines + d_end back-to-back from cmd_work.
 * bt_gatt_notify() fail-fasts -ENOMEM once the ATT TX pool
 * (CONFIG_BT_ATT_TX_COUNT=10) is full — the pool drains over the air
 * at ~1 PDU/connection-interval, so a 21-notify burst delivered only
 * ~9 lines and LOST d_end  phone: "D: directory timeout" ×8 attempts.
 * Fix: one line per DIR_LINE_GAP_MS, retry the SAME line on error.
 * 21 lines ≈ 320 ms; worst case 255 lines ≈ 3.9 s < app's 10 s.
 */
static struct k_work_delayable dir_work;
static uint8_t  dir_idx;          /* next run line to emit           */
static uint8_t  dir_n;            /* store_run_count() at start      */
static bool     dir_active;
static bool     dir_via_ble;      /* transport captured at start     */
static bool     dir_extras_done;  /* serial rec/free emitted once    */

#define DIR_LINE_GAP_MS 15

static void dir_work_handler(struct k_work *w)
{
	char line[96];
	int err;

	if (!dir_active) {
		return;
	}

	/* BLE bursts die with the link/subscription (pool never drains). */
	if (dir_via_ble &&
	    (!ble_lifecycle_connected() || !console_subscribed)) {
		printk("{\"ev\":\"d_abort\",\"at\":%u}\n", dir_idx);
		dir_active = false;
		return;
	}

	/* A serial request may have flipped the routing var mid-burst
	 * (it got E 4 busy); keep the burst on its origin transport. */
	req_via_ble = dir_via_ble;

	if (dir_idx < dir_n) {
		/* run field = stable per-run id (see rebuild_listing);
		 * dir_idx stays an entries[] position for the accessors. */
		snprintf(line, sizeof(line),
			 "{\"run\":%u,\"ts\":%lu,\"sz\":%lu,\"side\":%u}",
			 (unsigned)store_run_id(dir_idx),
			 (unsigned long)store_run_ts(dir_idx),
			 (unsigned long)store_run_total(dir_idx),
			 store_run_side(dir_idx));
	} else {
		/* Serial-only extras BEFORE the terminator (v0.17.7 order),
		 * exactly once even if d_end itself retries. */
		if (!dir_via_ble && !dir_extras_done) {
			char rec_line[28];
			char free_line[24];

			dir_extras_done = true;
			snprintf(rec_line, sizeof(rec_line),
				 "{\"rec\":\"%s\"}", recorder_state_str());
			console_emit(rec_line);
			snprintf(free_line, sizeof(free_line),
				 "{\"free\":%lu}",
			 (unsigned long)store_free_full_runs());
			console_emit(free_line);
		}
		snprintf(line, sizeof(line), "{\"d_end\":%u}", dir_n);
	}

	err = console_emit(line);
	if (err) {
		/* Pool empty: retry SAME line after a gap. */
		k_work_reschedule(&dir_work, K_MSEC(DIR_LINE_GAP_MS));
		return;
	}

	dir_idx++;
	if (dir_idx > dir_n) {
		dir_active = false;    /* d_end delivered */
		return;
	}
	k_work_reschedule(&dir_work, K_MSEC(DIR_LINE_GAP_MS));
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
		if (dir_active || job_active) {
			console_emit("E 4");   /* busy — mirrors 's' policy */
			return;
		}
		dir_n = store_run_count();
		dir_idx = 0;
		dir_via_ble = req_via_ble;
		dir_extras_done = false;
		dir_active = true;
		k_work_schedule(&dir_work, K_NO_WAIT);
	} else if (cmd == 's') {
		long id, off, len;
		int16_t pos = -1;

		if (parse_three_l(&req[1], &id, &off, &len) != 0 ||
		    id < 0 || off < 0 || len <= 0) {
			console_emit("E 3");            /* malformed */
		} else if (id > 0xFFFF ||
			   (pos = store_run_pos((uint16_t)id)) < 0) {
			console_emit("E 1");            /* unknown run_id */
		} else if ((uint32_t)off >= store_run_total((uint8_t)pos)) {
			console_emit("E 2");            /* bad off/len */
		} else if (job_active || dir_active) {
			/* one stream at a time; the phone never does this */
			console_emit("E 4");
		} else {
			uint32_t total = store_run_total((uint8_t)pos);
			uint32_t remain = total - (uint32_t)off;

			if ((uint32_t)len > remain) {
				len = remain;           /* clamp, as ArduinoFW */
			}
			job_run = (uint8_t)pos;  /* internal positional handle */
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
	} else if (cmd == 'B') {
		/* v0.11.4: re-arm the BHI bit-bang probe + reboot into it
		 * (probe-boot output is otherwise lost pre-console). */
		extern void bhy2_probe_rearm(void);

		printk("BB probe rearm\n");
		bhy2_probe_rearm();
	} else if (cmd == 'H') {
		/* B3 bench: dump one run's payload as hexdump lines (IDLE only).
		 * Payload only — header/CRC excluded. 24 B/line.
		 * v0.22.1: <id> is the stable run_id — resolve to the entry;
		 * unknown ids take the bad_id path (never a wrong run). */
		long hid = strtol(req + 2, NULL, 10);
		int16_t pos = (hid < 0 || hid > 0xFFFF)
				    ? (int16_t)-1 : store_run_pos((uint16_t)hid);

		if (pos >= 0) {
			uint32_t pay = store_run_payload_len((uint8_t)pos);
			uint32_t off = 16;   /* blob: 16 B header first */

			printk("{\"ev\":\"rh_begin\",\"id\":%u,\"payload\":%u}\n",
			       (unsigned)hid, pay);
			for (uint32_t o = 0; o < pay; o += 24) {
				uint8_t buf[24];
				uint32_t n = pay - o;

				if (n > 24) {
					n = 24;
				}
				store_read_blob((uint8_t)pos, off + o, buf, n);
				printk("{\"ev\":\"rh\",\"off\":%u,\"hex\":\"", o);
				for (uint32_t i = 0; i < n; i++) {
					printk("%02x", buf[i]);
				}
				printk("\"}\n");
			}
			printk("{\"ev\":\"rh_end\",\"id\":%u}\n", (unsigned)hid);
		} else {
			printk("{\"ev\":\"rh_err\",\"why\":\"bad_id\"}\n");
		}
	} else if (cmd == 'S') {
		/* BENCH (CONFIG_SGC_BENCH): synthetic FRAME run through the
		 * REAL pipeline — generator feeds sensor_prod snapshots, the
		 * recorder runs its normal lifecycle, the store gets a real
		 * run, the phone downloads + decodes it like any other.
		 * Ground truth (pole-impact times/amplitudes, turn schedule)
		 * prints on serial as it is placed into the data.
		 *   S <secs> <gate_interval_s>   e.g. S 60 15
		 * GS turn model: 35°/s × 1.2 s turns every 4 s alternating;
		 * centripetal ≈ ±1100 mg; pole impact = 2.2–3.5 g × 3 samples
		 * decaying; seed fixed → runs reproducible. */
#if IS_ENABLED(CONFIG_SGC_BENCH)
		char *end;
		long secs = strtol(&req[1], &end, 10);
		long gate_s = strtol(end, &end, 10);
		char side = 'a';       /* a=all, r/l = dual-device one-side */

		while (*end == ' ') {
			end++;
		}
		if (*end == 'r' || *end == 'l' || *end == 'a') {
			side = *end;
		}
		if (secs < 10 || secs > 120 || gate_s > 60) {
			/* gate_s = 0 → every turn; side r/l → impacts
			 * only on that direction's turns (device emulation) */
			console_emit("E 3");            /* malformed */
		} else if (!sensor_synth_start((uint32_t)secs,
					       (uint32_t)gate_s, side)) {
			console_emit("E 5");            /* busy */
		}
#else
		console_emit("E 5");                /* not a bench build */
#endif
	} else if (cmd == 'a' || cmd == 'g' || cmd == 'e') {
		/* M3 step 1: recording state machine (synthetic producer).
		 * a=arm (120 s timeout) g=go e=end — production triggers
		 * replace g/e with the drop/flatline detectors later. */
		if (cmd == 'a') {
			recorder_arm();
		} else if (cmd == 'g') {
			recorder_start();
		} else {
			recorder_end();
		}
		console_emit("OK rec");
	} else if (cmd == 'b') {
		/* SGL3: set the active run budget (payload bytes). The
		 * discipline mapping (SL 90 KB / GS-SG 120 / DH 150 /
		 * custom 60-600) lives app-side at 1 KB/s. nRF Connect:
		 *   write "b 92160"   -> SL (90 s)
		 *   write "b 153600"  -> DH default (150 s)
		 */
		long n = strtol(&req[1], NULL, 10);

		if (n <= 0) {
			console_emit("E 3");            /* malformed */
		} else if (!store_set_run_budget((uint32_t)n)) {
			console_emit("E 5");            /* out of bounds */
		} else {
			char ok[40];

			snprintf(ok, sizeof(ok), "OK budget %lu B",
				 (unsigned long)store_run_budget());
			console_emit(ok);
		}
	} else if (cmd == 'd') {
		/* F46 per-run delete (v0.22.0, JP GO 2026-10-27): remove one
		 * run from the INDEX ONLY — instant, no data erase; the run's
		 * sectors reclaim lazily when the runway top-up walks over
		 * them (post-wipe semantics). Both transports land here via
		 * the shared capture: serial "d 12" or a BLE write of "d 12"
		 * to the ABCA console char; the reply goes back on the same
		 * console_emit path as every other command. */
		uint32_t id;
		int16_t pos = -1;

		if (parse_run_id(&req[1], &id) != 0) {
			console_emit("E 3");		/* malformed */
		} else if (recorder_state_idx() != 0) {
			console_emit("E 5");  /* ARMED/LOGGING/POST_RUN */
		} else if (dir_active || job_active) {
			/* v0.22.1: wire ids are STABLE — a mid-transfer
			 * delete can no longer shift what the phone sees
			 * (that hazard is structurally gone). Guard kept as
			 * defense: an in-flight transfer holds internal
			 * entries[] POSITIONS (job_run/dir_idx) and the
			 * index still compacts on delete (same guard as
			 * 'D'). */
			console_emit("E 4");
		} else if (id > 0xFFFF ||
			   (pos = store_run_pos((uint16_t)id)) < 0) {
			console_emit("OK already gone");  /* idempotent retry */
		} else if (!store_run_delete((uint8_t)pos)) {
			console_emit("E 5");   /* record/top-up in flight */
		} else {
			char ok[24];

			snprintf(ok, sizeof(ok), "OK deleted %u",
				 (unsigned)id);
			console_emit(ok);
		}
	} else if (cmd == 'x') {
		/* Bench reset: wipe the index sector and reboot the log to
		 * empty (data sectors reclaim lazily on the next writes).
		 * The escape hatch for "the store state is garbage".
		 * v0.9.4: TWO-STEP CONFIRM — 'x' arms (3 s window), 'X'
		 * executes. Two ZioClaw self-wipes in one night (a stray
		 * 'x' in a capture command; a node-timeout that didn't kill
		 * the PC-side process) bought this guard. */
		static int64_t arm_t0 = LLONG_MIN;

		if (req_via_ble) {
			/* phone never wipes: keep the frozen single-step path */
			store_wipe();
			console_emit("OK wiped");
		} else if (arm_t0 != LLONG_MIN &&
			   k_uptime_get() - arm_t0 <= 3000) {
			arm_t0 = LLONG_MIN;
			store_wipe();
			console_emit("OK wiped");
		} else {
			arm_t0 = k_uptime_get();
			console_emit("confirm: X within 3 s");
		}
	} else if (cmd == 'w') {
#if IS_ENABLED(CONFIG_SGC_BENCH)
		/* BENCH (v0.20.0): storage-lifecycle producer — raw ramp bytes
		 * into the store (budget/rotation/wrap testing; NOT sensor
		 * frames — for realistic runs use 'S', the frame producer). */
		long len = strtol(&req[1], NULL, 10);

		if (len <= 0) {
			console_emit("E 3");            /* malformed */
		} else if (!store_record_start((uint32_t)len)) {
			console_emit("E 5");            /* busy or out of policy */
		} else {
			printk("w: accepted %ld B\n", len);
		}
#else
		console_emit("E 5");                /* not a bench build */
#endif
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

/* v0.21.5: advertised name = F21-configured name, not the build
 * constant. Built fresh on every pull_proto_sd() call: name writes
 * arrive only while connected (advertising is down then), so the
 * next re-advertise picks up renames with no in-connection restart.
 * <=20 B name fits the 31-byte scan-response budget. */
static struct bt_data sd_dyn[] = {
	{ .type = BT_DATA_NAME_COMPLETE, .data_len = 0, .data = (const uint8_t *)"" },
};

const struct bt_data *pull_proto_ad(void)   { return ad; }
const struct bt_data *pull_proto_sd(void)
{
	const char *n = ble_config_dev_name();

	sd_dyn[0].data = (const uint8_t *)n;
	sd_dyn[0].data_len = (uint8_t)strlen(n);
	return sd_dyn;
}
uint8_t pull_proto_ad_len(void)             { return ARRAY_SIZE(ad); }
uint8_t pull_proto_sd_len(void)             { return ARRAY_SIZE(sd_dyn); }

/* ── Lifecycle hooks ─────────────────────────────────────────────── */

void pull_proto_on_disconnect(void)
{
	/* Dead link aborts any stream; request slot is void (the phone
	 * will re-send after reconnecting — stateless, SR-2). */
	k_work_cancel_delayable(&stream_work);
	job_active = false;
	k_work_cancel_delayable(&dir_work);
	dir_active = false;
	req_pending = false;
}

void pull_proto_start(void)
{
	k_work_init(&cmd_work, cmd_work_handler);
	k_work_init_delayable(&stream_work, stream_work_handler);
	k_work_init_delayable(&dir_work, dir_work_handler);

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

	rebuild_listing(true);
}
