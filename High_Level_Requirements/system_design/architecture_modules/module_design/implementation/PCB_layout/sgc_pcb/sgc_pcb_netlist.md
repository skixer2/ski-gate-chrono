# SGC — Wiring Netlist (v3.0 — Nicla Sense ME Replica)

*2026-06-16 — v3.0: Complete redesign. Custom PCB = strict Nicla Sense ME replica.
All Nicla internal connections replicated exactly. SGC peripherals added using only
free ANNA-B112 GPIOs. BHI260AP/BMP390 on BHY2-managed SPI (P0.03–P0.05).
Flash = Nicla's MX25R1635F U7 (CS P0.26). No external flash IC.*

*Reference: config.h v2.0, sgc_architecture_hardware.md v2.0, ANNA-B112 datasheet Table 7.*

---

## Sheet 1: Nicla Replica — MCU + Core Peripherals (ANNA-B112 Module)

The ANNA-B112 module (MD1) contains the nRF52832 + BLE antenna. This is the
same module used on the stock Nicla Sense ME. All pin connections match exactly.

| Module Pin | nRF52 GPIO | Net Name | Connect To |
|-----------|-----------|----------|------------|
| 9 | — | VCC | 3.3V rail |
| 17 | P0.00 | XL1 | 32.768 kHz crystal (Y1 pin1), C1(12pF→GND) |
| 18 | P0.01 | XL2 | 32.768 kHz crystal (Y1 pin2), C2(12pF→GND) |
| 12 | P0.21 | RESET_N | R6(10k→3.3V pull-up), BQ25120 MR (reset network) |
| 16 | P0.18 | SWO/RESET | System reset network (BQ25120 MR) |
| 19 | P0.03 | BHI_SPI_SCK | BHI260AP (U5) + Flash U7 — BHY2 managed |
| 24 | P0.04 | BHI_SPI_MOSI | BHI260AP (U5) + Flash U7 — BHY2 managed |
| 23 | P0.05 | BHI_SPI_MISO | BHI260AP (U5) + Flash U7 — BHY2 managed |
| 25 | P0.31 | BHI_CS | BHI260AP CS (U5 pin) |
| 31 | P0.26 | FLASH_CS | Flash U7 /CS (MX25R1635F, Nicla stock) |
| 13 | P0.14 | BHI_INT | BHI260AP HIRQ/INT |
| 14 | P0.15 | I2C0_SDA | IS31FL3194 (U8) SDA, BQ25120 (U9) SDA, R7(2.2k→3.3V) |
| 15 | P0.16 | I2C0_SCL | IS31FL3194 (U8) SCL, BQ25120 (U9) SCL, R8(2.2k→3.3V) |
| 30 | P0.25 | CHG_DIS | BQ25120 CD |
| — | P0.06 | NC | **Not broken out from ANNA-B112 module** |
| — | P0.07 | NC | **Not broken out from ANNA-B112 module** |
| — | P0.08 | NC | **Not broken out from ANNA-B112 module** |
| — | P0.12 | NC | **Not broken out from ANNA-B112 module** |
| — | P0.13 | NC | **Not broken out from ANNA-B112 module** |
| — | P0.17 | NC | **Not broken out from ANNA-B112 module** |
| 4,7-8,10-11,32-33,41-44,46-48,49-52 | — | GND | Solid GND plane |

## Sheet 2: Nicla Replica — External Headers (available as GPIO on custom PCB)

These pins are exposed on the Nicla's J1/J2 headers. On the custom PCB, they're
available for SGC use (routed internally, not broken out to headers).

| Module Pin | nRF52 GPIO | Nicla Label | SGC Assignment |
|-----------|-----------|-------------|----------------|
| 20 | P0.02 | A0 | **LDC_INTB** — LDC1612 INTB (active LOW), R9(100k→3.3V pull-up) |
| 21 | P0.09 | RX/LPIO2 | **BEEPER** — Piezo transducer via R12(100Ω) |
| 22 | P0.10 | LPIO3 | **QI_DETECT** — Qi charger detect, R10(10k→3.3V pull-up) |
| 35 | P0.19 | ESLOV INT | **LED_DIN** — SK6812 #1 DIN (first in chain of 5) |
| 45 | P0.20 | TX/LPIO1 | **RFID_CS** (v2) — RFID /CS, R13(10k→3.3V) |
| 38 | P0.24 | LPIO0 | **RFID_EN** (v2) — MOSFET Q1 gate. LDO EN tied HIGH on PCB. |
| 27 | P0.29 | CS | **UWB_CS** (v2) — DW3000 /CS, R14(10k→3.3V) |
| 26 | P0.30 | A1 | **UWB_PWR** (v2) — MOSFET Q2 gate |

## Sheet 3: SGC SPI Bus (P0.11/P0.27/P0.28 — Arduino SPI Object)

The Nicla's external SPI header pins are routed internally on the custom PCB
for v2 devices. Managed by spi_bus.cpp via Arduino `SPI`.

| Module Pin | nRF52 GPIO | Net Name | Connect To |
|-----------|-----------|----------|------------|
| 34 | P0.11 | SGC_SPI_SCK | RFID_SCK(v2), UWB_SCK(v2) |
| 29 | P0.27 | SGC_SPI_MOSI | RFID_MOSI(v2), UWB_MOSI(v2) |
| 28 | P0.28 | SGC_SPI_MISO | RFID_MISO(v2), UWB_MISO(v2) |

## Sheet 4: Nicla Replica — I2C Buses

### I2C1 (P0.22/P0.23) — Wire (external) + SGC LDC1612

