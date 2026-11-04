# BHI260AP Zephyr Bring-Up — Case Study (M3 Phase A)

**Status: RESOLVED — hub boots from flash on Zephyr (v0.11.14, 2026-10-02).**
Companion ledger case: TC-2026-10-02-001. Proof:

```
bhy2: product_id -> 0, id 137        (0x89 = BHI260AP)
bhy2: rom_version -> 0, v5166
bhy2: soft_reset -> 0
bhy2: boot_status -> 0, 0x11         (pre-boot; matches Arduino)
bhy2: boot_from_flash -> 0
bhy2: boot_status after -> 0, 0x13   (hub RUNNING)
```

## 1. Hardware truth (five-source verified)

| Signal | nRF pin | Sources |
|---|---|---|
| SCK | P0.03 | schematic SCLK0 (pad 19), mbed PinNames `SPI_PSELSCK0=p3`, Zephyr board spi2 pinctrl |
| MOSI | P0.04 | schematic COPI0 (pad 24), `SPI_PSELMOSI0=p4`, board spi2 |
| MISO | P0.05 | schematic CIPO0 (pad 23), `SPI_PSELMISO0=p5`, board spi2 |
| BHI CS | P0.31 | variant.cpp pin 17 "CS BHI260", `SPI_PSELSS0=p31` (mbed DigitalOut), schematic CS0 (pad 25) |
| BHI RESET | P0.18 | variant.cpp pin 13 "Reset BHI260" — **mbed core drives it output-HIGH at init (PIN_CNF 0x301); no library code touches it** |
| BHI INT | P0.14 | variant.cpp pin 14 (unused Phase A) |
| Flash CS | P0.26 | shared bus — flash + BHI on the same SPI0-physical (= Zephyr `&spi2`) |

- **mbed's BHI SPI runs on SPIM2 (0x40023000), psel 3/4/5** — the SAME
  peripheral instance Zephyr uses. (0x40003000 + P0.15/16 = ESLOV
  SPI-slave to the SAMD11, a red herring.)
- Bus naming trap: Arduino "SPI0" object = physical SPI1 nets (ESLOV
  header P0.11/27/28/29); Arduino "SPI1" = physical SPI0 (BHI+flash).
  Same inversion for I2C0/I2C1. The schematic's physical names are truth.
- Power: +1V8 from the BQ25120 LS/LDO feeds nRF module, flash, LED
  driver AND sensors alike (power was never the issue).
- **Battery wires are SOLDERED to the Nicla** (no connector) — a true
  board power-cycle requires desoldering; nRF reboots never cold-boot
  the hub (VDD persists). Keep this in mind for POR-dependent straps.

## 2. Root causes (two stacked CS failures + phantom evidence)

1. **CS never asserted (primary).** Our overlay used
   `compatible = "bosch,bhi260ap"` — **no such binding exists in Zephyr
   4.4** (no driver, no yaml; Golioth's 2025 blog confirms nobody did
   these sensors in-tree). An unbound node gets no spi-bus resolution in
   `devicetree_generated.h`, so `SPI_CS_GPIOS_DT_SPEC_GET(node)`
   expanded to an empty spec and the SPI driver **silently skipped CS
   control**. The flash worked because `jedec,spi-nor` is properly bound.
2. **Manual-CS polarity inversion (secondary, my own).** With
   `gpio_dt_spec{dt_flags = GPIO_ACTIVE_LOW}`,
   `gpio_pin_set_dt(cs, 0)` = logical-inactive = **physical HIGH**.
   The chip was selected only *between* transfers. Fix: `dt_flags = 0`,
   set(0)=LOW=assert, set(1)=HIGH=release.
3. **Phantom evidence:** register 0x90 (bhy2 product-id reg) **reads
   0x00 even through the working mbed stack on a running hub**. Our
   "dead hub" signal (product_id 0) was noise; pin-scans keyed on 0x90
   nonzero answers were blind. Real health metric = **boot_status
   (0x11 pre-boot / 0x13 running)**. Also: `bhy2_soft_reset` times out
   routinely — the Arduino reference ignores its return value.

## 3. The working configuration (v0.11.14, apps/03_pull_proto)

- Manual GPIO CS (P0.31, plain polarity) around every op — never trust
  DT CS for unbound compatibles.
- Read framing: single scratch buffer covering address+data, discard
  byte 0 manually (no NULL skip entries in the rx set).
- 8 MHz (16 MHz is illegal on nRF52832 SPIM2; mbed's 16M request
  clamps anyway). Mode 0, MSB-first.
- P0.18 held output-HIGH from bring-up, never pulsed.
- Vendored bhy2 API (from Arduino_BHY2, our cal-hook-era copy) +
  `bhy2_init → soft_reset (ignore rc) → boot_from_flash → proof`.

## 4. Debugging arsenal (reusable)

- **Arduino probe sketches**: `workspace/bench_bhy_test/` (PlatformIO
  on the VPS; vendored Arduino_BHY2 + ArduinoBLE copied from the SGC
  libdeps). probe4 = the decisive bisection (library read vs bit-bang
  on a live hub).
- **Flashing Arduino images via OpenOCD**: app hexes are app-only
  (linked above the bootloader) — **flash
  `~/.platformio/packages/framework-arduino-mbed/bootloaders/NICLA/bootloader.hex`
  at 0x0 FIRST, then the app hex**, else double-fault lockup.
- **PIN_CNF dump** from the Arduino side reveals the mbed core's pin
  states (P0.18=0x301 etc.).
- **Bit-banging after SPIM detach**: nRF peripheral PSEL shadows GPIO
  — detach (ENABLE=0, PSEL=0xFFFFFFFF) before bit-banging; reboot to
  restore.
- **PMIC** BQ25120 at 0x6A on Zephyr `&i2c0` (TWIM0, P0.15/16 — same
  bus as the IS31 LED).

## 5. Post-mortem: what the debugging cost taught

- The "hub silent" diagnosis was built on a register that legitimately
  reads zero. Always calibrate your health-check register against the
  known-good stack BEFORE trusting it.
- The control group (flash JEDEC via bit-bang) proved the wires but
  couldn't prove the CS path — a shared-bus control doesn't test
  per-device select lines.
- Zephyr devicetree silently drops what bindings don't declare — both
  `reset-gpios` (compile error when referenced) and, far worse, the
  spi-device bus resolution (silent at compile time).
