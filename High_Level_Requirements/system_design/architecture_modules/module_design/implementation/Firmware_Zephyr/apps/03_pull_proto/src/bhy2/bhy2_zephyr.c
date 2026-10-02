/*
 * bhy2_zephyr.c — BHI260AP bring-up on Zephyr (M3 step 2, Phase A)
 * ==================================================================
 * The Bosch pure-C API (bhy2.c / bhy2_hif.c / bhy2_parse.c — vendored
 * VERBATIM from the Arduino_BHY2 library the ArduinoFW was built with,
 * .pio/libdeps, version-matched) needs exactly three host functions.
 * This file is the Zephyr implementation of them, plus the boot
 * sequence and the Phase-A proof (IDs on serial).
 *
 * HARDWARE (verified from the Arduino framework's own variant + the
 * library's common.cpp, cross-checked with JP's schematic reading):
 *   Bus   = SPI0-physical = Zephyr &spi2 (SCK P0.03 / MOSI P0.04 /
 *           MISO P0.05) — SHARED with the NOR flash (CS P0.26).
 *   CS    = P0.31 ("CS BHI260", variant pin 17; NOT P0.29 = SPI1's
 *           CS1 header line — JP's schematic read, different signal).
 *   INT   = P0.14 (host interrupt; Phase A does not use it yet).
 *   Clock = 16 MHz (Arduino library's spi.frequency(16000000)).
 *
 * Boot: soft reset → boot status → bhy2_boot_from_flash() — the Nicla's
 * hub runs its firmware from its OWN internal flash. No upload, no blob.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bhy2, LOG_LEVEL_INF);

#include "bhy2.h"
#include "bhy2_hif.h"

/* The vendored bhy2.c is OUR bench-proven copy (cal-hook era): it uses
 * the delegate indirection for FIFO parse callbacks (BHY2_CFG_DELEGATE_
 * FIFO_PARSE_CB_INFO_MGMT=1) — provide the no-op delegate here (the
 * Arduino library registered theirs; we parse FIFOs our own way later). */
void bhy2_get_fifo_parse_callback_info_delegate(uint8_t sensor_id,
                                                struct bhy2_fifo_parse_callback_table *info,
                                                const struct bhy2_dev *dev)
{
	ARG_UNUSED(sensor_id);
	ARG_UNUSED(info);
	ARG_UNUSED(dev);
}

#define MAX_READ_WRITE_LEN 256   /* BoschSensortec.h value */

static struct bhy2_dev bhy2;
static bool bhy2_ready;

/* Devicetree: the BHI node (see app.overlay — second device on &spi2). */
#define BHI_NODE DT_NODELABEL(bhi260)

/* MANUAL CS (v0.11.11): the bosch,bhi260ap compatible has NO binding in
 * Zephyr 4.4 — unbound nodes get no spi-bus resolution, so
 * SPI_CS_GPIOS_DT_SPEC_GET expanded to an empty spec and the SPI driver
 * silently skipped CS control: the hub was NEVER selected. Drive CS by
 * hand, exactly like the Arduino reference (DigitalOut around ops). */
static const struct gpio_dt_spec bhi_cs = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
	.pin = 31,
	.dt_flags = GPIO_ACTIVE_LOW,
};

/* RESET_BHI260 = P0.18 (Arduino variant pin 13, variant.cpp:91).
 * Hardcoded: upstream bosch,bhi260ap binding drops reset-gpios. */
static const struct gpio_dt_spec bhi_rst = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
	.pin = 18,
	.dt_flags = GPIO_ACTIVE_LOW,
};
static const struct device *const spi_dev = DEVICE_DT_GET(DT_BUS(BHI_NODE));
static struct spi_config bhi_spi_cfg = {
	.frequency = 8000000, /* nRF52832 SPIM2 max — mbed's 16M request clamps here anyway */
	.operation = (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |
		      SPI_TRANSFER_MSB),  /* Mode 0: Arduino mbed default */
	/* cs left empty — manual GPIO CS (bhi_cs) around every op */
};

/* ── The three host functions the Bosch API needs ─────────────────── */

