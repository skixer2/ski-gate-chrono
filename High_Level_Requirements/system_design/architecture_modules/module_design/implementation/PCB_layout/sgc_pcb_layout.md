# SGC — PCB Layout Design (v3.0 — Nicla Sense ME Replica)

*2026-06-16 — v3.0: Complete redesign. Custom PCB = strict Nicla Sense ME replica.
All Nicla internal connections replicated exactly. SGC peripherals added using only
free ANNA-B112 GPIOs. BHI260AP/BMP390 via Arduino_BHY2 (SPI P0.03–P0.05).
Flash = Nicla's MX25R1635F U7 (CS P0.26). See sgc_pcb_netlist.md for full wiring.*

*2026-06-15 — v1.1: SPI pins updated to match Nicla variant defaults.*

---

## 1. Board Specifications

| Parameter | Value |
|-----------|-------|
| Base design | Arduino Nicla Sense ME replica |
| MCU module | ANNA-B112 (nRF52832 + BLE) |
| Dimensions | ~22 × 55 mm |
| Layers | 4 (Signal-Top, GND, Power, Signal-Bottom) |
| Thickness | 0.8 mm |
| Finish | ENIG |
| Min trace/space | 0.1 mm / 0.1 mm |
| Min via | 0.3 mm drill / 0.6 mm pad |

## 2. Component Placement

```
┌──────────────────────────────────────────────────────────┐
│  [   ANNA-B112 Module (MD1)  ]    [ BHI260AP (U5) ]      │
│  [   nRF52832 + BLE Antenna  ]    [ LGA-44       ]      │
│                                                          │
│  [ 32k XTAL ] [ Flash U7  ]  [ BQ25120 (U9) ]           │
│  [ Y1       ] [ MX25R1635F]  [ Charger IC  ]            │
│                                                          │
│  [ I2C Bus (P15/P16) → RGB LED + Charger ]              │
│                                                          │
│  ─────────────── SGC Peripherals ─────────────────      │
│                                                          │
│  [ LDC1612 (U1) ]  [ LDC Coil (L1) 14mm ]              │
│  [ WSON-12      ]  [ PCB spiral trace  ]                │
│                                                          │
│  [ SK6812 ×5 ][ Beeper ][ Qi Detect ]                   │
│  [ D1-D5     ][ BZ1    ][ P0.10     ]                   │
│                                                          │
│  [ RFID (v2) UNPOP ]  [ DW3000 (v2) UNPOP ]             │
│  [ Impinj E310      ]  [ QFN-40            ]             │
│                                                          │
│  [ Battery JST ]  [ Qi Coil (back side) ]               │
└──────────────────────────────────────────────────────────┘
```

## 3. Critical Layout Rules

### SPI Buses
- **BHI SPI (P0.03–P0.05):** Short traces (< 25 mm). BHI260AP + Flash U7 share.
  BHY2 manages this bus — SGC firmware never touches it directly.
- **SGC SPI (P0.11/P0.27/P0.28):** Equal-length traces (±2 mm). v2 RFID + UWB.

### I2C Buses
- **I2C0 (P0.15/P0.16):** Nicla stock — IS31FL3194 + BQ25120A. No SGC devices.
- **I2C1 (P0.22/P0.23):** LDC1612. 2.2k pull-ups near the MCU.

### LDC1612 Coil
- 14 mm diameter, 2-layer PCB trace spiral
- Keepout zone: > 10 mm from any ferrous components, > 5 mm from BHI260AP (contains BMM150 magnetometer)
- Placed near edge of PCB for cross-arm proximity detection

### Qi Coil Isolation
- Mounted on opposite PCB side from BMM150 (inside BHI260AP)
- > 10 mm separation from magnetometer
- No ground plane under Qi coil area

### SK6812 LED Strip
- 5× LEDs along top PCB edge, ~3 mm pitch
- Single data line from P0.19, daisy-chained
- 100nF decoupling per LED, placed < 2 mm from VDD pin

### Beeper
- Piezo surface transducer bonded to enclosure inner wall
- Connected via P0.09 through 100Ω series resistor
- No sound port needed — IP67 compatible

### Power
- LDO EN (P0.24) tied HIGH via 10k resistor — VDDIO_EXT always on
- v2 MOSFET gates (P0.24 RFID_EN, P0.30 UWB_PWR) default LOW (pulled down)
- Qi detect (P0.10) with 10k pull-up — Qi receiver pulls LOW when active

## 4. Signal Integrity

| Signal | Source | Dest | Length | Notes |
|--------|--------|------|--------|-------|
| BHI_SPI_SCK | ANNA-B112 pin 19 | BHI260AP, Flash U7 | < 25 mm | BHY2 managed |
| BHI_SPI_MOSI | ANNA-B112 pin 24 | BHI260AP, Flash U7 | < 25 mm | |
| BHI_SPI_MISO | ANNA-B112 pin 23 | BHI260AP, Flash U7 | < 25 mm | |
| SGC_SPI_SCK | ANNA-B112 pin 34 | RFID, UWB | < 40 mm | v2 only |
| SGC_SPI_MOSI | ANNA-B112 pin 29 | RFID, UWB | < 40 mm | v2 only |
| SGC_SPI_MISO | ANNA-B112 pin 28 | RFID, UWB | < 40 mm | v2 only |
| I2C1_SDA | ANNA-B112 pin 36 | LDC1612 | < 30 mm | |
| I2C1_SCL | ANNA-B112 pin 37 | LDC1612 | < 30 mm | |
| LED_DIN | ANNA-B112 pin 35 | SK6812 D1 | < 20 mm | NZR 800 kHz |
| BEEPER | ANNA-B112 pin 21 | BZ1 | < 30 mm | PWM ~4 kHz |
| QI_DETECT | ANNA-B112 pin 22 | Qi receiver | < 20 mm | |
| LDC_INTB | ANNA-B112 pin 20 | LDC1612 INTB | < 15 mm | Interrupt, active LOW |
| RFID_CS (v2) | ANNA-B112 pin 45 | RFID /CS | < 40 mm | |
| UWB_CS (v2) | ANNA-B112 pin 27 | DW3000 /CS | < 40 mm | |

## 5. Layers

| Layer | Contents |
|-------|----------|
| Top (L1) | Components, signals, LDC coil |
| GND (L2) | Solid ground plane (no splits) |
| PWR (L3) | 3.3V + 1.8V power planes, split |
| Bottom (L4) | Qi coil, minimal routing, ground pour |

## 6. Thermal
- BQ25120 charger: on-board, adequate Cu pour for heat dissipation
- BHI260AP: low power, no special thermal management needed
- RFID (v2): inactive on v1, thermal vias to GND plane for v2
- Enclosure: no active cooling — passive conduction through PCB + potting
