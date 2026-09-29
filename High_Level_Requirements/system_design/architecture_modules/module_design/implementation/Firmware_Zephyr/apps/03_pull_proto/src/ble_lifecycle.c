/*
 * ble_lifecycle.c — advertising + connection lifecycle (step 3c)
 * ==============================================================
 * Grew out of app-02 v0.2.1/0.2.2 (LED-as-state era). Owns:
 *
 *   - connectable advertising (ADV packet = flags + product service
 *     UUID; scan response = device name — setAdvertisedService parity)
 *   - the deferred advertising restart: bt_le_adv_start() must NEVER
 *     run inside the disconnected callback — the old adv set is only
 *     released AFTER that callback returns, so an inline call fails
 *     with -ENOMEM and the device turns invisible (bench 09-22, the
 *     v0.2.1 lesson). Callback only SCHEDULES adv_work; the restart
 *     runs from the sysworkq moments later, with retry.
 *   - the `connected` flag everyone else reads.
 *
 * The advertisement names the product service, whose UUID is defined
 * in pull_proto.c (single source of truth) — hence the include.
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include "ble_lifecycle.h"
#include "pull_proto.h"

static volatile bool connected;

/* Recording suspension state (see ble_lifecycle_suspend below). */
static volatile bool suspended;
static struct bt_conn *cur_conn;

bool ble_lifecycle_connected(void)
{
	return connected;
}


/* ── Advertising ─────────────────────────────────────────────────── */

static void start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  pull_proto_ad(), pull_proto_ad_len(),
				  pull_proto_sd(), pull_proto_sd_len());
	if (err && err != -EALREADY) {
		printk("ERROR: adv start -> %d\n", err);
	}
}

/* Deferred restart with retry (self-rescheduling work — bounded 10
 * attempts, resets on success; see v0.2.2 discussion for the pattern). */
static struct k_work_delayable adv_work;

static void adv_work_handler(struct k_work *w)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  pull_proto_ad(), pull_proto_ad_len(),
				  pull_proto_sd(), pull_proto_sd_len());
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

/* ── Connection callbacks (BT RX thread: short, defer, return) ──── */

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
	if (cur_conn) {
		bt_conn_unref(cur_conn);
	}
	cur_conn = bt_conn_ref(conn);
	printk("connected: %s (LED -> solid green)\n", addr);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	connected = false;
	if (cur_conn) {
		bt_conn_unref(cur_conn);
		cur_conn = NULL;
	}

	/* Pull protocol owns stream state — let it abort cleanly
	 * (stateless design, SR-2: a disconnect costs one reconnect). */
	pull_proto_on_disconnect();

	if (suspended) {
		printk("disconnected: %s (reason 0x%02x), SUSPENDED "
		       "(recording)\n", addr, reason);
		return;
	}

	printk("disconnected: %s (reason 0x%02x), re-advertising\n",
	       addr, reason);

	k_work_schedule(&adv_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected_cb,
	.disconnected = disconnected_cb,
};

/* ── Bring-up ────────────────────────────────────────────────────── */

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

void ble_lifecycle_start(void)
{
	k_work_init_delayable(&adv_work, adv_work_handler);

	int err = bt_enable(bt_ready);

	if (err) {
		printk("ERROR: bt_enable -> %d\n", err);
	}
}

/* ── Recording suspension (JP 2026-09-29: ARM closes the link) ────
 * While ARMED/LOGGING there is no phone business — the connection and
 * advertising are torn down so acquisition runs with maximal memory
 * and radio quiet. The disconnected callback must NOT re-advertise
 * while suspended. Resumed when the recorder returns to IDLE. */
void ble_lifecycle_suspend(void)
{
	suspended = true;

	int err = bt_le_adv_stop();

	if (err && err != -EALREADY) {
		printk("rec-suspend: adv stop -> %d\n", err);
	}
	if (cur_conn) {
		bt_conn_disconnect(cur_conn,
				   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
	printk("rec-suspend: BLE down for acquisition\n");
}

void ble_lifecycle_resume(void)
{
	if (!suspended) {
		return;
	}
	suspended = false;
	k_work_schedule(&adv_work, K_NO_WAIT);
	printk("rec-resume: advertising\n");
}

