# BHI260AP on Zephyr: the silent CS trap (and a working bring-up recipe)

**Arduino Nicla Sense ME + Zephyr 4.4 (NCS 3.4.1) + Bosch BHI260AP.**

If your BHY2 stack reads **all-zeros over SPI** (`product_id` 0, `soft_reset`
timeouts) while another device on the **same bus** (the MX25R flash) works
fine — and an Arduino image on the same board boots the hub instantly —
you are almost certainly hitting the two bugs below. No hardware fault.
No wiring fault. No power issue.

## TL;DR — the two bugs

1. **`compatible = "bosch,bhi260ap"` has no binding in Zephyr** (no in-tree
   driver/yaml as of 4.4). An *unbound* node gets no SPI-bus resolution in
   `devicetree_generated.h`, so `SPI_CS_GPIOS_DT_SPEC_GET(node)` expands to
   an **empty spec** and the SPI driver **silently skips chip-select**.
   There is no build-time warning. Your device is never selected.
2. If you then switch to a **manual GPIO CS**: with
   `struct gpio_dt_spec { .dt_flags = GPIO_ACTIVE_LOW }`,
   `gpio_pin_set_dt(&cs, 0)` is *logical inactive* = **physical HIGH**.
   Your "assert" releases the chip; your "release" selects it.

## Phantom evidence (don't be fooled like we were)

- BHY2 **register 0x90 reads `0x00` even on a healthy, running hub** — we
  verified this by calling the vendored `bhy2_spi_read(0x90)` through the
  working mbed stack on a booted hub. Do not use it as an aliveness probe.
- The real health metric is **boot_status**: `0x11` (flash detected,
  ready) → `0x13` (firmware running).
- `bhy2_soft_reset` timing out is routine — the official Arduino reference
  **ignores its return value**. So will you.

## Working recipe (nRF52832, vendored Bosch bhy2 C API)

```c
static const struct gpio_dt_spec bhi_cs = {
        .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
        .pin  = 31,          /* Nicla: CS BHI260 (variant pin 17) */
        .dt_flags = 0,       /* PLAIN polarity: set(0)=LOW=assert */
};

/* read: single scratch buffer covering address+data, discard byte 0 */
uint8_t scratch[257];
const struct spi_buf tx = { .buf = &reg, .len = 1 };
struct spi_buf rx = { .buf = scratch, .len = len + 1 };
gpio_pin_set_dt(&bhi_cs, 0);
spi_transceive(spi_dev, &cfg, &txs, &rxs);
gpio_pin_set_dt(&bhi_cs, 1);
memcpy(buf, scratch + 1, len);
```

- 8 MHz, mode 0, MSB first (16 MHz is illegal on nRF52832 SPIM2 — the
  Arduino world's 16 MHz request clamps anyway).
- Reset pin (P0.18) held output-HIGH from init, never pulsed — that is
  exactly what the mbed core does (its PIN_CNF reads `0x301`); no Arduino
  library code touches it.
- Sequence: `bhy2_init(BHY2_SPI_INTERFACE, …)` → `bhy2_soft_reset` (ignore
  rc) → `bhy2_boot_from_flash` → read boot_status (`0x11` → `0x13`) →
  configure virtual sensors.

## Nicla Sense ME pin truth (five-source verified)

| Signal | nRF pin | Notes |
|---|---|---|
| SCK / MOSI / MISO | P0.03 / P0.04 / P0.05 | Zephyr `&spi2`, shared with the MX25R flash |
| BHI CS | **P0.31** | variant.cpp pin 17, mbed `SPI_PSELSS0` |
| BHI RESET | P0.18 | variant.cpp pin 13 |
| BHI INT | P0.14 | variant.cpp pin 14 |
| Flash CS | P0.26 | same bus |

Bus-naming trap: Arduino's "SPI0" object = physical SPI1 nets (ESLOV
header P0.11/27/28/29); Arduino's "SPI1" = physical SPI0 (BHI + flash).
The schematic's physical names are the truth. The mbed stack drives the
hub via **SPIM2 (0x40023000), psel 3/4/5** — the same instance Zephyr
uses for `&spi2`.

## Bonus for OpenOCD flashers

PIO/Arduino app hexes are **app-only** (linked above the bootloader).
Flashing them without the bootloader at 0x0 gives a double-fault lockup
after `reset run`. Flash
`…/framework-arduino-mbed/bootloaders/NICLA/bootloader.hex` **first**,
then the app hex.

## Full story

See [`CASE_STUDY.md`](CASE_STUDY.md) for the complete debugging log: the
Arduino-side bisection sketches (library read vs bit-bang on a live hub),
the PIN_CNF dump technique, the SPIM-detach trick for bit-banging shared
buses, and the post-mortem (why a shared-bus control group can't test
per-device select lines).

---

*Author: **Jean Paul Voyat** — investigation executed with
[OpenClaw](https://openclaw.ai) running **GLM 5.3** (Z.ai), on the
bench of the [ski-gate-chrono](https://github.com/jean-paul-voyat) project.
License: CC-BY-4.0 (text), MIT (code snippets).*
