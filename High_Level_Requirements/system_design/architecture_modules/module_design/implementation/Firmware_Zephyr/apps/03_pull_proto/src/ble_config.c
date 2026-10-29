/*
 * ble_config.c — see ble_config.h. Battery recipe ported verbatim from
 * ArduinoCore-mbed Nicla_System (getBatteryVoltagePercentage), BQ25120A
 * @ 0x6A on i2c1 (Wire1). Sources: BQ25120A.h/.cpp, Nicla_System.cpp
 * (fetched 2026-10-09, recorded in the ledger).
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/i2c.h>
#include <string.h>
#include "ble_config.h"
#include "ble_lifecycle.h"   /* 0.23.3: connected-aware battery poll rate */
#include "store.h"
#include "recorder.h"

/* ── Persistence (ArduinoFW FlashConfig, byte-identical) ──────────── */
#define CFG_ADDR        0x1FC000u
#define CFG_MAGIC       0x53474343u   /* "SGCC" — same as ArduinoFW   */
struct __attribute__((packed)) cfg_disk {
	uint32_t magic;
	char     dev_name[21];
	uint8_t  arm_side;
	uint8_t  discipline;
	uint8_t  _pad[5];
};

static char    cfg_name[21] = "SGC";
static uint8_t cfg_arm;              /* 0=left */
static uint8_t cfg_disc = 1;         /* GS parity with ArduinoFW */

static struct k_work_delayable cfg_flush;
static struct k_work_delayable batt_work;

static void cfg_flush_handler(struct k_work *w)
{
	const struct device *f = store_flash_device();

	if (!f) {
		printk("cfg: no flash device\n");
		return;
	}
	struct cfg_disk d;

	memset(&d, 0, sizeof(d));
	d.magic = CFG_MAGIC;
	strncpy(d.dev_name, cfg_name, sizeof(d.dev_name) - 1);
	d.arm_side = cfg_arm;
	d.discipline = cfg_disc;

	int rc = flash_erase(f, CFG_ADDR, 4096);

	if (rc == 0) {
		rc = flash_write(f, CFG_ADDR, &d, sizeof(d));
	}
	printk("cfg: persist '%s' (arm=%u disc=%u) rc=%d\n",
	       cfg_name, cfg_arm, cfg_disc, rc);
}

static void cfg_schedule_flush(void)
{
	/* Coalesce: one pending flush picks up the newest RAM values. */
	k_work_reschedule(&cfg_flush, K_MSEC(250));
}

static void cfg_load(void)
{
	const struct device *f = store_flash_device();
	struct cfg_disk d;

	if (!f || flash_read(f, CFG_ADDR, &d, sizeof(d)) != 0) {
		printk("cfg: load read FAILED — defaults\n");
		return;
	}
	if (d.magic != CFG_MAGIC) {
		printk("cfg: no valid config (magic 0x%08lx) — defaults\n",
		       (unsigned long)d.magic);
		return;
	}
	memcpy(cfg_name, d.dev_name, 20);
	cfg_name[20] = '\0';
	cfg_arm = d.arm_side ? 1 : 0;
	cfg_disc = (d.discipline > 3) ? 1 : d.discipline;
	printk("cfg: loaded '%s' arm=%u disc=%u\n",
	       cfg_name, cfg_arm, cfg_disc);
}

/* ── Config API ───────────────────────────────────────────────────── */
const char *ble_config_dev_name(void)  { return cfg_name; }
uint8_t ble_config_arm_side(void)      { return cfg_arm; }
uint8_t ble_config_discipline(void)    { return cfg_disc; }

void ble_config_set_dev_name(const char *name, uint8_t len)
{
	if (len > 20) {
		len = 20;
	}
	memcpy(cfg_name, name, len);
	cfg_name[len] = '\0';
	cfg_schedule_flush();
}

void ble_config_set_arm_side(uint8_t v)
{
	cfg_arm = v ? 1 : 0;
	cfg_schedule_flush();
}

