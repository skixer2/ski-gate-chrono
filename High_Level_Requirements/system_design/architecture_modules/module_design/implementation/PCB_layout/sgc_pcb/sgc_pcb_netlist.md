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

All: VDD → 5V_BOOST | GND → GND. C19(100n→GND) per LED.

**⚠️ P0 Prototype (Monolithic PCB):** SK6812 VDD = 3.3V (dim, but functional for field validation).
From P1 onward (companion carrier PCB): VDD = 5V_BOOST via MT3608 boost converter.

**Data path:** nRF52 P0.19 (3.3V NZR) → 74AHCT1G125 level shifter → SK6812 D1 DIN (5V NZR).
Level shifter VCC = 5V_BOOST, input from 3.3V GPIO, output to SK6812 data line.

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
- 3.3V LDO: VDD_nRF, SGC peripherals (except SK6812 LEDs)
- 1.8V LDO: VDD_Sensors
- LDO EN: Tied HIGH via R19(10k→3.3V) — VDDIO_EXT always on
- v2 MOSFET gates: P0.24 (RFID_EN), P0.30 (UWB_PWR) — GPIO output, default LOW

### 5V Boost Converter (MT3608 — SGC Addition)

The battery rail (VBAT, 3.0–4.2 V) is boosted to 5.1 V to power the SK6812 LED
strip and level shifter. The boost is software-gated via EN pin for power saving.

| MT3608 Pin | Net Name | Connect To |
|-----------|----------|------------|
| 1 (SW) | BOOST_SW | L_Boost → VBAT |
| 2 (GND) | GND | Solid GND plane, via-stitched |
| 3 (FB) | BOOST_FB | R_FB1 (75k) → 5V_BOOST, R_FB2 (10k) → GND |
| 4 (EN) | BOOST_EN | nRF52 GPIO (P0.24 on v1), HIGH = active |
| 5 (VIN) | VBAT | Battery rail, C_IN (10µF) to GND |
| 6 (NC) | — | No connect (MT3608 pin 6 is unused) |

| External | Value | Connect To |
|----------|-------|------------|
| L_Boost | 4.7 µH, Isat ≥ 2A | VBAT ↔ MT3608 SW (pin 1) |
| C_IN | 10 µF, 10V, X5R | VBAT ↔ GND, < 3 mm from MT3608 VIN |
| C_OUT | 22 µF, 10V, X5R | 5V_BOOST ↔ GND, < 3 mm from L_Boost output |
| R_FB1 | 75 kΩ, 1% | 5V_BOOST ↔ MT3608 FB (pin 3) |
| R_FB2 | 10 kΩ, 1% | MT3608 FB (pin 3) ↔ GND |

**⚠️ GPIO conflict (v2):** In v1, P0.24 drives BOOST_EN. In v2, P0.24 is reassigned
to RFID_EN → BOOST_EN must move to another free GPIO (recommended: P0.20 or P0.29
when v2 peripherals not populated).

### Qi Receiver (IP6833 — Production / BQ51013B Module — Prototype)

The Qi receiver converts magnetic coupling from a Qi transmitter pad into
regulated 5 V DC to feed the BQ25120A charger input.

**P0 Prototype:** Pre-built BQ51013B module (Adafruit #1901 or equivalent).
Two-wire connection: V+ (5V) → BQ25120A IN, GND → common ground.

**P1+ Production:** IP6833 QFN-28, integrated on companion carrier PCB.

#### IP6833 Pinout

| IP6833 Pin | Net Name | Connect To |
|-----------|----------|------------|
| AC1 | QI_AC1 | L_QI terminal 1, C_AC1 (22 nF NP0) → GND |
| AC2 | QI_AC2 | L_QI terminal 2, C_AC2 (22 nF NP0) → GND |
| RECT | QI_RECT | C_RECT (22 µF) → GND |
| BOOT | QI_BOOT | C_BOOT (100 nF) → RECT |
| VOUT | V_QI_5V | C_OUT (10 µF) → GND, → BQ25120A IN pin |
| VDD | QI_VDD | C_VDD (1 µF) → GND |
| GND | GND | Solid GND plane, exposed pad soldered |
| (other) | NC or per datasheet | Consult IP6833 datasheet for full pinout |

#### External Components

| Ref | Value | Package | Purpose |
|-----|-------|---------|---------|
| L_QI | 24 µH WPC A11 | Wound coil 48×32mm | Qi receiver coil |
| C_AC1 | 22 nF, 50V, NP0/C0G | 0603 | AC1 resonant cap |
| C_AC2 | 22 nF, 50V, NP0/C0G | 0603 | AC2 resonant cap |
| C_RECT | 22 µF, 16V, X5R | 0805 | Rectifier filter |
| C_BOOT | 100 nF, 16V, X5R | 0603 | Bootstrap cap |
| C_OUT | 10 µF, 10V, X5R | 0805 | Output decoupling |
| C_VDD | 1 µF, 10V, X5R | 0603 | Internal VDD |

---

## Component Reference

| Ref | Component | Footprint | Notes |
|-----|-----------|-----------|-------|
| MD1 | ANNA-B112 | Module, 52-pin LGA | nRF52832 + BLE |
| U1 | LDC1612DNTR | WSON-12 | SGC addition |
| U2 | RFID (Impinj E310) | (v2, unpopulated) | v2 only |
| U3 | DW3000 | QFN-40 | v2 only |
| U4 | MT3608 | SOT-23-6 | SGC addition — 5V boost for SK6812 LEDs |
| U_QI | IP6833 (P1+) / BQ51013B module (P0) | QFN-28 / Module | SGC addition — Qi wireless receiver |
| U5 | BHI260AP | LGA-44 | Nicla stock |
| U7 | MX25R1635F | SOIC-8 / USON-8 | Nicla stock (2 MB Flash) |
| U8 | IS31FL3194 | QFN-16 | Nicla stock (RGB LED driver) |
| U9 | BQ25120A | DSBGA-25 | Nicla stock (charger) |
| D1–D5 | SK6812-mini | 2×2mm | SGC addition |
| BZ1 | Piezo transducer | 10×10mm | SGC addition |
| Q1, Q2 | P-MOSFET | SOT-23 | v2 power gates |
| L1 | LDC coil | PCB trace, 14mm | SGC addition |
| L_Boost | Power inductor 4.7 µH | 0805/1206 | SGC addition — boost inductor |
| L_QI | Qi coil 24 µH WPC A11 | Wound coil 48×32mm | SGC addition — Qi receiver coil |
| Y1 | 32.768 kHz crystal | 3.2×1.5mm | Nicla stock |