int8_t bhy2_zephyr_read(uint8_t reg, uint8_t *buf, uint32_t len,
			void *intf_ptr)
{
	/* v0.11.13: NO null-buffer skip entries (Zephyr buffer-walk
	 * semantics insurance): transceive into ONE scratch buffer covering
	 * address byte + data, then discard byte 0 manually. 8 MHz (16 was
	 * illegal on nRF52832 SPIM2 — killed as a variable). */
	uint8_t scratch[257];
	const struct spi_buf tx = { .buf = &reg, .len = 1 };
	const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
	struct spi_buf rx = { .buf = scratch, .len = len + 1 };
	const struct spi_buf_set rxs = { .buffers = &rx, .count = 1 };

	gpio_pin_set_dt(&bhi_cs, 0); /* select */
	int err = spi_transceive(spi_dev, &bhi_spi_cfg, &txs, &rxs);
	gpio_pin_set_dt(&bhi_cs, 1); /* release */

	if (err == 0) {
		memcpy(buf, scratch + 1, len);
		return BHY2_INTF_RET_SUCCESS;
	}
	return (BHY2_INTF_RET_TYPE)-1;
}

int8_t bhy2_zephyr_write(uint8_t reg, const uint8_t *buf, uint32_t len,
			 void *intf_ptr)
{
	/* Write = address byte then data, back to back. */
	uint8_t hdr = reg;
	struct spi_buf tx[2] = {
		{ .buf = &hdr, .len = 1 },
		{ .buf = (void *)buf, .len = len },
	};
	const struct spi_buf_set txs = { .buffers = tx, .count = 2 };

	gpio_pin_set_dt(&bhi_cs, 0); /* select */
	int err = spi_write(spi_dev, &bhi_spi_cfg, &txs);
	gpio_pin_set_dt(&bhi_cs, 1); /* release */

	return (err == 0) ? BHY2_INTF_RET_SUCCESS : (BHY2_INTF_RET_TYPE)-1;
}

void bhy2_zephyr_delay_us(uint32_t us, void *intf_ptr)
{
	if (us < 1000) {
		k_busy_wait(us);
	} else {
		k_msleep(DIV_ROUND_UP(us, 1000));
	}
}

/* ── Boot + Phase-A proof ─────────────────────────────────────────── */

/* ── Bit-bang probe (v0.11.2) ──────────────────────────────────────
 * SPIM path is config-proven yet the hub answers nothing. This probe
 * takes the peripheral OUT of the loop: SCK/MOSI/CS driven by hand at
 * ~100 kHz, MISO sampled bit by bit. CONTROL GROUP: the MX25R flash
 * (known good) read the same way — JEDEC 0x9F must return C2 28 15;
 * if it does, method + nets are proven and the BHI answers (or
 * silence) become meaningful. Then warm-reboots into the SPIM path. */
static const struct gpio_dt_spec bb_sck  = { DEVICE_DT_GET(DT_NODELABEL(gpio0)),  3, 0 };
static const struct gpio_dt_spec bb_mosi = { DEVICE_DT_GET(DT_NODELABEL(gpio0)),  4, 0 };
static const struct gpio_dt_spec bb_miso = { DEVICE_DT_GET(DT_NODELABEL(gpio0)),  5, 0 };
static const struct gpio_dt_spec bb_cs_f = { DEVICE_DT_GET(DT_NODELABEL(gpio0)), 26, 0 };
static const struct gpio_dt_spec bb_cs_b = { DEVICE_DT_GET(DT_NODELABEL(gpio0)), 31, 0 };
static const struct gpio_dt_spec bb_cs_x = { DEVICE_DT_GET(DT_NODELABEL(gpio0)), 29, 0 };

static void bb_half(void) { k_busy_wait(5); }

/* Once-per-power-cycle sentinel: .noinit survives sys_reboot. */
__attribute__((section(".noinit"))) static uint32_t bb_once;

static void bb_tx8(uint8_t b)
{
	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&bb_mosi, (b >> i) & 1);
		bb_half();
		gpio_pin_set_dt(&bb_sck, 1);
		bb_half();
		gpio_pin_set_dt(&bb_sck, 0);
	}
}

/* Generalized exchange: idle = SCK idle level (0 = mode 0, 1 = mode 3).
 * Data changes while SCK idles; sample on the active (non-idle) edge. */
static void bb_tx8_m(uint8_t b, int idle)
{
	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&bb_mosi, (b >> i) & 1);
		bb_half();
		gpio_pin_set_dt(&bb_sck, !idle);
		bb_half();
		gpio_pin_set_dt(&bb_sck, idle);
	}
}

