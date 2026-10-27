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
 * (v0.20.0 cleanup: the B2 golden raw-vector capture and the one-shot
 * FIFO hexdump were removed — the host differential suite owns that
 * ground now; synthetic runs enter at the FRAME level via the
 * sensor_prod_get_frame() override.) */

/* ---- B3: latest-value snapshot (recorder pulls each 10 ms tick) ---- */
static volatile struct sgc_raw_frame snap;   /* quat seqlocked below */
static volatile uint32_t snap_seq;           /* even = stable */
static volatile bool snap_quat_valid;

static void snap_quat_write(int16_t w, int16_t x, int16_t y, int16_t z)
{
	snap_seq++;                       /* odd = write in flight */
	__asm volatile("dmb" ::: "memory");
	snap.q_w = w; snap.q_x = x; snap.q_y = y; snap.q_z = z;
	__asm volatile("dmb" ::: "memory");
	snap_seq++;
	snap_quat_valid = true;
}

bool sensor_prod_get_frame(struct sgc_raw_frame *out)
{
	/* Bench override first: synthetic 'S' runs feed the identical
	 * downstream path (BitPacker → ring → store), even sensor-less. */
#if IS_ENABLED(CONFIG_SGC_BENCH)
	{
		extern bool sensor_synth_get_frame(struct sgc_raw_frame *f);

		if (sensor_synth_get_frame(out)) {
			return true;
		}
	}
#endif
	if (!snap_quat_valid) {
		return false;
	}
	for (int retry = 0; retry < 4; retry++) {
		uint32_t s1 = snap_seq;
		if (s1 & 1u) {
			continue;
		}
		__asm volatile("dmb" ::: "memory");
		out->q_w = snap.q_w; out->q_x = snap.q_x;
		out->q_y = snap.q_y; out->q_z = snap.q_z;
		out->la_x = snap.la_x; out->la_y = snap.la_y;
		out->la_z = snap.la_z;
		out->baro_pa_div2 = snap.baro_pa_div2;
		__asm volatile("dmb" ::: "memory");
		if (snap_seq == s1) {
			return true;
		}
	}
	return false;   /* writer won the race 4x — skip this tick */
}

/* Layout (v0.12.8+): payload starts at data_ptr+0; event size INCLUDES
 * the SID byte (RV=11, LACC=7, BARO=4, TEMP=5). No status byte in stream. */
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
				snap_quat_write(q.w, q.x, q.y, q.z);
		return;
	}
	case BHY2_SENSOR_ID_LACC: {
		if (info->data_size != 7) { prod.short_frames++; note_obs(info->sensor_id, info->data_size, info->data_ptr); return; }
		struct bhy2_data_xyz v;
		bhy2_parse_xyz(info->data_ptr, &v);
		prod.la_x = v.x; prod.la_y = v.y; prod.la_z = v.z;
		prod.n_lacc++;
				snap.la_x = v.x; snap.la_y = v.y; snap.la_z = v.z;
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
				/* hPa x50 == Pa/2 (ArduinoFW: pressure.value()*50.0f);
		 * integer form, <=1 LSB vs the float path. */
		snap.baro_pa_div2 = (uint16_t)((prod.baro_pa * 39u) / 100u);
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

	static uint32_t idle_polls;   /* consecutive polls with zero events */
	while (dev != NULL) {
		uint32_t n0 = prod.n_rv + prod.n_lacc + prod.n_baro + prod.n_temp;
		int8_t rslt = bhy2_get_and_process_fifo(work_buf, WORK_BUF_SIZE, dev);
		uint32_t n1 = prod.n_rv + prod.n_lacc + prod.n_baro + prod.n_temp;

		if (rslt != BHY2_OK) {
			prod.errors++;
		}
		/* Hub-FIFO stall guard (2026-10-03: 28 s into the first
		 * combined run, events stopped with rslt==0 — hub FIFO
		 * overflow stall, needs FLUSH to restart). */
		if (n1 == n0) {
			if (++idle_polls == 500u) {
				printk("{\"ev\":\"bhy_stall\",\"flush\":%d}\n",
				       (int)bhy2_hif_set_fifo_flush(0, &dev->hif));
				idle_polls = 0;
			}
		} else {
			idle_polls = 0;
		}
		k_sleep(K_MSEC(10));
	}
}
/* Zephyr 4.4.2: K_THREAD_DEFINE delay is ms-int (-1 = SYS_FOREVER_MS);
 * K_FOREVER (k_timeout_t) does NOT compile here. -1 = startable later
 * via k_thread_start(). */