void ble_config_set_discipline(uint8_t v)
{
	cfg_disc = (v > 3) ? 1 : v;   /* ArduinoFW parity: >3 → 1 */
	cfg_schedule_flush();
}

/* ── Battery (BQ25120A @ 0x6A on i2c1) ────────────────────────────── */
#define BQ_ADDR       0x6A
#define BQ_STATUS     0x00
#define BQ_FAULTS     0x01
#define BQ_FAST_CHG   0x03
#define BQ_BATT_MON   0x0A

#define BATT_UNKNOWN  0x7F

static const struct device *bqi2c;
static uint8_t batt_cache = BATT_UNKNOWN;
static bool batt_vin;          /* VIN present at last good read (USB power) */

static int bq_read(uint8_t reg, uint8_t *val)
{
	return i2c_write_read(bqi2c, BQ_ADDR, &reg, 1, val, 1);
}

static int bq_write(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };

	return i2c_write(bqi2c, buf, 2, BQ_ADDR);
}

/* Nicla_System.cpp getBatteryVoltagePercentage() — verbatim port.
 * -1 no battery / -2 unreadable / -3 skipped (battery mode) /
 * -4 I2C dead; else 0..100. */
static bool bq_on_vin = true;   /* 0.23.2: VIN state from FAULTS, refreshed per read */

static int bq_battery_pct(void)
{
	uint8_t faults;

	if (bq_read(BQ_FAULTS, &faults) != 0) {
		return -4;
	}
	static bool diag_done;   /* one-shot bench diagnostic (values) */
	if (!diag_done) {
		diag_done = true;
		uint8_t st = 0xFF;
		bq_read(BQ_STATUS, &st);
		printk("batt: BQ alive, faults=0x%02x status=0x%02x\n",
		       faults, st);
	}
	if (faults & 0x20) {            /* BAT_UVLO: not connected / too low */
		return -1;
	}

	const bool on_battery = (faults & 0x40) != 0;
	bq_on_vin = !on_battery;

	uint8_t data;

	if (on_battery) {
		/* 0.23.2 (JP bench 2026-10-27: battery % showed "unknown" the
		 * first time the device ran on battery): with VIN absent nothing
		 * is charging, so the datasheet 9.3.4 charge-pause dance is
		 * unnecessary — trigger VBMON directly and read it. The 0.21.4
		 * "CD-pin dance not ported" caveat only ever applied to reads
		 * taken while VIN was present. */
		if (bq_write(BQ_BATT_MON, 0x80) != 0) {   /* VBMON_READ */
			return -4;
		}
		k_msleep(3);
		if (bq_read(BQ_BATT_MON, &data) != 0) {
			return -4;
		}
	} else {
		/* Pause charging for an accurate reading (datasheet 9.3.4). */
		uint8_t fastchg;

		if (bq_read(BQ_FAST_CHG, &fastchg) != 0) {
			return -4;
		}
		bool was_charging = (fastchg & 0x02) == 0;

		if (fastchg & 0x02) {
			/* 0.23.5 self-heal (bench 2026-10-28: after 13 h on USB
			 * the cell still read the 92% band — a paused charger
			 * floats the cell at its natural voltage and the band
			 * never moves. A failed restore in ANY earlier poll
			 * leaves bit1 stuck FOREVER: was_charging=false then
			 * skips the restore on every later read. Clear it,
			 * loudly, then run the normal pause->read->restore.) */
			if (bq_write(BQ_FAST_CHG, fastchg & ~0x02) == 0) {
				printk("batt: charger was PAUSED - restored\n");
			}
			was_charging = true;
		}

		if (was_charging && bq_write(BQ_FAST_CHG, fastchg | 0x02) != 0) {
			return -4;
		}
		int rc = bq_write(BQ_BATT_MON, 0x80);   /* VBMON_READ */
		k_msleep(3);
		if (was_charging) {
			/* 0.23.5: the restore may not silently fail — a single
			 * NAK here pauses charging forever (see self-heal
			 * above). Retry; scream if it sticks. */
			int rr = -1;

			for (int i = 0; i < 3 && rr != 0; i++) {
				rr = bq_write(BQ_FAST_CHG, fastchg & ~0x02);
			}
			if (rr != 0) {
				printk("batt: FAST_CHG restore FAILED\n");
			}
		}
		if (rc != 0) {
			return -4;
		}
		if (bq_read(BQ_BATT_MON, &data) != 0) {
			return -4;
		}
	}
	if ((data & 0xFC) == 0) {
		return -2;
	}

	static const int threshold_lookup[] = { 0, 2, 4, 6, 0, 0, 8, 10 };
	int tens = 60 + ((data >> 5) & 0x03) * 10;
	int ones = threshold_lookup[(data >> 2) & 0x07];

	return tens + ones;
}

