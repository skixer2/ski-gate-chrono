/*
 * sensor_prod.c — Phase B step B1: real BHI260 virtual sensors → FIFO drain.
 *
 * Scope (B1 ONLY — no storage, no recorder changes, no BitPacker):
 *   - configure RV(34)/LACC(31)/BARO(129) @ 100 Hz, TEMP(128) @ 1 Hz,
 *     latency 0 — the exact ArduinoFW set (main.cpp:1346–1349)
 *   - dedicated drain thread, 10 ms tick (JP decision: poll, not INT)
 *   - FIFO parse callbacks count frames per sensor + keep latest values
 *   - 1 Hz JSON stats line (separate line — wire-contract discipline)
 *
 * Pass gate (B1): ≥95% of 100 f/s per 100 Hz sensor over 60 s, 0 overflows.
 *
 * Rates are ArduinoFW-verbatim; range untouched (hub defaults) so LACC
 * arrives as int16 mm/s², RV as Q14 — the B2 encoder contract.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "sensor_prod.h"
#include "bhy2/bhy2_zephyr.h"
#include "bhy2/bhy2_defs.h"
#include "bhy2/bhy2_parse.h"
#include "bhy2/bhy2_hif.h"

/* ---- latest values (written in drain-thread context only) ---- */
static struct {
	uint32_t n_rv, n_lacc, n_baro, n_temp;
	uint32_t errors;            /* non-OK rslt from get_and_process_fifo */
	uint32_t short_frames;      /* parse cb with unexpected payload size */
	int16_t  qw, qx, qy, qz;    /* Q14, raw from hub */
	int16_t  la_x, la_y, la_z;  /* mm/s², raw from hub */
	uint32_t baro_pa;           /* Pa, from baro virtual sensor */
	int16_t  temp_cx10;         /* 0.1 °C/LSB — ArduinoFW header semantics */
} prod;

/* Observed (sid,size) pairs + raw bytes of first frame per sid (decode aid) */
static struct { uint8_t sid; uint8_t size; uint16_t n; uint8_t raw[8]; bool grabbed; } obs[6];
static void note_obs(uint8_t sid, uint8_t size, const uint8_t *data)
{
	for (int i = 0; i < 6; i++) {
		if (obs[i].n != 0 && obs[i].sid == sid && obs[i].size == size) {
			obs[i].n++; return;
		}
	}
	for (int i = 0; i < 6; i++) {
		if (obs[i].n == 0) {
			obs[i].sid = sid; obs[i].size = size; obs[i].n = 1;
			if (!obs[i].grabbed && size <= 8) {
				memcpy(obs[i].raw, data, size); obs[i].grabbed = true;
			}
			return;
		}
	}
}

/* One generic callback: the vendored parser hands us callback_info with
 * data_ptr/data_size already sliced per sensor frame.
 *
 * B2 golden vectors: 'v' arms 8 s; first GOLDEN_MAX frames per sensor print
 * raw event bytes (SID included) next to the firmware decode. Serial-only. */

#define GOLDEN_MAX 5
static int64_t golden_deadline;
static uint8_t golden_got[4];
void sensor_prod_golden_arm(void);
static void golden_emit(uint8_t idx, uint8_t sid,
			const struct bhy2_fifo_parse_data_info *info,
			const int32_t *v, uint8_t vn)
{
	if (k_uptime_get() >= golden_deadline || golden_got[idx] >= GOLDEN_MAX) {
		return;
	}
	golden_got[idx]++;
	const uint8_t *b = info->data_ptr - 1;   /* include SID byte */
	printk("{\"ev\":\"gv\",\"sid\":%u,\"hex\":\"", sid);
	for (uint8_t i = 0; i < info->data_size; i++) {
		printk("%02x", b[i]);
	}
	printk("\",\"v\":[");
	for (uint8_t i = 0; i < vn; i++) {
		printk("%s%d", i ? "," : "", v[i]);
	}
	printk("]}\n");
}
 * BHY2 frame layout: data_ptr[0] = status byte, payload at +1;
 * data_size INCLUDES the status byte (RV=11, LACC=7, BARO=7, TEMP=3). */
