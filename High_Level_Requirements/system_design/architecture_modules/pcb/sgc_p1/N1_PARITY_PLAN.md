# N1 = "boosted Nicla" — parity audit vs official ABX00050 + multi-sheet plan

**JP directive 2026-11-04:** our module must be **the Nicla plus** {bigger ANNA, bigger flash,
5 V DC/DC for the LED strip, LED-strip connector} and **identical to the Nicla for everything
else** — to avoid weeks of prototype debug loops. Structure: **many pages, one per function**,
like Arduino's own schematic. Use the Arduino KiCad library where a Nicla-exact part exists.

Source of truth reviewed: `reference/ABX00050-schematics.pdf` (official, Rev V1.0, 2022-11-25,
7 sheets) + text extraction `reference/abx.txt`.

---

## 1. Official Nicla Sense ME — what it actually contains (sheet by sheet)

| # | Sheet | Contents |
|---|-------|----------|
| 1 | **TOP** | hierarchical block overview only (Power_bq, ANNAB112_BQ, Sensors, LevelShifters, Connectors, USB Bridge) |
| 2 | **CONNECTORS** | J5 ESLOV 5-pin (SM05B-SRSS-TB) + R36/R37 10k, R38/R39 0R · J2 battery (VIN_BQ25120/VBAT/NTC) · J1 2×10 header (JTAG_SAMD11, JTAG_ANNA, JTAG_BHI, SPI1_EXT, I2C1_EXT, LPIO_EXT, ADC0/ADC1 with R19/R20 499) · J4 3-pin (BM03B-ACHSS) · J7 USB micro ZX62-AB-5PA · pushbutton **PB1** (PTS830GM140SMTRLFS) + D1 PMEG4005EPK |
| 3 | **LEVEL SHIFTERS** | U11 TXB0108YZPR (8-ch, LPIO/SPI1) · U12 NTS0304EUKZ (I2C1) · U13 NTS0304EUKZ (SAMD11 PA02/PA03) · C23–C27, R40/R41 0R |
| 4 | **POWER** | U9 **BQ25120AYFPR** (charger + power path + I²C + LDO) · straps: ISET 4.99k, ILIM 4.02k, IPRETERM 499, TS 10k + 4.02k NTC divider, CD 100k PU, INT 100k PU, MR 1 µF, LSCTRL 100k, VINLS 100k · L1 MLZ1608A2R2WT000 2.2 µH · C14 1 µF (PMID), C10 4.7 µF, C15 10 µF, C1 1 µF, C21 1 µF · D3/D4/D5 PMEG4005EPK reverse-protection diodes (VBUS / VESLOV) · rails: **VBUS_USB → BQ25120A → SYS / PMID / VBAT**, **LS/LDO → +1V8** (single 1.8 V rail), **VDDIO_EXT** |
| 5 | **USB (SAMD11)** | U1 ATSAM-D11-D14A-MUT USB-serial bridge + U14 **AP2112K-3.3** LDO → +3V3 · U13 level translator · C18/C19 100nF, C20/C28 1 µF, R28/R30/R34/R35 100k |
| 6 | **SENSORS** | U5 **BHI260AP** (QSPI0 to its own flash, I2C1/SPI1 to host, 32k XO, HOSTBOOT strap R23 10k, R21 10k, C9 220 nF on VREG) · U3 **BMP390L** (I2C1 + SPI shared) · U4 BMM150 (DNP/magnetometer) · U2 **BME688** (DNP) · U6 **MX25R1635FZUIH0 = BHI program flash** · caps: C5 1 µF, C6/C7/C12/C13 100 nF, C3/C4/C8/C10/C11/C17 100 nF |
| 7 | **ANNAB112** | MD1 **ANNA-B112** + U7 **MX25R1635FZUIH0 = data-logging flash** · U8 **IS31FL3194-CLS2-TR** + DL1 **SMLP34RGB2W3** RGB · Y1 **SIT1532AI-J4-DCC-32.768E** 32.768 kHz XO (XL1 driven, XL2 → GND) · R7/R12 100k, R6/R9 10k, R31 0R, C17 100 nF, C2 100 nF |

### Nicla's real power tree
```
USB-C VBUS ──[D3 PMEG4005]──┐
                            ├─► BQ25120A (IN) ─► SYS ─┬─► LS/LDO ─► +1V8 ─► ANNA-B112, BHI260AP, BMP390, IS31, flashes
Battery ────────────────────┘        (VINLS)          └─► PMID ─► (system peripherals, RGB LED anodes)
```
No boost converter on the Nicla. No 3.3 V rail except the SAMD11's own AP2112K. One 1.8 V rail.

---

## 2. Our SGC P1 vs Nicla — component parity