static uint32_t bb_rx(int bits)
{
	uint32_t v = 0;

	for (int i = 0; i < bits; i++) {
		gpio_pin_set_dt(&bb_sck, 1);
		bb_half();
		v = (v << 1) | (gpio_pin_get_dt(&bb_miso) ? 1 : 0);
		gpio_pin_set_dt(&bb_sck, 0);
		bb_half();
	}
	return v;
}

static uint32_t bb_rx_m(int bits, int idle)
{
	uint32_t v = 0;

	for (int i = 0; i < bits; i++) {
		gpio_pin_set_dt(&bb_sck, !idle);
		bb_half();
		v = (v << 1) | (gpio_pin_get_dt(&bb_miso) ? 1 : 0);
		gpio_pin_set_dt(&bb_sck, idle);
		bb_half();
	}
	return v;
}

static uint32_t bb_exchange_mode(const struct gpio_dt_spec *cs, uint8_t cmd, int rbits, int idle)
{
	gpio_pin_configure_dt(&bb_sck, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_mosi, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_miso, GPIO_INPUT);
	gpio_pin_configure_dt(cs, GPIO_OUTPUT_INACTIVE);
	if (idle) {
		gpio_pin_set_dt(&bb_sck, 1);
	}
	k_busy_wait(50);
	gpio_pin_set_dt(cs, 0); /* assert */
	bb_half(); bb_half();
	bb_tx8_m(cmd, idle);
	uint32_t v = bb_rx_m(rbits, idle);
	gpio_pin_set_dt(cs, 1); /* release */
	bb_half();
	if (idle) {
		gpio_pin_set_dt(&bb_sck, 0);
	}
	return v;
}

static uint32_t bb_exchange(const struct gpio_dt_spec *cs, uint8_t cmd, int rbits)
{
	gpio_pin_configure_dt(&bb_sck, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_mosi, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_miso, GPIO_INPUT);
	gpio_pin_configure_dt(cs, GPIO_OUTPUT_INACTIVE);
	k_busy_wait(50);
	gpio_pin_set_dt(cs, 0); /* assert */
	bb_half(); bb_half();
	bb_tx8(cmd);
	uint32_t v = bb_rx(rbits);
	gpio_pin_set_dt(cs, 1); /* release */
	bb_half();
	return v;
}

/* Wake the MX25R from deep power-down (0xAB + tRES), so the control
 * group is valid — the Zephyr NOR driver parks it in DPD after use. */
static void bb_flash_wake(const struct gpio_dt_spec *cs)
{
	gpio_pin_configure_dt(&bb_sck, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_mosi, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_miso, GPIO_INPUT);
	gpio_pin_configure_dt(cs, GPIO_OUTPUT_INACTIVE);
	k_busy_wait(50);
	gpio_pin_set_dt(cs, 0);
	bb_tx8(0xAB);
	gpio_pin_set_dt(cs, 1);
	k_msleep(1); /* tRES max 35 us — generous margin */
}

/* Detach SPIM2 from the bus: while the peripheral stays connected via
 * PSEL, GPIO writes to the pins are shadowed (nRF52 rule: peripheral
 * owns the pin). Raw register writes — the Zephyr driver re-inits
 * cleanly after the probe's sys_reboot. SPIM2 base 0x40023000:
 * ENABLE 0x500, PSEL.SCK 0x508, PSEL.MOSI 0x50C, PSEL.MISO 0x510. */
static void bb_detach_spim2(void)
{
	volatile uint32_t *spim_enable = (volatile uint32_t *)0x40023500UL;
	volatile uint32_t *psel_sck    = (volatile uint32_t *)0x40023508UL;
	volatile uint32_t *psel_mosi   = (volatile uint32_t *)0x4002350CUL;
	volatile uint32_t *psel_miso   = (volatile uint32_t *)0x40023510UL;

	printk("BB: SPIM2 pre-detach ENABLE=%08lX sck=%08lX mosi=%08lX miso=%08lX\n",
	       (unsigned long)*spim_enable, (unsigned long)*psel_sck,
	       (unsigned long)*psel_mosi, (unsigned long)*psel_miso);
	*spim_enable = 0;
	*psel_sck = 0xFFFFFFFFUL;
	*psel_mosi = 0xFFFFFFFFUL;
	*psel_miso = 0xFFFFFFFFUL;
}

