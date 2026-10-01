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

/* RESET_BHI260 = P0.18 (Arduino variant pin 13, variant.cpp:91).
 * Hardcoded: upstream bosch,bhi260ap binding drops reset-gpios. */
static const struct gpio_dt_spec bhi_rst = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
	.pin = 18,
	.dt_flags = GPIO_ACTIVE_LOW,
};
static const struct device *const spi_dev = DEVICE_DT_GET(DT_BUS(BHI_NODE));
static const struct spi_config bhi_spi_cfg = {
	.frequency = 16000000,
	.operation = (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |
		      SPI_TRANSFER_MSB),  /* Mode 0: Arduino mbed default */
	.cs = {
		.gpio = SPI_CS_GPIOS_DT_SPEC_GET(BHI_NODE),
		.delay = 1,
	},
};
static struct spi_config bhi_spi_cfg_rw = {
	.frequency = 16000000,
	.operation = (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |
		      SPI_TRANSFER_MSB),
};

/* ── The three host functions the Bosch API needs ─────────────────── */

int8_t bhy2_zephyr_read(uint8_t reg, uint8_t *buf, uint32_t len,
			void *intf_ptr)
{
	/* Read = write the register address, then read len bytes. */
	const struct spi_buf tx = { .buf = &reg, .len = 1 };
	const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
	struct spi_buf rx[2] = {
		{ .buf = NULL, .len = 1 },          /* discard reg echo */
		{ .buf = buf, .len = len },
	};
	const struct spi_buf_set rxs = { .buffers = rx, .count = 2 };

	int err = spi_transceive(spi_dev, &bhi_spi_cfg, &txs, &rxs);

	return (err == 0) ? BHY2_INTF_RET_SUCCESS : (BHY2_INTF_RET_TYPE)-1;
}

int8_t bhy2_zephyr_write(uint8_t reg, const uint8_t *buf, uint32_t len,
			 void *intf_ptr)
{
	/* Write = address |0x80? No — the Bosch HIF uses separate read/
	 * write register access by transfer order; write = reg then data. */
	uint8_t hdr = reg;
	struct spi_buf tx[2] = {
		{ .buf = &hdr, .len = 1 },
		{ .buf = (void *)buf, .len = len },
	};
	const struct spi_buf_set txs = { .buffers = tx, .count = 2 };

	int err = spi_write(spi_dev, &bhi_spi_cfg, &txs);

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

bool bhy2_bringup(void)
{
	if (bhy2_ready) {
		return true;
	}
	if (!device_is_ready(spi_dev)) {
		printk("bhy2: SPI device not ready\n");
		return false;
	}

	/* Hard-reset pulse on RESET_BHI260 (P0.18): the battery keeps VDD
	 * alive across nRF reboots, so a wedged hub NEVER sees a cold boot
	 * unless we pulse its reset line. Low 10 ms, release, boot settle. */
	if (bhi_rst.port != NULL) {
		gpio_pin_configure_dt(&bhi_rst, GPIO_OUTPUT_ACTIVE); /* low */
		k_msleep(10);
		gpio_pin_set_dt(&bhi_rst, 0); /* high = released */
		k_msleep(150);
		printk("bhy2: reset pulse done (P0.18)\n");
	} else {
		printk("bhy2: no reset-gpios in DT!\n");
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