static void batt_work_handler(struct k_work *w)
{
	struct k_work_delayable *d =
		CONTAINER_OF(w, struct k_work_delayable, work);
	int pct = bq_battery_pct();

	switch (pct) {
	case -3:
		/* no longer produced (0.23.2) — defensive */
		batt_vin = false;
		batt_cache = BATT_UNKNOWN;
		break;
	case -4:
		printk("batt: BQ25120A I2C error\n");
		batt_cache = BATT_UNKNOWN;
		break;
	case -1:
	case -2:
		/* 0.23.3: floor code (-2) = voltage BELOW the lowest resolvable
		 * band (~60%) — that is real information, not an error. Encode it
		 * as the 0 sentinel; the app shows "< 60%". Real 0%% never occurs
		 * (the encoding floor is 60). -1 (BAT_UVLO) stays unknown. */
		batt_cache = (pct == -2) ? 0 : BATT_UNKNOWN;
		break;
	default:
		batt_cache = (uint8_t)(pct & 0x7F);
		/* 0.23.2: VIN state now comes from the FAULTS register
		 * (bq_on_vin), NOT from read success — battery-mode reads
		 * succeed too, so bit7 (charging) stays false on battery. */
		batt_vin = bq_on_vin;
		break;
	}
	/* 0.23.3 (JP: charging indication should change live when USB is
	 * plugged/unplugged): poll fast while a phone is linked — the app
	 * refreshes every 5 s — and slow otherwise so SLEEP keeps its long
	 * idle stretches (a 5 s poll would wake the SoC 12x/min for nothing). */
	k_work_reschedule(d, K_SECONDS(
		ble_lifecycle_connected() ? 5 : 30));
}

/* ── Status surfaces ──────────────────────────────────────────────── */
uint8_t ble_config_battery_byte(void)
{
	/* v0.21.4: bit7 = on USB power AND not full (charging). VIN state is
	 * implied by the read path (success ⇒ VIN present; -3 ⇒ battery).
	 * CHG_STAT pin still TODO — this reports "charging" until 100%, then
	 * "on USB, held" without the bolt. Good enough for the snow kit. */
	bool charging = batt_vin && batt_cache != BATT_UNKNOWN &&
			batt_cache < 100;
	return batt_cache | (charging ? 0x80 : 0x00);
}

bool ble_config_on_vin(void)
{
	/* 0.23.5 (JP GO 2026-10-28): power policy input — on USB the
	 * device must NOT System Off (a bench device left unconnected
	 * went dark for 11 h). Refreshed every poll (5 s / 30 s); the
	 * boot-time default is true, which can only delay (never
	 * enable) an off by one idle-poll period on battery. */
	return bq_on_vin;
}

uint8_t ble_config_state_byte(void)
{
	/* recorder: SLEEP=0 ARMED=1 LOGGING=2 POST_RUN=3; sensor flags 0
	 * until the BHI status port. */
	return recorder_state_idx() & 0x1F;
}