/* BQ25120A PMIC (I2C0/TWIM0 @ 0x6A — same bus as the IS31 LED).
 * nicla::begin() talks to it in the Arduino world; we never have.
 * enable1V8LDO() = write 0xA8 to LDO_CTRL (0x07). If the sensors'
 * supply hangs off this LDO, the hub was unpowered in ALL our Zephyr
 * runs while Arduino's begin() lit it up. */
static void bq_probe(void)
{
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	uint8_t status = 0xAA, ldo = 0xAA, rd = 0xAA;
	uint8_t sta_reg = 0x00, ldo_reg = 0x07;
	uint8_t wr[2];

	if (!device_is_ready(i2c)) {
		printk("BB: i2c0 not ready!\n");
		return;
	}
	int r1 = i2c_write_read(i2c, 0x6A, &sta_reg, 1, &status, 1);
	int r2 = i2c_write_read(i2c, 0x6A, &ldo_reg, 1, &ldo, 1);
	printk("BB: BQ25120 status r=%d v=0x%02X | LDO_CTRL r=%d v=0x%02X\n",
	       r1, status, r2, ldo);

	wr[0] = ldo_reg;
	wr[1] = 0xA8; /* Arduino enable1V8LDO() value */
	int r3 = i2c_write(i2c, wr, 2, 0x6A);
	int r4 = i2c_write_read(i2c, 0x6A, &ldo_reg, 1, &rd, 1);
	printk("BB: LDO_CTRL <- 0xA8 (r=%d) readback r=%d v=0x%02X\n",
	       r3, r4, rd);
	k_msleep(10);
}

static void bhy2_bitbang_probe(void)
{
	printk("BB: gpregret=%02lX (dirty from previous session, why gate failed)\n",
	       (unsigned long)nrf_power_gpregret_get(NRF_POWER, 0));
	printk("BB: bit-bang probe (100 kHz, mode 0)\n");
	bq_probe();
	bb_detach_spim2();

	/* Photocopy the mbed electrical state captured by the Arduino probe2
	 * PIN_CNF dump (hub boots fine under it). Raw PIN_CNF writes after
	 * the SPIM detach. P0.18=0x301 (output HIGH — the mbed core drives
	 * the reset released; our floating probe may have held the hub in
	 * reset through every scan). */
	{
		volatile uint32_t *cnf = (volatile uint32_t *)0x50000700UL;

		cnf[3] = 0x1;    /* SCK     output            */
		cnf[4] = 0x3;    /* MOSI    output+input      */
		cnf[5] = 0x4;    /* MISO    input+pulldown    */
		cnf[26] = 0x3;   /* flash CS output+input     */
		cnf[31] = 0x3;   /* BHI CS  output+input      */
		cnf[18] = 0x301; /* reset   output HIGH +sense*/
		cnf[14] = 0xC;   /* INT     input+pullup      */
		printk("BB: mbed PIN_CNF state applied (incl. P0.18 high)\n");
	}
	k_msleep(5);

	/* MISO idle-level forensics: no CS, no clock. */
	gpio_pin_configure_dt(&bb_sck, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_mosi, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&bb_miso, GPIO_INPUT);
	k_busy_wait(100);
	printk("BB: miso idle level (floating read) = %d\n",
	       gpio_pin_get_dt(&bb_miso));

	bb_flash_wake(&bb_cs_f);
	uint32_t jedec = bb_exchange(&bb_cs_f, 0x9F, 24);
	printk("BB: flash @CS P0.26 JEDEC 0x9F -> %06lX (expect C22815)\n",
	       (unsigned long)jedec);

	/* Full CS scan — every safe pin, both SPI modes. Excluded: bus 3/4/5,
	 * console 9/20, I2C 15/16/22/23, flash CS 26, reset 18. */
	static const uint8_t scan[] = { 0, 1, 2, 6, 7, 8, 10, 11, 12, 13, 14,
				       17, 19, 21, 24, 25, 27, 28, 29, 30, 31 };
	for (int m = 0; m <= 1; m++) {
		printk("BB: --- CS scan mode %d ---\n", m ? 3 : 0);
		for (unsigned i = 0; i < sizeof(scan); i++) {
			const struct gpio_dt_spec cs = {
				.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
				.pin = scan[i],
				.dt_flags = 0,
			};
			uint32_t v = bb_exchange_mode(&cs, 0x90 | 0x80, 16, m);
			if (v != 0) {
				printk("BB: ANSWER pin P0.%u mode %d -> %04lX\n",
				       scan[i], m ? 3 : 0, (unsigned long)v);
			}
		}
		printk("BB: scan mode %d done (nonzero lines above = responders)\n",
		       m ? 3 : 0);
	}
	printk("BB: done, rebooting into SPIM path\n");
	k_msleep(200);
	nrf_power_gpregret_set(NRF_POWER, 0, 0x42); /* skip probe next warm boot */
	bb_once = 0x42BB42BB; /* RAM sentinel — primary gate (GPREGRET proved dirty) */
	sys_reboot(SYS_REBOOT_WARM);
}