| Function | Nicla ABX00050 | SGC P1 (current) | Verdict |
|---|---|---|---|
| MCU module | ANNA-B112 (nRF52832, 64 KB RAM) | **ANNA-B402** (nRF52833, 128 KB RAM, open CPU) | ✅ **bigger, as ordered** |
| BHI260AP program flash | MX25R1635F (2 MB) | U2 MX25R1635F (2 MB) | ✅ identical |
| Data-logging flash | MX25R1635F (2 MB) | U7 **MX25R6435F (8 MB)** | ✅ **4× bigger, as ordered** |
| Charger / PMIC | BQ25120A | BQ25120A (U3) | ✅ identical |
| Fuel gauge | — | MAX17048 (U4) | ➕ our addition |
| IMU + pressure | BHI260AP + BMP390L | BHI260AP (U8) + BMP390 (U9) | ✅ identical |
| RGB status LED | IS31FL3194 + SMLP34RGB2W3 | IS31FL3194 (U5) + SMLP34RGB2W3 (D1) | ✅ identical |
| 32.768 kHz XO | **Y1 SIT1532AI** on XL1/XL2 | ❌ **MISSING — net `32K_XL1` has no driver** | 🔴 **GAP** |
| USB bridge | SAMD11 + AP2112K 3V3 + level shifters | none (SWD header J3 only) | ⚠️ decision |
| External IO | ESLOV J5 + TXB0108 + NTS0304EUKZ ×2 | none | ⚠️ decision |
| Arming button | PB1 on-board pushbutton | J5 off-board Langir 16 mm piezo | ✅ deliberate (JP: pressure sensor, water-tight) |
| 5 V rail | — | MT3608 + L2 + D2 SS34 + TXS0102 + J4 (all DNP) | ➕ **our addition, as ordered** |
| Proximity | — | LDC1612 (U6) | ➕ our addition |
| USB connector | Micro-USB ZX62-AB-5PA + ESD | USB-C USB4085 + 5.1 k Rd | ✅ modernised |
| Reverse-protection diodes | D3/D4/D5 PMEG4005 on VBUS/VESLOV | ❌ none seen on VBUS_USB → BQ IN | ⚠️ check |

### 🔴 Real parity gaps found (must fix)
1. **Y1 32.768 kHz oscillator absent.** Nicla drives ANNA XL1 from a SIT1532AI and grounds XL2.
   Our netlist has `32K_XL1` on U1 pin 17 with **zero** driver parts (`grep ref="Y1"` = 0 matches) —
   a dangling net and a parity break. Fix: add **Y1 SIT1532AI-J4-DCC-32.768E** (1.8 V) + 100 nF decap,
   XL2 (pin 18) → GND. Same as Nicla sheet 7.
2. **VBUS reverse-protection diode** — Nicla puts a PMEG4005 in series between USB VBUS and BQ25120A IN.
   Ours goes straight in. Also protects against back-feed on the 5 V boost node if the strip is powered externally.
3. **BHI260AP VREG cap value** — Nicla uses **220 nF** (C9); we now have C20 220 nF ✅ (fixed 2026-11-03),
   good — that's exactly the parity value DS asked for.

### ⚠️ Charger strap deltas (values differ from Nicla — verify before cloning)
| Strap | Nicla | Ours | Effect |
|---|---|---|---|
| ISET | 4.99k | 1.82k | charge current target differs (we designed ~110 mA) |
| ILIM | 4.02k | 1k | input current limit differs |
| IPRETERM | 499 Ω | 4.99k | termination threshold differs (1 % vs 10 %) |
| TS/NTC | 10k + divide from 4.02k | pack NTC direct on J2.2 | different NTC network |

These are deliberate (our pack is a 150 mAh-class cell, not Nicla's 3.7 V LiPo of a different size),
but JP should confirm the targets once we clone the POWER page.

---

## 3. Proposed sheet structure for N1 (Arduino style, one function per page)

```
sgc_n1.kicad_sch              (root, hierarchical)
├── 01_top.kicad_sch           TOP — block diagram + rail index (mirrors Arduino TOP)
├── 02_power.kicad_sch         POWER — BQ25120A, straps, PMID/VBAT/+1V8, MAX17048, protection diodes
├── 03_mcu.kicad_sch           MCU — ANNA-B402/B412 + Y1 32k XO + decoupling + NFC + SWD + USB pins
├── 04_sensors.kicad_sch       SENSORS — BHI260AP + BMP390 + LDC1612 + bypass caps + host I2C/SPI
├── 05_storage.kicad_sch       STORAGE — MX25R6435F (BHI QSPI program) + MX25R6435F 8 MB (logging)
├── 06_boost_led.kicad_sch     BOOST + LED — MT3608, L2, D2, TXS0102, J4 strip, SK6812 reserve
└── 07_connectors.kicad_sch    CONNECTORS — USB-C, J3 SWD, J2 battery, J5 piezo, test points
```

Kept from today's work: page 2 (`led_reserve`) becomes `06_boost_led` verbatim — zero wires,
label-style, netlist-identical (JP's style rule). `01_top` is a pure hierarchy sheet (no parts),
like Arduino's.

**Safety net for every split step:** `netdiff.py` must report **IDENTICAL MEMBERSHIP** after each
page move. Any membership change = the split broke a net, revert immediately.

---

## 4. Decisions needed from JP

1. **USB bridge**: keep our SWD-only design (J3 programming header), or add the Nicla SAMD11 +
   3V3 LDO + level shifters for USB serial/programming parity? → *This is the single biggest
   "identical to Nicla" item we deliberately dropped.*
2. **ESLOV / external IO**: dropped for good, or reserved as a v2 footprint (like the LED strip was)?
3. **Flash**: confirm 8 MB (MX25R6435F) on the logging slot, 2 MB (MX25R1635F) on the BHI program slot.
4. **Boost population**: keep DNP for rev A, or populate now since the strip is a product feature?
5. **Arduino KiCad library**: JP says he added it — I could not find it on the relay or on JP-PC
   (`*.kicad_sym` matching arduino/nicla/anna: none; no global sym-lib-table). Need the path/folder.
