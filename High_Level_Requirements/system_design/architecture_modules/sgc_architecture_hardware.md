# SGC — Architecture: Hardware (v2.0)

*2026-06-17 — v2.1: Corrected I²C bus naming (Wire=p22/p23, Wire1=p15/p16 per Nicla variant). BHI260AP on internal SPI1 (p3/p4/p5, CS=p31). Flash on SPI1 (CS=p26). Onboard RGB is I2C IS31FL3194 on Wire1, not GPIO. SK6812 strip on P0.19 for custom PCB only.*

*2026-06-16 — v2.0: Complete pin reassignment. Custom PCB is a strict Nicla Sense ME replica. All Nicla pins replicated exactly. SGC peripherals on free ANNA-B112 GPIOs only. BHI260AP/BMP390 accessed via Arduino_BHY2 (SPI slave on P0.03–P0.05). Battery via BQ25120/BHY2. Flash = Nicla's onboard MX25R1635F (U7). RFID + UWB v2 kept. See sgc_architecture_decisions.md and config.h v2.0 for full mapping rationale.*

*2026-06-15 — v1.3: Unified pinout — custom PCB mirrors Nicla Sense ME. SPI on variant defaults. Single firmware binary. No #ifdef NICLA_SENSE_ME conditionals.*

*2026-06-09 — v1.2: Architecture pivot — UHF RFID reader removed from active BOM. Power budget updated (no RFID).*

*2026-06-08 — v1.1: Fixed W25Q16 on SPIM0; removed stale RFID_EN assignment; LDC1612 arming = cross-arm proximity; PCB 22×55mm; 5× SK6812-mini with sequential animation.*

*2026-06-06 — Initial version.*

---

### Terminology: ANNA-B112 Module vs Carrier PCB

The **ANNA-B112** is a pre-certified module (nRF52832 + BLE antenna in a single SMD package).
It is NOT a bare chip — it's a complete subsystem that gets soldered onto a larger PCB.

That larger PCB is the **carrier PCB** (custom SGC board, 22 × 55 mm, 4-layer) which:
- **Carries** the ANNA-B112 module as a mounted component
- **Adds** all SGC peripherals: LDC1612 proximity, SK6812 × 5, beeper, Qi coil, MOSFET power gating, BQ25120 charger
- **Replicates** the Nicla Sense ME wiring exactly (BHI260AP, BMP390, Flash MX25R1635F, IS31FL3194)
- **Reserves** footprint for v2 (RFID Impinj E310 + UWB DW3000, not populated)

**Analogy:** ANNA-B112 = "brain", carrier PCB = "body" providing power, sensors, and actuators.

### Production Architecture (200 units/month)

For the production phase, the approach shifts from a ground-up Nicla replica to a
**modular assembly using off-the-shelf Nicla Sense ME boards**:

- The production Nicla is used **as-is** (not re-laid-out) — layout files are proprietary
  and Arduino Pro custom spins require thousands of units per batch
- A **companion carrier PCB** sits **side-by-side** with the Nicla (NOT stacked —
  Nicla is ~5 mm thick, stacking would exceed enclosure constraints)
- The carrier PCB contains: LDC1612, 5V boost (SOT-23), level shifter, SK6812 × 5,
  beeper, battery connector
- Connection: Nicla edge pins → ribbon cable or rigid flex → carrier PCB
- EMS partner does panelized assembly + bed-of-nails flashing via SWD

**Full production strategy:** `sgc_production_strategy.md`

---

## 1. Hardware Block Diagram — Nicla Sense ME Replica

The custom PCB is a **strict replica** of the Arduino Nicla Sense ME.
All Nicla internal connections are preserved exactly. SGC peripherals are
added using only free ANNA-B112 GPIOs.