/* Bench hook: serial 'B' clears the sentinel and warm-reboots into
 * the bit-bang probe (its boot output is otherwise only visible on
 * the flash-boot, before any console attaches). */
void bhy2_probe_rearm(void)
{
	bb_once = 0;
	sys_reboot(SYS_REBOOT_WARM);
}

bool bhy2_bringup(void)
{
	if (bhy2_ready) {
		return true;
	}

	/* Probe once per power cycle. RAM sentinel in .noinit survives warm
	 * reboot (nRF52 keeps RAM across sys_reboot, v4.02 lesson) and is
	 * ours alone — GPREGRET proved dirty from prior sessions. */
	/* v0.11.12: probe COMPILED OUT — it perturbs the board state
	 * (PMIC LDO writes, 21-pin CS scan) before the SPIM proof ever
	 * runs. Clean-boot test: manual CS + skip-1 framing alone. */
	/* if (bb_once != 0x42BB42BB) {
		bhy2_bitbang_probe();
	} */
	if (!device_is_ready(spi_dev)) {
		printk("bhy2: SPI device not ready\n");
		return false;
	}

	/* Manual CS: configure P0.31 output HIGH (deselected) up front. */
	gpio_pin_configure_dt(&bhi_cs, GPIO_OUTPUT_INACTIVE);

	/* mbed parity: P0.18 = output HIGH, never pulsed (the mbed core
	 * drives the reset released; our old 10 ms low pulse may itself
	 * have been wedging the boot sequence). */
	if (bhi_rst.port != NULL) {
		gpio_pin_configure_dt(&bhi_rst, GPIO_OUTPUT_INACTIVE); /* high */
		k_msleep(150);
		printk("bhy2: P0.18 held high (no pulse, mbed parity)\n");
	}

	int8_t ret = bhy2_init(BHY2_SPI_INTERFACE,
			       bhy2_zephyr_read, bhy2_zephyr_write,
			       bhy2_zephyr_delay_us,
			       MAX_READ_WRITE_LEN, NULL, &bhy2);

	if (ret != BHY2_OK) {
		printk("bhy2: init -> %d\n", ret);
		return false;
	}

	/* Chip/product/ROM identity — the Phase-A gate. */
	uint16_t prod = 0;

	ret = bhy2_get_product_id((uint8_t *)&prod, &bhy2);
	printk("bhy2: product_id -> %d, id %u\n", ret, prod);

	uint16_t rom = 0;

	ret = bhy2_get_rom_version(&rom, &bhy2);
	printk("bhy2: rom_version -> %d, v%u\n", ret, rom);

	ret = bhy2_soft_reset(&bhy2);
	printk("bhy2: soft_reset -> %d\n", ret);
	k_msleep(300);

	uint8_t boot = 0;

	ret = bhy2_get_boot_status(&boot, &bhy2);
	printk("bhy2: boot_status -> %d, 0x%02x\n", ret, boot);

	ret = bhy2_boot_from_flash(&bhy2);
	printk("bhy2: boot_from_flash -> %d\n", ret);
	k_msleep(300);

	ret = bhy2_get_boot_status(&boot, &bhy2);
	printk("bhy2: boot_status after -> %d, 0x%02x\n", ret, boot);

	bhy2_ready = (ret == BHY2_OK);
	return bhy2_ready;
}