static void on_frame(const struct bhy2_fifo_parse_data_info *info, void *priv)
{
	ARG_UNUSED(priv);

	switch (info->sensor_id) {
	case BHY2_SENSOR_ID_RV: {
		if (info->data_size != 11) { prod.short_frames++; note_obs(info->sensor_id, info->data_size, info->data_ptr); return; }
		struct bhy2_data_quaternion q;
		bhy2_parse_quaternion(info->data_ptr, &q);
		prod.qw = q.w; prod.qx = q.x; prod.qy = q.y; prod.qz = q.z;
		prod.n_rv++;
		{
			int32_t gv[5] = { q.x, q.y, q.z, q.w,
				(int16_t)(info->data_ptr[8] | (info->data_ptr[9] << 8)) };
			golden_emit(0, info->sensor_id, info, gv, 5);
		}
		return;
	}
	case BHY2_SENSOR_ID_LACC: {
		if (info->data_size != 7) { prod.short_frames++; note_obs(info->sensor_id, info->data_size, info->data_ptr); return; }
		struct bhy2_data_xyz v;
		bhy2_parse_xyz(info->data_ptr, &v);
		prod.la_x = v.x; prod.la_y = v.y; prod.la_z = v.z;
		prod.n_lacc++;
		{
			int32_t gv[3] = { v.x, v.y, v.z };
			golden_emit(1, info->sensor_id, info, gv, 3);
		}
		return;
	}
	case BHY2_SENSOR_ID_BARO: {
		/* OBSERVED 4 B @ ~16.4 Hz (hub clamps BMP390 pass-through rate).
		 * Decode deferred: hold-last for frames, ArduinoFW parity. */
		if (info->data_size != 4) { prod.short_frames++; note_obs(info->sensor_id, info->data_size, info->data_ptr); return; }
		const uint8_t *d = info->data_ptr;   /* event = SID + uint24 raw */
		prod.baro_pa = (uint32_t)d[0] |
			       ((uint32_t)d[1] << 8) |
			       ((uint32_t)d[2] << 16);   /* uint24 LE, x0.0078 -> hPa */
		prod.n_baro++;
		{
			int32_t gv[1] = { (int32_t)prod.baro_pa };
			golden_emit(2, info->sensor_id, info, gv, 1);
		}
		return;
	}
	case BHY2_SENSOR_ID_TEMP: {
		/* OBSERVED 5 B @ ~0.5 Hz — payload layout under investigation;
		 * candidate: status + int16 (0.01 °C/LSB) at +1. */
		if (info->data_size != 5) { prod.short_frames++; note_obs(info->sensor_id, info->data_size, info->data_ptr); return; }
		const uint8_t *d = info->data_ptr;   /* event = SID + 4 (int16 + 2) */
		int16_t t = (int16_t)(d[0] | (d[1] << 8));
		prod.temp_cx10 = t / 10;   /* candidate decode, ArduinoFW semantics */
		prod.n_temp++;
		{
			int32_t gv[1] = { t };
			golden_emit(3, info->sensor_id, info, gv, 1);
		}
		return;
	}
	default:
		return;   /* status/meta frames — not counted (B1 scope) */
	}
}


void sensor_prod_golden_arm(void)
{
	for (int i = 0; i < 4; i++) {
		golden_got[i] = 0;
	}
	golden_deadline = k_uptime_get() + 8000;
	printk("{\"ev\":\"golden_armed\",\"ms\":8000}\n");
}

/* ---- drain thread: poll FIFO every 10 ms ---- */
#define WORK_BUF_SIZE 256u   /* ~27 B/tick @300 f/s → 10× headroom */
static uint8_t work_buf[WORK_BUF_SIZE];

static void drain_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct bhy2_dev *dev = bhy2_zephyr_dev();

	static bool dumped = false;
	while (dev != NULL) {
		int8_t rslt = bhy2_get_and_process_fifo(work_buf, WORK_BUF_SIZE, dev);
		if (rslt != BHY2_OK) {
			prod.errors++;
		}
		/* one-shot raw FIFO hexdump @ ~5 s (decode aid, remove after B1) */
		if (!dumped) {
			static uint32_t ticks = 0;
			if (++ticks == 500) {
				dumped = true;
				k_sleep(K_MSEC(50));   /* let the FIFO refill */
				uint32_t rd = 0, rem = 0;
				if (bhy2_hif_get_nonwakeup_fifo(work_buf, 64, &rd,
								&rem, &dev->hif) == BHY2_OK &&
				    rd > 0) {
					printk("FIFOHEX n=%u ", rd);
					for (uint32_t i = 0; i < rd && i < 64; i++) {
						printk("%02x", work_buf[i]);
					}
					printk("\n");
				}
			}
		}
		k_sleep(K_MSEC(10));
	}
}
/* Zephyr 4.4.2: K_THREAD_DEFINE delay is ms-int (-1 = SYS_FOREVER_MS);
 * K_FOREVER (k_timeout_t) does NOT compile here. -1 = startable later
 * via k_thread_start(). */