```
                   ╔══════════ Nicla Sense ME (replica) ═══════════╗
                   ║                                              ║
                   ║  ┌──────────────────────────────────────┐   ║
                   ║  │      ANNA-B112 (nRF52832 + BLE)      │   ║
                   ║  │   64 MHz Cortex-M4, 512 KB Flash     │   ║
                   ║  │              64 KB RAM               │   ║
                   ║  └──┬───┬───┬───┬───┬───┬───┬───┬──────┘   ║
                   ║     │   │   │   │   │   │   │   │          ║
                   ║  SPI│   │I2C│I2C│   │   │   │   │          ║
                   ║  P03│P14│P15│P22│   │   │   │   │          ║
                   ║  P04│INT│P16│P23│   │   │   │   │          ║
                   ║  P05│   │   │   │   │   │   │   │          ║
                   ║     │   │   │   │   │   │   │   │          ║
                   ║  ┌──▼───▼─┐ │   │   │   │   │   │          ║
                   ║  │BHI260AP│ │   │   │   │   │   │          ║
                   ║  │IMU hub │ │   │   │   │   │   │          ║
                   ║  │+BMM150 │ │   │   │   │   │   │          ║
                   ║  │CS:P0.31│ │   │   │   │   │   │          ║
                   ║  └──┬─────┘ │   │   │   │   │   │          ║
                   ║     │SPI-mst│   │   │   │   │   │          ║
                   ║  ┌──▼──┐┌─▼──┐│   │   │   │   │   │          ║
                   ║  │BMP  ││BME ││   │   │   │   │   │          ║
                   ║  │390  ││688 ││   │   │   │   │   │          ║
                   ║  └─────┘└────┘│   │   │   │   │   │          ║
                   ║               │   │   │   │   │   │          ║
                   ║  ┌────────────▼───▼┐  │   │   │   │          ║
                   ║  │  Flash U7       │  │   │   │   │          ║
                   ║  │  MX25R1635F 2MB │  │   │   │   │          ║
                   ║  │  CS: P0.26      │  │   │   │   │          ║
                   ║  └────────────────┘  │   │   │   │          ║
                   ║                      │   │   │   │          ║
                   ║  ┌───────────────────▼───▼─┐ │   │          ║
                   ║  │  RGB LED + BQ25120       │ │   │          ║
                   ║  │  (I2C0: P0.15/P0.16)    │ │   │          ║
                   ║  └─────────────────────────┘ │   │          ║
                   ╚══════════════════════════════╧═══╧══════════╝

                   ╔══════ SGC Peripherals (added on custom PCB) ═══╗
                   ║                                               ║
                   ║  I2C1 bus (P0.22/P0.23):                     ║
                   ║    ┌─────────┐                                ║
                   ║    │ LDC1612 │  Inductive proximity            ║
                   ║    │ 0x2A    │  INTB → P0.02                  ║
                   ║    └─────────┘                                ║
                   ║                                               ║
                   ║  GPIO pins (free ANNA-B112 GPIOs):            ║
                   ║    P0.02 → LDC_INTB   (interrupt)             ║
                   ║    P0.09 → BEEPER     (PWM piezo)             ║
                   ║    P0.10 → QI_DETECT  (Qi presence)           ║
                   ║    P0.19 → LED_STRIP  (SK6812, custom PCB)   ║
                   ║                                               ║
                   ║  Onboard RGB LED uses I2C IS31FL3194         ║
                   ║  on Wire1 (P0.15/P0.16) — no GPIO needed.    ║
                   ║                                               ║
                   ║  v2 only (RFID + UWB, not populated in v1):   ║
                   ║    P0.20 → RFID_CS    (SPI chip select)       ║
                   ║    P0.24 → RFID_EN    (power enable)          ║
                   ║    P0.29 → UWB_CS     (SPI chip select)       ║
                   ║    P0.30 → UWB_PWR    (power enable)          ║
                   ╚═══════════════════════════════════════════════╝
```

**Key design rules (v2.0):**
- **Strict Nicla replica:** All Nicla Sense ME pins wired identically.
  BHI260AP on SPI P0.03–P0.05 (CS P0.31, INT P0.14). Sensors via Arduino_BHY2.
- **Battery:** BQ25120 charger + BHI260AP fuel gauge. No custom VBAT_ADC.
- **Flash:** Nicla's onboard MX25R1635F (U7) — 2 MB, CS P0.26. No external flash.
- **I2C1 bus (P0.22/P0.23):** LDC1612 added to Nicla's external I2C header bus.
- **BHI SPI (P0.03–P0.05):** BHI260AP + Flash U7 — managed by BHY2 internally.
- **SGC SPI (P0.11/P0.27/P0.28):** RFID (v2) + UWB (v2) — Arduino SPI bus.
  Nicla's external header SPI, routed internally on custom PCB.
  CS P0.20 (RFID), CS P0.29 (UWB).
- **6 GPIOs unavailable:** P0.06, P0.07, P0.08, P0.12, P0.13, P0.17 are not broken
  out from the ANNA-B112 module (ANNA-B112 datasheet Table 7).

---

## 2. Pin Map — Nicla Sense ME Replica + SGC Peripherals

### Category A — Nicla Replica Pins (LOCKED — identical to stock Nicla Sense ME)

These match the Nicla Sense ME exactly. Custom PCB copies this wiring.

| GPIO | Nicla Function | Bus / Peripheral |
|------|---------------|-----------------|
| P0.00 | 32.768 kHz crystal (Y1, XL1) | RTC oscillator |
| P0.01 | 32.768 kHz crystal (Y1, XL2) | RTC oscillator |
| P0.03 | SPI SCK | Internal SPI → BHI260AP (U5) + Flash U7 |
| P0.04 | SPI MOSI | Internal SPI → BHI260AP (U5) + Flash U7 |
| P0.05 | SPI MISO | Internal SPI → BHI260AP (U5) + Flash U7 |
| P0.11 | SPI SCK | External SPI header (J1-6), shared M2 bus |
| P0.14 | BHI260AP HIRQ/INT | Interrupt from BHI260AP to nRF52832 |
| P0.15 | I2C1 SDA (Wire1) | RGB LED (IS31FL3194) + BQ25120A charger |
| P0.16 | I2C1 SCL (Wire1) | RGB LED + charger |
| P0.18 | System reset | BQ25120 MR (reset network) |
| P0.21 | RESET_N | ANNA-B112 module reset (pin 12) |
| P0.22 | I2C0 SDA (Wire) | External I2C header (J2-1) / ESLOV |
| P0.23 | I2C0 SCL (Wire) | External I2C header (J2-2) / ESLOV |
| P0.25 | Charge disable | BQ25120 CD |
| P0.26 | Flash CS | MX25R1635F (U7) — Nicla's onboard 2 MB flash |
| P0.27 | SPI MOSI | External SPI header (J1-4), shared M2 bus |
| P0.28 | SPI MISO | External SPI header (J1-5), shared M2 bus |
| P0.31 | BHI260AP CS | Chip select for BHI260AP sensor hub |