void ble_config_batt_debug(void)
{
	/* 0.23.4 raw-register latch test (bench serial 'y', JP 2026-10-28
	 * "always 92%, never another value"). mon_pre = the register as
	 * the last conversion left it; pct = the full production read
	 * (re-triggers a conversion, incl. the charge-pause on VIN);
	 * mon_post = register after it. Across successive 'y' polls:
	 * mon_* moving = conversions live (bands are just coarse);
	 * mon_* frozen forever, especially while charging, = latched. */
	uint8_t faults = 0, mon_pre = 0, mon_post = 0;
	uint8_t fastchg = 0, st = 0;
	int fr = bq_read(BQ_FAULTS, &faults);
	int mpre = bq_read(BQ_BATT_MON, &mon_pre);
	int pct = bq_battery_pct();
	int mpost = bq_read(BQ_BATT_MON, &mon_post);
	bq_read(BQ_FAST_CHG, &fastchg);   /* 0.23.5: bit1=1 charging paused */
	bq_read(BQ_STATUS, &st);

	printk("batt_dbg: fr=%d faults=0x%02x mon_pre=0x%02x(%d) "
	       "pct=%d mon_post=0x%02x(%d) fastchg=0x%02x st=0x%02x "
	       "cache=0x%02x byte=0x%02x\n",
	       fr, faults, mon_pre, mpre, pct, mon_post, mpost,
	       fastchg, st, batt_cache, ble_config_battery_byte());
}

uint8_t ble_config_flash_used_pct(void)
{
	return store_used_pct();
}

uint8_t ble_config_cal(void)
{
	return 0;             /* TODO: BHI calibration-status port */
}

/* ── Boot ─────────────────────────────────────────────────────────── */
void ble_config_start(void)
{
	k_work_init_delayable(&cfg_flush, cfg_flush_handler);

	/* Zephyr nodelabel vs Arduino Wire numbering TRAP (2026-10-10):
	 * the BQ25120A + IS31FL3194 sit on P0.15/P0.16, which is Zephyr
	 * &i2c0 (TWIM0) — the board dts puts the LED driver there and the
	 * LED has worked since M0. Zephyr &i2c1 = P0.22/P0.23 (Arduino
	 * "Wire", the sensor-side bus) — nothing at 0x6A there. Arduino's
	 * Wire1 = Zephyr i2c0; do not trust the names across APIs. */
	bqi2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	if (!device_is_ready(bqi2c)) {
		printk("cfg: i2c1 not ready — battery stays unknown\n");
		bqi2c = NULL;
	}

	cfg_load();

	if (bqi2c) {
		/* 0.23.7 (JP cell 2026-10-29: Renata ICP622540PMT, 550 mAh
		 * Li-ion, NTC third wire): the charger ran at the untouched
		 * factory default 10 mA since day one — "charging" was
		 * geological (550 mAh / 10 mA = 55 h). Renata standard charge
		 * 0.2C = 110 mA: full in ~5 h from empty, ~2 h from half.
		 * FAST_CHG = ICHRG_RANGE 1 (40-300 mA decade) + ICHRG code 7
		 * (40 + 7x10 = 110 mA) => 0x9C. One boot write + read-back. */
		uint8_t fastchg;

		if (bq_read(BQ_FAST_CHG, &fastchg) == 0 && fastchg != 0x9C) {
			if (bq_write(BQ_FAST_CHG, 0x9C) == 0 &&
			    bq_read(BQ_FAST_CHG, &fastchg) == 0) {
				printk("batt: ICHG %u mA (was 10 mA default)\n",
				       40 + ((fastchg >> 2) & 0x1F) * 10);
			} else {
				printk("batt: ICHG write FAILED\n");
			}
		}

		k_work_init_delayable(&batt_work, batt_work_handler);
		k_work_schedule(&batt_work, K_SECONDS(2));
	}
}