| Module Pin | nRF52 GPIO | Net Name | Connect To |
|-----------|-----------|----------|------------|
| 36 | P0.22 | I2C1_SDA | LDC1612 SDA, R15(2.2k→3.3V) |
| 37 | P0.23 | I2C1_SCL | LDC1612 SCL, R16(2.2k→3.3V) |

### I2C0 (P0.15/P0.16) — Wire1 (sensors + RGB LED + charger)

Nicla stock — no SGC devices on this bus. See Sheet 1 for connections.

## Sheet 5: SGC Sensors

### U1 (LDC1612DNTR — Inductive Proximity Sensor)

| Pin | Net Name | Connect To |
|-----|----------|------------|
| VDD | 1.8V | LDO 1.8V output, C17(100n→GND) |
| GND | GND | GND plane |
| SDA | I2C1_SDA | P0.22, R15 |
| SCL | I2C1_SCL | P0.23, R16 |
| ADDR | GND | I²C address 0x2A |
| SD | GND | Shutdown inactive |
| INTB | LDC_INTB | P0.02, R9(100k→3.3V) |
| IN0A | COIL_A | L1 pin1, C18(33pF→IN0B) |
| IN0B | COIL_B | L1 pin2, C18(33pF→IN0A) |

### L1 (PCB Coil — 14mm, 2-layer spiral)

Custom PCB trace inductor. Connected to LDC1612 IN0A/IN0B.

## Sheet 6: SGC Peripherals

### D1–D5 (SK6812-mini LED Chain, 5×)

| LED | DIN | DOUT |
|-----|-----|------|
| D1 | P0.19 (LED_DIN) | D2 DIN |
| D2 | D1 DOUT | D3 DIN |
| D3 | D2 DOUT | D4 DIN |
| D4 | D3 DOUT | D5 DIN |
| D5 | D4 DOUT | NC (end of chain) |

All: VDD → 3.3V | GND → GND. C19(100n→GND) per LED.

### BZ1 (Piezo Transducer, 10×10mm)

| Pin | Connect To |
|-----|------------|
| \+ | P0.09 via R12(100Ω) |
| − | GND |

### QI_DET (Qi Charger Presence)

| Net | Connect To |
|-----|------------|
| QI_DETECT | P0.10, R10(10k→3.3V) |
| Qi receiver 5V output | QI_DETECT (pulls LOW when active) |

---

## Sheet 7: v2 Peripherals (⚠️ UNPOPULATED — Footprint Only)

### U2 (RFID Module — Impinj E310-based)

| Pin | Net Name | Connect To |
|-----|----------|------------|
| VDD | VDD_RFID | Q1 drain |
| GND | GND | GND plane |
| SCK | SGC_SPI_SCK | P0.11 |
| MOSI | SGC_SPI_MOSI | P0.27 |
| MISO | SGC_SPI_MISO | P0.28 |
| /CS | RFID_CS | P0.20, R13(10k→3.3V) |
| EN | VDD_RFID | Q1 drain (power rail) |

### Q1 (P-MOSFET — RFID Power Gate)

| Pin | Connect To |
|-----|------------|
| Gate | P0.24 (RFID_EN) via R17(1k) |
| Source | 3.3V |
| Drain | VDD_RFID rail |

### U3 (UWB Module — Qorvo DW3000)

| Pin | Net Name | Connect To |
|-----|----------|------------|
| VDD | VDD_UWB | Q2 drain |
| GND | GND | GND plane |
| SCK | SGC_SPI_SCK | P0.11 |
| MOSI | SGC_SPI_MOSI | P0.27 |
| MISO | SGC_SPI_MISO | P0.28 |
| /CS | UWB_CS | P0.29, R14(10k→3.3V) |

### Q2 (P-MOSFET — UWB Power Gate)

| Pin | Connect To |
|-----|------------|
| Gate | P0.30 (UWB_PWR) via R18(1k) |
| Source | 3.3V |
| Drain | VDD_UWB rail |

---

## Sheet 8: Power

Nicla stock power tree — identical to Nicla Sense ME:
- BQ25120A charger (U9) on I2C0 (P0.15/P0.16)
- Battery: JST 3-pin connector (VBAT, NTC, GND)
- Qi coil: 5W receiver → rectifier → 5V → BQ25120 input
- 3.3V LDO: VDD_nRF, SGC peripherals
- 1.8V LDO: VDD_Sensors
- LDO EN (P0.24): Tied HIGH via R19(10k→3.3V) — VDDIO_EXT always on
- v2 MOSFET gates: P0.24 (RFID_EN), P0.30 (UWB_PWR) — GPIO output, default LOW

---

## Component Reference

| Ref | Component | Footprint | Notes |
|-----|-----------|-----------|-------|
| MD1 | ANNA-B112 | Module, 52-pin LGA | nRF52832 + BLE |
| U1 | LDC1612DNTR | WSON-12 | SGC addition |
| U2 | RFID (Impinj E310) | (v2, unpopulated) | v2 only |
| U3 | DW3000 | QFN-40 | v2 only |
| U5 | BHI260AP | LGA-44 | Nicla stock |
| U7 | MX25R1635F | SOIC-8 / USON-8 | Nicla stock (2 MB Flash) |
| U8 | IS31FL3194 | QFN-16 | Nicla stock (RGB LED driver) |
| U9 | BQ25120A | DSBGA-25 | Nicla stock (charger) |
| D1–D5 | SK6812-mini | 2×2mm | SGC addition |
| BZ1 | Piezo transducer | 10×10mm | SGC addition |
| Q1, Q2 | P-MOSFET | SOT-23 | v2 power gates |
| L1 | LDC coil | PCB trace, 14mm | SGC addition |
| Y1 | 32.768 kHz crystal | 3.2×1.5mm | Nicla stock |