🔒 **Not broken out from ANNA-B112 module** (ANNA-B112 datasheet Table 7):
P0.06, P0.07, P0.08, P0.12, P0.13, P0.17 — these have no pads on the module package.

### Category B — SGC-Specific Pin Assignments

Assigned to free ANNA-B112 GPIOs not consumed by the Nicla replica.

| GPIO | SGC v1 (core) | SGC v2 (RFID + UWB) | Nicla original |
|------|--------------|---------------------|----------------|
| P0.02 | `LDC_INTB` | `LDC_INTB` | A0 header |
| P0.09 | `BEEPER` | `BEEPER` | UART RX / LPIO2 |
| P0.10 | `QI_DETECT` | `QI_DETECT` | LPIO3 header |
| P0.19 | `LED_STRIP` | `LED_STRIP` | ESLOV INT |
| P0.20 | *(debug UART)* | `RFID_CS` | UART TX / LPIO1 |
| P0.24 | *(spare)* | `RFID_EN` | LPIO0 / LDO EN |
| P0.29 | *(spare)* | `UWB_CS` | SPI CS header |
| P0.30 | *(spare)* | `UWB_PWR` | A1 header |

**v1 net: 4 GPIOs** (P0.02, P0.09, P0.10, P0.19). 4 spare.
**v2 net: 8 GPIOs used.** Zero spare.

### Shared Buses

| Bus | Pins | Arduino Object | Devices |
|-----|------|---------------|---------|
| Internal SPI1 | P0.03 (SCK), P0.04 (MOSI), P0.05 (MISO) | `SPI1` | BHI260AP (CS P0.31), Flash U7 (CS P0.26) — BHY2-managed + SPIF BlockDevice |
| User SPI | P0.11 (SCK), P0.27 (MOSI), P0.28 (MISO) | `SPI` | RFID v2 (CS P0.20), UWB v2 (CS P0.29) — Arduino SPI header |
| I2C0 / Wire | P0.22 (SDA), P0.23 (SCL) | `Wire` | LDC1612 @ 0x2A — Nicla external I²C header |
| I2C1 / Wire1 | P0.15 (SDA), P0.16 (SCL) | `Wire1` | BQ25120A PMIC, RGB LED IS31FL3194 @ 0x53 — Nicla internal |

### I²C Bus Address Map

| Device | 7-bit Address | Bus | Notes |
|--------|--------------|-----|-------|
| BHI260AP | *(via BHY2/SPI1)* | Internal SPI1 | Managed by Arduino_BHY2 library (SPI, not I²C) |
| BMP390 | *(via BHY2)* | BHI260AP sub-sensor | Internal to BHI260AP sensor hub |
| LDC1612 | 0x2A | I2C0 / Wire (P0.22/P0.23) | SGC addition — external I²C header |
| RGB LED | 0x53 | I2C1 / Wire1 (P0.15/P0.16) | IS31FL3194 driver (Nicla stock) |
| BQ25120A | *(via Wire1)* | I2C1 / Wire1 (P0.15/P0.16) | PMIC charger IC (Nicla stock) |

### Unused ANNA-B112 GPIOs (available for future)

P0.09, P0.20 (if v2 not populated), P0.24 (if v2 not populated),
P0.29, P0.30 (if v2 not populated).

---

## 3. Sensor Subsystem

### BHI260AP — 9-Axis Fusion IMU (Nicla Stock)

| Parameter | Value | Reference |
|-----------|-------|-----------|
| Interface | Internal SPI1 (P0.03–P0.05, CS P0.31, INT P0.14) | Nicla Sense ME stock wiring |
| Software API | Arduino_BHY2 library | `BHY2.begin()`, sensor classes |
| Accel ODR | 200 Hz | 2× fusion rate |
| Gyro ODR | 400 Hz | 4× fusion rate |
| Fusion ODR | 100 Hz | Target logging rate (F01) |
| Output 1 | Rotation Vector (quaternion, 4×int16) | Fused 9-axis orientation |
| Output 2 | Linear Acceleration (3×int16, mm/s²) | Accel with gravity removed |
| Calibration | Figure-8 for 10s, accuracy ≥ 2 required for arming (F51) | BHY2 handles cal internally |

The BHI260AP on the Nicla is connected via internal SPI1 (not I²C). The Arduino_BHY2 library
handles all low-level communication, firmware upload, and sensor fusion internally.
SGC firmware uses BHY2 sensor classes (`SensorXYZ`, `Sensor`, etc.) — no direct
SPI register access needed. Arduino alias: `SPI1` object (MOSI=p4, MISO=p5, SCK=p3).

### BMP390 — Barometric Pressure (Nicla Stock)