K_THREAD_DEFINE(sensor_prod_tid, 1536, drain_thread, NULL, NULL, NULL,
		5, 0, -1);

/* ---- 1 Hz stats (own low-prio thread; printk from drain ctx avoided) ---- */
static void stats_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (true) {
		k_sleep(K_SECONDS(1));
		if (bhy2_zephyr_dev() == NULL) {
			continue;
		}
		printk("{\"ev\":\"b1_rate\",\"rv\":%u,\"lacc\":%u,\"baro\":%u,"
		       "\"temp\":%u,\"err\":%u,\"short\":%u,"
		       "\"baro_pa\":%u,\"qw\":%d,\"tx10\":%d,\"la\":[%d,%d,%d],"
		       "\"obs\":[[%u,%u,%u],[%u,%u,%u],[%u,%u,%u],[%u,%u,%u]],"
		       "\"r0\":[%u,%u,%u,%u,%u,%u,%u,%u]}\n",
		       prod.n_rv, prod.n_lacc, prod.n_baro, prod.n_temp,
		       prod.errors, prod.short_frames,
		       prod.baro_pa, prod.qw, prod.temp_cx10, prod.la_x, prod.la_y, prod.la_z,
		       obs[0].sid, obs[0].size, obs[0].n,
		       obs[1].sid, obs[1].size, obs[1].n,
		       obs[2].sid, obs[2].size, obs[2].n,
		       obs[3].sid, obs[3].size, obs[3].n,
		       obs[0].raw[0],obs[0].raw[1],obs[0].raw[2],obs[0].raw[3],
		       obs[0].raw[4],obs[0].raw[5],obs[0].raw[6],obs[0].raw[7]);
		memset(&prod, 0, sizeof(prod));   /* 1 s window */
	}
}
K_THREAD_DEFINE(sensor_prod_stats_tid, 1024, stats_thread, NULL, NULL, NULL,
		10, 0, -1);

/* ---- setup ---- */
static const struct { uint8_t id; float rate; } cfgs[] = {
	{ BHY2_SENSOR_ID_RV,   100.0f },
	{ BHY2_SENSOR_ID_LACC, 100.0f },
	{ BHY2_SENSOR_ID_BARO, 100.0f },
	{ BHY2_SENSOR_ID_TEMP,   1.0f },
};

void sensor_prod_start(void)
{
	struct bhy2_dev *dev = bhy2_zephyr_dev();
	if (dev == NULL) {
		printk("{\"ev\":\"b1_cfg\",\"ok\":false,\"reason\":\"hub_not_ready\"}\n");
		return;
	}

	memset(&prod, 0, sizeof(prod));

	int8_t rslt = bhy2_update_virtual_sensor_list(dev);
	printk("{\"ev\":\"b1_cfg\",\"step\":\"sensor_list\",\"rslt\":%d}\n", rslt);

	for (int i = 0; i < (int)ARRAY_SIZE(cfgs); i++) {
		rslt = bhy2_set_virt_sensor_cfg(cfgs[i].id, cfgs[i].rate,
						0 /* latency ms */, dev);
		printk("{\"ev\":\"b1_cfg\",\"sid\":%u,\"rate\":%u,\"rslt\":%d}\n",
		       cfgs[i].id, (unsigned)cfgs[i].rate, rslt);

		bhy2_zephyr_set_fifo_callback(cfgs[i].id,
					      on_frame, NULL);
		rslt = BHY2_OK;
		printk("{\"ev\":\"b1_cfg\",\"sid\":%u,\"cb_rslt\":%d}\n",
		       cfgs[i].id, rslt);
	}

	k_thread_start(sensor_prod_tid);
	k_thread_start(sensor_prod_stats_tid);
	printk("{\"ev\":\"b1_cfg\",\"ok\":true}\n");
}