/* Prio 3 — ABOVE the recorder workqueue (preempt 4): BHI and the NOR
 * flash SHARE spi2; acquisition must win the bus over page programs. */
K_THREAD_DEFINE(sensor_prod_tid, 1536, drain_thread, NULL, NULL, NULL,
		3, 0, -1);

#if IS_ENABLED(CONFIG_SGC_BENCH)
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
		prod.n_rv = 0; prod.n_lacc = 0; prod.n_baro = 0; prod.n_temp = 0;
	prod.errors = 0; prod.short_frames = 0;   /* counters only — keep values */
	}
}
K_THREAD_DEFINE(sensor_prod_stats_tid, 1024, stats_thread, NULL, NULL, NULL,
		10, 0, -1);
#endif /* CONFIG_SGC_BENCH */

/* ---- setup ---- */
static const struct { uint8_t id; float rate; } cfgs[] = {
	{ BHY2_SENSOR_ID_RV,   100.0f },
	{ BHY2_SENSOR_ID_LACC, 100.0f },
	{ BHY2_SENSOR_ID_BARO, 100.0f },
	{ BHY2_SENSOR_ID_TEMP,   1.0f },
};

/* ---- SLEEP suspension (F12, v0.23.0) ----
 * power_state parks the producer while the device sleeps: the drain
 * thread first (no SPI traffic while the hub is reconfigured from
 * another thread — the flash shares spi2 and the recorder may run
 * right after the wake), then the hub virtual sensors via SensorConf
 * with 0 Hz — the BHY2 firmware semantic for 'disabled': the hub
 * simply stops generating FIFO events. The hub itself stays awake,
 * so resume is two config writes, not a re-init (F13: no sensor
 * re-init on wake). The bench b1_rate thread (CONFIG_SGC_BENCH)
 * keeps ticking — it just prints zeros while parked, honestly. */
static volatile bool prod_running;   /* set once drain thread started */

void sensor_prod_suspend(void)
{
	if (!prod_running) {
		return;   /* hub never came up — nothing to park */
	}
	k_thread_suspend(sensor_prod_tid);

	struct bhy2_dev *dev = bhy2_zephyr_dev();

	if (dev == NULL) {
		return;
	}
	for (int i = 0; i < (int)ARRAY_SIZE(cfgs); i++) {
		int8_t rslt = bhy2_set_virt_sensor_cfg(cfgs[i].id, 0.0f,
						       0, dev);

		printk("{\"ev\":\"b1_sleep\",\"sid\":%u,\"rslt\":%d}\n",
		       cfgs[i].id, rslt);
	}
	printk("{\"ev\":\"sleep\",\"sensors\":\"parked\"}\n");
}

void sensor_prod_resume(void)
{
	if (!prod_running) {
		return;
	}
	struct bhy2_dev *dev = bhy2_zephyr_dev();

	if (dev != NULL) {
		for (int i = 0; i < (int)ARRAY_SIZE(cfgs); i++) {
			int8_t rslt = bhy2_set_virt_sensor_cfg(cfgs[i].id,
							       cfgs[i].rate,
							       0, dev);

			printk("{\"ev\":\"b1_wake\",\"sid\":%u,"
			       "\"rslt\":%d}\n", cfgs[i].id, rslt);
		}
	}
	k_thread_resume(sensor_prod_tid);
	printk("{\"ev\":\"wake\",\"sensors\":\"running\"}\n");
}

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
#if IS_ENABLED(CONFIG_SGC_BENCH)
	k_thread_start(sensor_prod_stats_tid);
#endif
	prod_running = true;
	printk("{\"ev\":\"b1_cfg\",\"ok\":true}\n");
}