| Parameter | Value |
|-----------|-------|
| Interface | Managed by BHI260AP (sub-sensor on BHI260AP's SPI master bus) |
| Software API | Arduino_BHY2: `Sensor pressure(SENSOR_ID_BARO)` |
| ODR | 100 Hz (via BHY2) |
| Resolution | 0.25 m altitude equivalent |
| Start detection | Descent > 1.5 m/s vertical velocity (F04) |
| End detection | 10 s flatline ±0.3 m/s + IMU stillness (F06) |

### LDC1612 — Inductive Proximity Sensor (SGC Addition)

| Parameter | Value |
|-----------|-------|
| Interface | I2C1 (P0.22/P0.23), address 0x2B (Seeed default; 0x2A if ADDR bridged to GND). INTB → P0.02 |
| Channels | 2 (one per coil — dual-axis sensing) |
| Coil | PCB trace inductor, 14 mm diameter, 2-layer spiral |
| Target | Passive copper/iron foil disc in **opposite arm's** strap |
| Detection | Cross-arm proximity — athlete brings forearms together |
| Arm threshold | 1000 ms continuous proximity (F03) |
| Factory reset | 20 s continuous hold (F42) |
| Sleep polling | 10 Hz, ~50 µA (F13 wake) |

**Rationale:** No mechanical button, no moving parts — preserves IP67 sealing. The athlete naturally brings forearms together before pushing off at the start gate. This motion is detected as rising LDC1612 inductance shift from the approaching copper/iron target disc on the opposite strap. Both arms arm independently on their own sensor.

---

## 4. RGB LED (Nicla Stock + SGC Addition)

### Onboard RGB LED (Nicla Stock)

| Parameter | Value |
|-----------|-------|
| Type | IS31FL3194 I2C LED driver IC |
| Interface | I2C1 / Wire1 (P0.15/P0.16), address 0x53 |
| Software API | `nicla::leds.begin()` then `nicla::leds.setColor(r, g, b)` |
| Colors | Red, green, blue — each 0-255 PWM |
| Init requirement | `nicla::begin()` must be called first to init Wire1 |

**Important:** `LEDR`/`LEDG`/`LEDB` in pins_arduino.h are `I2CLed` objects,
NOT GPIO pins. `digitalWrite()`/`analogWrite()` on them calls Nicla_System internally.

### SK6812 LED Strip (SGC Custom PCB Addition)

| Parameter | Value |
|-----------|-------|
| Type | 5× SK6812-mini (2×2mm each) |
| Arrangement | Single row along top PCB edge, ~3 mm pitch |
| Interface | Single-wire NZR, P0.19, 800 kHz |
| Protocol | Standard WS2812-compatible 24-bit GRB, daisy-chained |

### Sequential Animation

Instead of all LEDs flashing simultaneously, they are driven in **sequential animation** — a flowing lit point that travels along the strip. This:

- **Improves visibility:** A moving light catches the eye better than a static blink, especially in peripheral vision.
- **Reduces peak current:** Only 1 LED at full brightness, others dim/off → lower instantaneous draw.
- **Looks better:** Gives a premium, dynamic appearance — a "breathing" flow rather than a cheap blink.

**Animation patterns by state (F41):**

| State | Color | Pattern |
|-------|-------|---------|
| SLEEP | Off | All off |
| IDLE (uncalibrated) | Blue | Slow flowing point (1 cycle/s), 40% duty |
| IDLE (calibrated, acc ≥ 2) | Blue | Solid chase (flowing point, 80% duty) |
| SETUP | White | ⚠️ v2 only — Slow flowing point (1 cycle/s) |
| ARMED | Green | Fast chase (2 cycles/s), 100% duty |
| LOGGING | Red | Solid chase (2 cycles/s), 100% duty |
| POST_RUN | Blue | Slow flowing point (1 cycle/s) |
| LOW BATTERY / ERROR | Yellow | Rapid blink all LEDs (3 Hz) |
| FACTORY RESET | Red | 3× fast chase then restart |

**Current budget per LED:** ~12 mA at full white, ~5 mA per color at typical brightness.
Peak with sequential animation (1 LED full + 4 dim): ~8 mA. Average across state colors: ~5–7 mA.

**Physical:** LEDs placed under translucent polycarbonate window in enclosure. Light pipe effect through the PC wall diffuses the point into a soft glow visible from multiple angles.

---

## 5. UHF RFID Subsystem — ⚠️ UNPOPULATED (v2 ONLY)

**The UHF RFID reader is NOT populated in v1.** Impinj E310 footprint + SPI routing +
VDD MOSFET + ceramic antenna keepout are present on the PCB but no IC is loaded.

### v2 Specs (for reference only)

| Parameter | Value |
|-----------|-------|
| Module | Impinj E310-based (SILION SIM3600, Chafon CF-E311) |
| Size | ~25 × 25 mm SMD |
| Frequency | 860–960 MHz (ETSI EN 302 208) |
| Interface | SPI to SGC SPI bus (P0.11/P0.27/P0.28), CS: P0.20 |
| Enable | GPIO P0.24 → MOSFET gate → VDD_RFID rail |
| Power | ~80 mA active, duty-cycled per discipline |
| Read range | ≥ 1 m with passive UHF gate tags |

### Antenna Keepout (v1 PCB, same as v2)

```
UHF RFID ceramic antenna (unpopulated)
        │
        │  > 10 mm
        ▼
    BMM150 (inside BHI260AP)
```

---

## 6. DW3000 UWB Footprint (⚠️ NOT POPULATED — v2 ONLY)

| Parameter | Value |
|-----------|-------|
| IC | Qorvo DW3000 (5×5 mm QFN-40) |
| Standard | IEEE 802.15.4z |
| Frequency | Channel 5 (6.5 GHz) or Channel 9 (8 GHz) |
| Interface | SPI to SGC SPI bus (P0.11/P0.27/P0.28), CS: P0.29 |
| Power gating | MOSFET on VDD_UWB rail, GPIO P0.30, default LOW (off) |
| Antenna | Ceramic chip antenna footprint, tuned for UWB |
| Keepout | > 15 mm from BMM150, > 10 mm from UHF RFID antenna |

**⚠️ v1 firmware MUST NOT touch P0.29 (CS) or P0.30 (PWR).** These pins are reserved.

---

## 7. Power Architecture

### Power Tree

```
Qi Charging Pad (5W, any Qi-compliant transmitter)
        │
        ▼
   Qi Receiver Coil (24 µH, WPC A11-type)
        │  AC1/AC2
        ▼
   Qi Receiver IC (IP6833 or BQ51013B module)
        │  ┌─ Sync rectifier + LDO → 5 V regulated output
        │  └─ ASK/FSK comms to transmitter
        ▼
   BQ25120A Charger IC
        │  ┌─ Charging status → BQ25120 I²C (Wire1, P0.15/P0.16)
        │  └─ Battery temp → NTC thermistor on battery lead
        ▼
   Renata ICP622540PMT
   600 mAh, 3.7V Li-Po
   Integrated NTC (3-wire: BAT+, BAT−/GND, NTC)
        │
        ├──► 3.3V LDO (TPS7A05 or similar) → VDD_nRF (nRF52832, Flash, Beeper)
        │
        ├──► 1.8V LDO → VDD_Sensors (BHI260AP, BMP390, LDC1612)
        │
        ├──► 5V Boost (MT3608) → VDD_LED (SK6812 × 5 + level shifter)
        │     EN pin → GPIO (P0.24 or free pin) = software power gate
        │
        ├──► MOSFET (P0.24) → VDD_RFID (⚠️ v2 only — UHF RFID unpopulated in v1)
        │
        └──► MOSFET (P0.30) → VDD_UWB (⚠️ reserved, unpopulated)
```

**⚠️ P0 Prototype (Monolithic PCB):** The custom Nicla replica PCB used at P0 powers SK6812 LEDs
from the 3.3V rail directly (no boost). LEDs are dim but functional for field validation.
From P1 onward (companion carrier PCB), the 5V boost provides full brightness and reliable
cold-weather operation. Qi receiver on P0 uses a pre-built module; P1+ integrates the IP6833 IC.
See §7.3 (boost) and §7.4 (Qi receiver).

### 5V Boost Converter — SK6812 LED Supply

The SK6812-mini LEDs require ≥ 4.0V for reliable operation (5.0V nominal).
At 3.3V they are dim, have poor colour accuracy, and may fail to latch data
in cold temperatures. A dedicated boost converter generates the 5V LED rail
from the Li-Po battery, independent of the LDO-regulated digital rails.

**IC Selection: MT3608 (Aerosemi)**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Package | SOT-23-6 | 2.9 × 1.6 mm, tiny |
| Input range | 2.0 – 24 V | Works down to deeply depleted Li-Po (3.0V cutoff) |
| Output | Adjustable, set to 5.0 V | Feedback divider: R1=75k, R2=10k → 5.1 V |
| Switch current | 4 A peak | Massive headroom for 300 mA load |
| Switching freq | 1.2 MHz | Enables small inductor (4.7 µH, 0805) |
| Quiescent | 65 µA (PFM mode) | Light-load efficiency for battery life |
| Shutdown | ~2 µA via EN pin LOW | MCU-controlled power gate |
| Efficiency | ~88% @ 3.7V→5V/300mA | Per MT3608 datasheet curves |
| Temp range | −40 to +85 °C | Winter sports approved |
| Cost | ~$0.08–0.15 (volume) | JLCPCB basic part (C84817), no extended fee |

**External components (minimum BOM):**

| Ref | Value | Package | Purpose |
|-----|-------|---------|---------|
| L_Boost | 4.7 µH, Isat ≥ 2 A | 0805 or 1206 | Power inductor |
| C_IN | 10 µF, 10 V, X5R | 0805 | Input decoupling |
| C_OUT | 22 µF, 10 V, X5R | 0805 | Output filter |
| R_FB1 | 75 kΩ, 1% | 0603 | Feedback divider high-side |
| R_FB2 | 10 kΩ, 1% | 0603 | Feedback divider low-side |
| C_FF | 22 pF (optional) | 0603 | Feedforward cap across R_FB1 for stability |

**Feedback calculation:** Vout = Vref × (1 + R1/R2) = 0.6 × (1 + 75k/10k) = 5.1 V

**EN pin control:** The boost EN pin is driven by an nRF52832 GPIO
(P0.24 or any free pin). Firmware holds EN LOW during SLEEP (shutdown, ~2 µA),
HIGH during ARMED/LOGGING/POST_RUN (LED active). This eliminates the ~65 µA
boost quiescent draw when LEDs are off.

**Alternative ICs (drop-in if MT3608 out of stock):**

| IC | Package | Isw | Vin min | Notes |
|----|---------|-----|---------|-------|
| FP6291 | SOT-23-6 | 2.5 A | 2.6 V | Higher Vin min, less headroom for depleted battery |
| TPS61023 | SOT-563 | 3.7 A | 0.5 V | Premium TI option (~$0.45), ultra-low Vin, but tiny SOT-563 harder to hand-solder |

### Qi Wireless Charging Receiver

The Qi receiver harvests power from any standard Qi (WPC BPP) transmitter pad and
delivers regulated 5 V to the BQ25120A charger input (IN pin, 3.4–6.4 V range).

**Architecture:**

```
Qi Transmitter Pad (5W, any brand)
        │  ~110–205 kHz magnetic coupling
        ▼
   Receiver Coil (24 µH, WPC A11 type)
        │  AC terminals
        ▼
   Qi Receiver IC
        │  ┌─ Full-bridge synchronous rectifier
        │  ├─ 5 V LDO regulator
        │  ├─ ASK modulation (comm back to transmitter)
        │  └─ FSK demodulation (comm from transmitter)
        ▼
   5 V DC Output → BQ25120A IN pin
```

#### P0 Prototype: Pre-Built Module

For the P0 prototype use a pre-built Qi receiver module. This avoids designing
and debugging the Qi front-end during the field-validation phase.

**Recommended: BQ51013B-based module**

| Parameter | Value |
|-----------|-------|
| Source | Adafruit #1901, Protopoint, or AliExpress generic |
| Chip | TI BQ51013B (industry standard) |
| Output | 5.0 V ±0.2 V, 500 mA continuous |
| Size | ~28 × 26 mm PCB + attached coil (~40 × 30 mm) |
| Connections | V+ (5V out), GND — two solder pads |
| Coil | Included, thin flexible PCB coil with ferrite shield |
| Cost | $5–15 depending on source |

**Connection to SGC PCB:** V+ → BQ25120A IN pin, GND → common ground.
The module mounts inside the enclosure with the coil facing outward
(toward the transmitter pad).

**Alternative modules:**

| Module | Chip | Output | Notes |
|--------|------|--------|-------|
| Adafruit #1901 | BQ51013B / RT1650 | 5V 500mA | Best documented, ships worldwide |
| Protopoint Mini Qi | BQ51013A | 5V 500mA | Compact, breadboard-friendly |
| ElectroDragon Qi | BQ51013B | 5V 1A | Larger, higher current |
| AliExpress generic | BQ51013B (varies) | 5V 500mA–1A | Cheapest ($2–5), verify on arrival |

#### P1+ Production: Integrated IP6833 (Injoinic)

For the companion carrier PCB (and the monolithic Nicla replica if redesigned),
integrate the Qi receiver chip directly onto the PCB.

**IC Selection: IP6833 (Injoinic)**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Package | QFN-28, 4 × 4 mm, 0.4 mm pitch | JLCPCB assembly compatible |
| Standard | WPC Qi BPP (5 W) | Compatible with all Qi transmitters |
| Output | 5.0 V, up to 1.6 A | Programmable LDO, set to 5 V |
| Rectifier | Integrated synchronous full-bridge | 96% efficiency, no external diodes |
| MCU | Integrated 32-bit ARM | Handles Qi protocol, ASK/FSK comms |
| Protection | FOD, OVP, OTP, UVLO, current limit | All built-in |
| Temp range | −40 to +85 °C | Winter sports approved |
| Cost | ~$1–2 (volume) | LCSC, widely available |

**External components (minimum BOM):**

| Ref | Value | Package | Purpose |
|-----|-------|---------|---------|
| L_QI | 24 µH WPC A11 coil | ~48 × 32 × 1 mm | Qi receiver coil, wound wire + ferrite |
| C_AC1 | 22 nF, 50 V, NP0/C0G | 0603 | Resonant capacitor AC1 to GND |
| C_AC2 | 22 nF, 50 V, NP0/C0G | 0603 | Resonant capacitor AC2 to GND |
| C_RECT | 22 µF, 16 V, X5R | 0805 | Rectifier output filter |
| C_BOOT | 100 nF, 16 V, X5R | 0603 | Bootstrap capacitor for HS gate driver |
| C_OUT | 10 µF, 10 V, X5R | 0805 | LDO output decoupling |
| C_VDD | 1 µF, 10 V, X5R | 0603 | Internal VDD decoupling |

**Qi coil options:**

| Coil | Type | L | Size | Notes |
|------|------|---|------|-------|
| Würth 760308101 | Wound + ferrite | 24 µH | 48×32×1.4mm | Qi A11, $3–5, recommended |
| Würth 7603081013 | Wound + ferrite | 10.5 µH | 30×30mm | Smaller alternative |
| PCB spiral (custom) | 2-layer trace | ~10–15 µH | 30×30mm area | No BOM cost, lower efficiency |

**Note:** Pre-made wound coils with ferrite backing deliver ~75–80% system
efficiency vs ~60–65% for PCB spirals. For the production device targeting
cold-weather operation, the higher efficiency of wound coils helps offset
battery capacity losses at low temperatures.

**IP6833 application schematic (simplified):**

```
    L_QI (24 µH)
  ┌───────~~~~───────┐
  │                  │
  ├─ C_AC1 (22nF) ──┤ GND
  │                  │
AC1                  AC2     ← IP6833 pins
  │                  │
  └──────┬───────────┘
         │
    ┌────┴────┐
    │  IP6833 │
    │  QFN-28 │
    │         │
    │  RECT ──┼── C_RECT (22µF) ── GND
    │  BOOT ──┼── C_BOOT (100nF) ─ GND
    │  VOUT ──┼── C_OUT (10µF) ── GND
    │         │      │
    │  VDD  ──┼── C_VDD (1µF) ─── GND
    │  GND  ──┼── GND plane
    │         │      │
    │  (other pins: NC or tied per datasheet)
    └─────────┘      │
                     ▼
              5V → BQ25120A IN
```

### Power Budget

| State | Total draw | Notes |
|-------|-----------|-------|
| SLEEP | ~55 µA | LDC1612 10 Hz poll, boost shut down (EN LOW) |
| IDLE | ~14 mA | BLE advertising, sensors idle, boost off (LEDs off) |
| ARMED | ~26 mA | 100 Hz fusion, ring buffer filling, boost active (LEDs chasing green) |
| LOGGING | ~24 mA | Flash writes active, BLE off, boost active (LEDs chasing red). **RFID unpopulated in v1** |
| POST_RUN | ~17 mA | BLE advertising for transfer, boost active (LEDs chasing blue) |

### Session Budget (3h, 10 runs, v1 — no RFID)

```
  20 min IDLE/ARMED (avg 18 mA):          ~6 mAh
  20 min LOGGING (24 mA):                 ~8 mAh  (no RFID in v1)
  20 min POST_RUN/BLE (17 mA):            ~6 mAh
 120 min IDLE between runs (14 mA):       ~28 mAh
  ─────────────────────────────────
  Total per session:                      ~48 mAh

  600 mAh / 48 mAh = ~12.5 sessions (20°C)
  At −10°C (25% derate):      ~9 sessions (H02 satisfied: ≥8h / ~3 sessions)
```

Note: Budget includes 5V boost active during ARMED, LOGGING, and POST_RUN states.
Boost efficiency ~88% at 300 mA LED load; sequential animation keeps average
LED draw ~15 mA @ 5V → ~85 mW load on battery (~23 mA @ 3.7V after efficiency).

### Battery Protection

| Condition | Action |
|-----------|--------|
| VBAT < 3.3V | Close file, enter SLEEP (R04) |
| VBAT < 2.5V | LDO dropout → BLE bonding lost (RAM cleared). Phone auto-re-pairs on next connection (Just Works, no user intervention) |
| Qi power detected | Charge regardless of state. If LOGGING: continue logging. BLE operations allowed. Charging status → GATT ...ABCF |
| NTC over-temp | BQ25100 reduces charge current (JEITA profile) |

---

## 8. Enclosure

### Mechanical Design

| Parameter | Value | Reference |
|-----------|-------|-----------|
| Material | Polycarbonate (translucent) | Light pipes for RGB LED strip (F41) |
| Sealing | IP67, no mechanical buttons, no ports | H03 |
| Target disc | Passive copper/iron foil disc embedded in **opposite arm's** strap — no moving parts, detected by LDC1612 cross-arm proximity | I06, H04 |
| Beeper | Piezo surface transducer bonded to inner enclosure wall | No sound port needed (H03, I08) |
| LED window | Translucent PC strip over 5× SK6812-mini LEDs | F41 |
| Qi coil | Mounted on opposite PCB side from BMM150 | H08 isolation > 10 mm |
| Thickness | < 16 mm total (PCB + battery + Qi coil + enclosure walls) | H05 |
| Weight | ≤ 40 g per arm | H06 |
| Shock | 200 g | H09 — internal potting for impact resistance |
| Strap | Elastic forearm band + quick-release clip | Forearm mounting |

### Thermal Considerations

| Condition | Behavior |
|-----------|----------|
| −20°C to −10°C | Battery derated ~25%. LDOs and MCU rated to −40°C — no issue |
| Qi charging (any temp) | BQ25100 manages charge current. NTC on battery lead provides temp monitoring |
| Summer storage | Self-discharge ~5%/month. Shelf life ~6–8 months before recharge needed |

### Manufacturing Notes

- **Prototype:** 3D-printed (MJF or SLA), translucent polycarbonate-like resin. ~$15–20 per shell
- **Production:** Injection-molded PC. Tooling amortized over 5000+ units (~$6/unit at volume)
- **Assembly:** PCB + battery potted inside enclosure for impact resistance. No screws (IP67 seal by ultrasonic welding or adhesive)
- **Calibration:** BMM150 figure-8 after final assembly; RFID reader verified on known tag at 1 m

---

## 9. PCB Layout

### Board Specs

| Parameter | Value |
|-----------|-------|
| Layers | 4-layer FR4 (signal-GND-PWR-signal) |
| Thickness | 0.8 mm (thin for wearable) |
| Dimensions | ~22 × 55 mm (elongated for forearm anatomy) |
| Finish | ENIG (required for BHI260AP LGA pads) |
| Copper | 1 oz (35 µm) |
| Min trace/space | 0.1 mm / 0.1 mm |

### Component Placement Zones

```
┌──────────────────────────────────────────────────────┐
│  UHF RFID Module          │  UWB Footprint           │
│  (top-left, antenna       │  (top-right,             │
│   edge facing outward)    │   UNPOPULATED)           │
│                           │                          │
├───────────────────────────┼──────────────────────────┤
│                           │                          │
│  LDC1612 Coil             │  SPI Flash (MX25R1635F)  │
│  (14mm PCB spiral,        │  SOIC-8                  │
│   top edge zone)          │                          │
│                           │  Power section           │
│  BHI260AP                 │  LDOs, BQ25100,          │
│  (center, away from       │  MOSFETs, passives       │
│   RFID + Qi coil)         │                          │
│                           │  Battery connector       │
│  BMP390                   │  (3-pin JST-ACH)         │
│  (edge, near vent)        │                          │
├───────────────────────────┼──────────────────────────┤
│                           │                          │
│  nRF52832                 │  RGB LED Strip           │
│  (center-bottom)          │  5× SK6812-mini along    │
│                           │  top edge, spaced ~3mm   │
│                           │  (sequential animation)  │
├───────────────────────────┴──────────────────────────┤
│              Qi Coil (bottom layer)                  │
│        > 10 mm from BMM150 (H08)                     │
│        Centered for even charging                    │
└──────────────────────────────────────────────────────┘
```

### Critical Keepout Zones

| Component | From | Distance | Reason |
|-----------|------|----------|--------|
| BMM150 (in BHI260AP) | Qi coil ferrite | > 10 mm | Magnetic interference (H08) |
| BMM150 | UHF RFID antenna | > 10 mm | RF coupling through ground (H12) |
| BMM150 | DW3000 antenna (v2) | > 15 mm | UWB antenna near-field |
| UHF RFID antenna | PCB edge | < 2 mm | Antenna radiation pattern |
| LDC1612 coil | Copper planes | > 5 mm | Eddy current interference |

---

## 10. Interconnection Summary

| Interface | Devices | Pins | Protocol | Speed |
|-----------|---------|------|----------|-------|
| BHI SPI1 | BHI260AP, Flash U7 | P0.03–05 (SCK/MOSI/MISO), P0.26 (Flash CS), P0.31 (BHI CS) | SPI Mode 0 | ≤ 8 MHz |
| User SPI | RFID (v2), UWB (v2) | P0.11 (SCK), P0.27 (MOSI), P0.28 (MISO), P0.20 (RFID CS), P0.29 (UWB CS) | SPI Mode 0 | ≤ 8 MHz |
| I2C0 / Wire | LDC1612 | P0.22 (SDA), P0.23 (SCL) | I²C Fast-mode | 400 kHz |
| I2C1 / Wire1 | RGB LED, BQ25120A | P0.15 (SDA), P0.16 (SCL) | I²C Fast-mode | 400 kHz |
| BLE | Phone | nRF52 internal RF | BLE 5.0, LE 2M PHY | ≤ 2 Mbps |
| BHI260AP INT | nRF52832 | P0.14 | GPIO rising edge | Managed by BHY2 |
| LDC1612 INTB | nRF52832 | P0.02 | GPIO rising edge | Wake from SLEEP |
| RGB LED strip | 5× SK6812-mini (custom PCB) | P0.19 | Single-wire NZR | 800 kHz |
| Boost EN | MT3608 EN pin | P0.24* | GPIO OUT | HIGH = 5V active |
| Beeper PWM | Piezo transducer | P0.09 | GPIO PWM | ~4 kHz |
| Qi detect | Qi charger presence | P0.10 | GPIO IN | On-demand |

*P0.24 is also used as RFID_EN in v2. For v1, it controls boost EN.
In v2, RFID_EN takes P0.24 and boost EN must move to another free GPIO (P0.20 or P0.29 when v2 not populated).
| RFID EN (v2) | MOSFET gate | P0.24 | GPIO OUT | HIGH = ON |
| UWB PWR (v2) | MOSFET gate | P0.30 | GPIO OUT | HIGH = ON |

---

## 11. Traceability Matrix

| Hardware Requirement | Architecture Section | Implementation (BOM / PCB) |
|----------------------|---------------------|---------------------------|
| H01 (−20°C to +40°C) | §7 Power, §8 Enclosure | LDOs + MCU rated −40°C; Li-Po derated |
| H02 (≥ 8h at −10°C) | §7 Session Budget | 600 mAh → ~11 sessions without RFID (v1) |
| H03 (IP67 sealed) | §8 Enclosure | Polycarbonate shell, no ports, surface transducer |
| H04 (inductive through shell) | §3 LDC1612, §8 Target disc | Passive copper/iron disc in opposite strap — cross-arm proximity, no moving parts |
| H05 (< 16 mm thick) | §8 Enclosure | PCB 0.8mm + battery + Qi coil stack |
| H06 (≤ 40 g) | BOM | ~25 g electronics + 10 g enclosure + strap |
| H07 (≥ 10 runs Flash) | §1 MX25R1635F | 2 MB → ~18 runs compressed |
| H08 (no ferromagnetic near BMM150) | §9 Keepout Zones | > 10 mm Qi coil, > 10 mm RFID antenna |
| H09 (200 g shock) | §8 Enclosure | Internal potting, 0.8 mm PCB |
| H10 (Qi charging, IP67) | §7 Power Tree | Qi coil → BQ25100, sealed enclosure |
| H11 (UHF RFID module) | §5 RFID Subsystem | Impinj E310 module, SPI, ceramic antenna |
| H12 (RFID not interfere with BMM150) | §5 Antenna Keepout | > 10 mm separation + ground plane |
| H13 (DW3000 unpopulated footprint) | §6 UWB Footprint | QFN pad + CS + VDD MOSFET, no IC loaded |

---

*Next: PCB schematic capture → layout → prototype fabrication. Enclosure CAD from PCB outline. BMM150 calibration jig design.*
