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

/* One generic callback: the vendored parser hands us callback_info with
 * data_ptr/data_size already sliced per sensor frame. */
static void on_frame(const struct bhy2_fifo_parse_data_info *info, void *priv)
{
	ARG_UNUSED(priv);

	switch (info->sensor_id) {
	case BHY2_SENSOR_ID_RV: {
		if (info->data_size != 10) { prod.short_frames++; return; }
		struct bhy2_data_quaternion q;
		bhy2_parse_quaternion(info->data_ptr, &q);
		prod.qw = q.w; prod.qx = q.x; prod.qy = q.y; prod.qz = q.z;
		prod.n_rv++;
		return;
	}
	case BHY2_SENSOR_ID_LACC: {
		if (info->data_size != 6) { prod.short_frames++; return; }
		struct bhy2_data_xyz v;
		bhy2_parse_xyz(info->data_ptr, &v);
		prod.la_x = v.x; prod.la_y = v.y; prod.la_z = v.z;
		prod.n_lacc++;
		return;
	}
	case BHY2_SENSOR_ID_BARO: {
		/* bhy2 baro payload: uint32 Pa LE + uint16 accuracy */
		if (info->data_size != 6) { prod.short_frames++; return; }
		prod.baro_pa = (uint32_t)info->data_ptr[0] |
			       ((uint32_t)info->data_ptr[1] << 8) |
			       ((uint32_t)info->data_ptr[2] << 16) |
			       ((uint32_t)info->data_ptr[3] << 24);
		prod.n_baro++;
		return;
	}
	case BHY2_SENSOR_ID_TEMP: {
		/* bhy2 temp payload: int16, 0.01 °C/LSB (bhy2_data_temperature) */
		if (info->data_size != 2) { prod.short_frames++; return; }
		int16_t t = (int16_t)(info->data_ptr[0] |
				      (info->data_ptr[1] << 8));
		prod.temp_cx10 = t / 10;   /* → 0.1 °C/LSB, ArduinoFW semantics */
		prod.n_temp++;
		return;
	}
	default:
		return;   /* status/meta frames — not counted (B1 scope) */
	}
}

/* ---- drain thread: poll FIFO every 10 ms ---- */
#define WORK_BUF_SIZE 256u   /* ~27 B/tick @300 f/s → 10× headroom */
static uint8_t work_buf[WORK_BUF_SIZE];

static void drain_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	struct bhy2_dev *dev = bhy2_zephyr_dev();

	while (dev != NULL) {
		int8_t rslt = bhy2_get_and_process_fifo(work_buf, WORK_BUF_SIZE, dev);
		if (rslt != BHY2_OK) {
			prod.errors++;
		}
		k_sleep(K_MSEC(10));
	}
}
K_THREAD_DEFINE(sensor_prod_tid, 1536, drain_thread, NULL, NULL, NULL,
		5, 0, K_FOREVER);

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
		       "\"baro_pa\":%u,\"qw\":%d,\"tx10\":%d}\n",
		       prod.n_rv, prod.n_lacc, prod.n_baro, prod.n_temp,
		       prod.errors, prod.short_frames,
		       prod.baro_pa, prod.qw, prod.temp_cx10);
		memset(&prod, 0, sizeof(prod));   /* 1 s window */
	}
}
K_THREAD_DEFINE(sensor_prod_stats_tid, 1024, stats_thread, NULL, NULL, NULL,
		10, 0, K_FOREVER);

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

		rslt = bhy2_register_fifo_parse_callback(cfgs[i].id,
							on_frame, NULL, dev);
		printk("{\"ev\":\"b1_cfg\",\"sid\":%u,\"cb_rslt\":%d}\n",
		       cfgs[i].id, rslt);
	}

	k_thread_start(sensor_prod_tid);
	k_thread_start(sensor_prod_stats_tid);
	printk("{\"ev\":\"b1_cfg\",\"ok\":true}\n");
}
