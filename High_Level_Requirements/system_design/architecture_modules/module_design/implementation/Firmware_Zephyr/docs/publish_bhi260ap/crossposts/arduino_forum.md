# Arduino Forum post (paste as new topic)

**Board/category:** Arduino Boards / Nicla

**Title:** Nicla Sense ME: BHI260AP dead on Zephyr? Read this before blaming the hardware (silent CS trap)

---

If you're porting the Nicla Sense ME to Zephyr (NCS) and the BHI260AP answers nothing over SPI — `product_id` 0, soft-reset timeouts — while the flash on the same bus works and everything works under Arduino sketches: the hardware is fine. Two silent software traps:

1. **`compatible = "bosch,bhi260ap"` has no binding in Zephyr.** Unbound devicetree nodes lose their SPI-bus resolution, so `SPI_CS_GPIOS_DT_SPEC_GET()` comes back empty and the SPI driver silently never toggles chip-select. The hub is simply never addressed.
2. If you work around it with a manual GPIO CS, watch the polarity: `GPIO_ACTIVE_LOW` + `gpio_pin_set_dt(&cs, 0)` = physical **HIGH** (logical inactive). Plain polarity, drive 0/1 directly.

And a calibration note that cost us hours: BHY2 register `0x90` reads `0x00` **even on a healthy running hub** — we verified it by calling the library's own `bhy2_spi_read(0x90)` through the working Arduino stack on a booted hub. Use `boot_status` (`0x11` → `0x13`) as your aliveness metric. `bhy2_soft_reset` returning a timeout is normal — the Arduino reference ignores it.

**Pin truth for the port** (schematic + variant.cpp + mbed PinNames all agree): SCK P0.03 / MOSI P0.04 / MISO P0.05 (Zephyr `&spi2`, shared with the MX25R flash, its CS = P0.26), **BHI CS = P0.31**, BHI RESET = P0.18 (the mbed core drives it output-high at init — hold it high), BHI INT = P0.14. Max 8 MHz on SPIM2 (nRF52832).

Bonus if you flash Arduino hexes via OpenOCD: app hexes link above the bootloader — flash `bootloaders/NICLA/bootloader.hex` to 0x0 first or you get a double-fault lockup.

Full case study with the debugging log (Arduino-side bisection sketches included — they boot the hub via mbed, then bit-bang the same bus to isolate the fault):
https://github.com/skixer2/bhi260ap-zephyr-bringup

— Jean Paul Voyat (investigation executed with OpenClaw running GLM 5.3, Z.ai)
