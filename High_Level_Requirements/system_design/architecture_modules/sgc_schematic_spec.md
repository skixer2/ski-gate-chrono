# SGC — P1 Custom Board Schematic Specification (v0.1)

*Doc: `sgc_schematic_spec.md` · v0.1 · 2026-10-29 · **JP GO 2026-10-29 06:56 UTC** ("design schematics so I can create the layout and ask for quotes")*
*Owner: Lead Systems Coordinator · Status: for JP review → schematic capture · No firmware code changed in this pass (pin moves flagged for the Zephyr overlay/config in §3.3).*

---

## 0. Sources of truth & verification method

**Rule applied: no invented pin numbers.** Every module/IC pin below is quoted from the referenced datasheet or schematic. Fetch locations on the gateway (all fetched 2026-10-29):

| Fact source | Doc | Gateway path |
|---|---|---|
| u-blox **ANNA-B402 data sheet**, UBX-20032372 **R06** (pin table §3.1 Table 4, pp. 18–20; power §2.1.1; antenna §2.2.1; LFXO §2.4; USB §2.5.5) | C1-Public | `/tmp/anna-b402.pdf` / `.txt` (fetched from `content.u-blox.com/sites/default/files/ANNA-B402_DataSheet_UBX-20032372.pdf`) |
| u-blox **ANNA-B4 System integration manual**, UBX-21000517 **R08** (antenna options Table 4 p.13, internal-antenna reference design Appendix B pp. 69–70, VCC app circuits §3.4.1) | C1-Public | `/tmp/anna-b4-sim.pdf` / `.txt` |
| TI **BQ25120A**, SLUSD08A (YFP DSBGA ball map §7; ISET/ILIM/IPRETERM Tables 3–5, §9.3.13–14; TS/NTC §9.3.16 Fig. 18 Eq. 1–2; VBATREG default) | | `/tmp/bq25120a.txt` (pre-existing) |
| Analog Devices/Maxim **MAX17048/MAX17049** datasheet (Pin Description table, TDFN-8; "MAX17048: Not internally connected" for CELL) | | PDF fetch blocked (analog.com bot-wall); pin table + address captured verbatim from the analog.com datasheet text via web search extraction — re-verify at schematic review (OD-12) |
| Lumissil **IS31FL3194** datasheet, Rev. C 2024-03-27 (WLCSP-8 pin config; fixed 7-bit address "101 0011" = 0x53; VIH ≤ 1.4 V) | | `/tmp/is31fl3194.pdf` / `.txt` |
| Bosch **BHI260AP**, BST-BHI260AP-DS000-02 rev 1.1 (44-pad LGA 3.6×4.1×0.83 mm; pad table §pin description; VDDIO 1.71–1.89 V) | | `/tmp/bhi260ap.pdf` / `.txt` |
| Bosch **BMP390**, BST-BMP390-DS002-07 rev 1.7 (10-pin LGA 2.0×2.0 mm; CSB→VDDIO = I²C mode) | | `/tmp/bmp390.pdf` / `.txt` |
| Macronix **MX25R6435F**, Wide Range 64 Mb, v1.6 (8-pin SOP 200 mil pin config; internal pull-ups on NC WP#/HOLD#) | | via web-search extraction of `macronix.com .../MX25R6435F, Wide Range, 64Mb, v1.6.pdf` |
| TI **LDC1612** (WSON-12 pin map; ADDR=L → 0x2A; INTB push-pull) | | `/tmp/ldc1612.pdf` / `.txt` |
| GCT **USB4085-GF-A** drawing (16-pin USB-C 2.0 mid-mount; A5=CC1, B5=CC2, A4/A9/B4/B9=VBUS, shell=GND) | | `/tmp/usb4085.pdf` / `.txt` |
| Arduino **Nicla Sense ME schematic**, ABX00050-schematics.pdf (BQ25120A app values; U2 "BHI Flash" + U7 "Data Logging Flash"; Y1 = SIT1532AI-J4-DCC-32.768E; IS31 U8 = IS31FL3194-CLS2-TR + SMLP34RGB2W3) | | `/tmp/nicla_sch.pdf` / `.txt` (from `docs.arduino.cc/resources/schematics/ABX00050-schematics.pdf`) |
| Arduino **Nicla Sense ME full pinout**, ABX00050-full-pinout.pdf (VDDIO_EXT = BQ LS/LDO, software-programmable 1.8–3.3 V; LPIO/UART/I2C header mapping) | | manuals.plus mirror text (web search extraction) |
| Our architecture docs | | `sgc_architecture_hardware.md` v2.5 · `sgc_bom.md` v4.3 · `sgc_architecture_decisions.md` (AD-019, AD-029) · `sgc_production_strategy.md` v1.7 · `sgc_usb_c_charging_spec.md` v1.1 · Firmware_Zephyr `apps/03_pull_proto/app.overlay` |

`TBD-JP` = not verifiable from these sources; do not guess — decide during capture/layout.

---

## 1. Scope

P1 **standalone custom PCB** (first non-Nicla hardware spin), based on the **u-blox ANNA-B402** open-CPU module (nRF52833) per **AD-019** — no companion-carrier phase. The board is a **strict replica of the proven Nicla Sense ME sensor set** (BHI260AP + BMM150 + BMP390L + BME688 + dual MX25R flash + IS31FL3194 RGB + BQ25120A power) with the SGC deltas:

- **Fuel gauge MAX17048 @ 0x36** on the power bus (AD-029; BQ band-gauge demoted to fallback)
- **USB-C charging** (GCT USB4085 → BQ25120A IN) replacing Qi (dropped v4.2)
- **No beeper** (DNP footprint retained, v4.2) · **no Qi** (removed)
- Battery: Renata Li-Po, **3-wire NTC** pack (BAT+, GND, NTC), user-replaceable JST-3
- SK6812-mini ×5 status strip on P0.19 (SGC peripheral) + 5 V boost for full brightness (P1 behavior per hardware doc §7)
- v2 footprints retained, unpopulated: LDC1612 + coil, RV-3028-C7 RTC, user-SPI header nets, UWB/RFID rails

Everything else mirrors the Nicla: **+1.8 V single system rail from the BQ25120A buck**, all logic at 1.8 V, I²C pull-ups to +1.8 V (Nicla parity — Nicla schematic shows 100 kΩ pull-ups on the power bus to +1V8).

---

## 2. Block diagram & bus map

```
                 USB-C (GCT USB4085, Rd 5.1k×2, ESD)
                        │ VBUS 5 V (+10 µF bulk)
                        ▼
                 ┌──────────────┐  ISET=1.82k→ICHG 110 mA, ILIM=1k→200 mA
                 │  BQ25120A    │  TS ← NTC (3-wire pack)   IPRETERM 4.99k
                 │  (YFP-25)    │  CD ← P0.08 (100k PU V_PMID)   [OD-8]
                 └──┬───┬───┬───┘
              BAT ◄─┘   │   └─ SYS(buck) = +1V8 RAIL (300 mA, default 1.8 V)
                        │ PMID (V_PMID)
        ┌───────────────┼──────────────────────────────────────────┐
        │               │                                          │
        ▼               ▼                                          ▼
 ┌─────────────┐  ┌─────────────┐                     MT3608 boost → +5 V
 │ Renata LiPo │  │  MAX17048   │                          │
 │ 600 mAh 3.7V│  │ gauge 0x36  │                     SK6812 ×5 strip
 │ JST-3 + NTC │  │ VDD=VBAT    │                     data: P0.19 → level
 └─────────────┘  └─────────────┘                     translator → DIN

 +1V8 RAIL ──► ANNA-B402 VCC (module-internal DC/DC, per datasheet §2.1.1)
           ──► BHI260AP VDD/VDDIO + BMP390L + BME688 + BMM150 (Nicla cluster)
           ──► U7 MX25R6435F (data flash) + U2 MX25R1635F (BHI FW flash, QSPI)
           ──► I²C pull-ups + Y1 32k MEMS osc (SIT1532AI, Nicla-parity)
 V_PMID ───► IS31FL3194 VCC (Nicla parity) ── OUT1/2/3 ──► RGB LED (SMLP34RGB2W3)

 ANNA-B402 buses (nRF ports; Zephyr names per board file + app.overlay):
 ├─ &spi2 (P0.03 SCK / P0.04 MOSI / P0.05 MISO) ── U7 flash CS P0.26 ─┐
 │                                              └─ BHI260AP CS P0.31 ──┤ H-IF SPI
 │   BHI HIRQ → P0.14 · BHI RESETN → P0.13 (moved, see §3.3)          │
 ├─ &i2c0 power bus (P0.15 SDA / P0.16 SCL, PU +1V8):
 │     0x36 MAX17048 · 0x53 IS31FL3194 · 0x6A BQ25120A
 ├─ &i2c1 sensor bus (P0.22 SDA / P0.23 SCL):
 │     0x2A LDC1612 (DNP, v2) · 0x52 RV-3028-C7 (DNP)
 ├─ GPIO: P0.02 BUTTON (piezo, edge int; v2: LDC INTB) · P0.19 SK6812
 │        P0.17 boost EN · P0.08 BQ CD · P0.13 BHI RESET · P0.21 /ALRT
 ├─ UART console TP: TX P0.20 / RX P0.09 (Nicla LPIO1/LPIO2 parity)
 ├─ SWD: SWDCLK pin 41 / SWDIO pin 42 (+ optional SWO P1.00)
 └─ Module USB (VBUS 21 / USBDP 34 / USBDM 35): NOT USED — NC
```

**Bus-map derivation (strict, per docs):** Zephyr `&i2c0` = P0.15/P0.16 = power-management bus ("Wire1" in Arduino naming) — gauge 0x36 (AD-029), LED 0x53, charger 0x6A. Zephyr `&i2c1` = P0.22/P0.23 = sensor bus ("Wire") — LDC1612 0x2A (ADDR=GND, DNP) + RV-3028 0x52 (DNP). BHI260AP is **SPI**, not I²C (app.overlay: `&spi2`, CS P0.31, 16 MHz, INT P0.14); BMP390/BMM150/BME688 are BHI260AP **sub-sensors on the BHI's M1 master port** (Nicla schematic — the task brief's "BMP390 I²C addr" does not apply; per the ABX00050 pinout, BMP390L rides BHI M1 with MCSB). Address map has no conflicts (AD-029 address table confirmed).

---

## 3. ANNA-B402 pin assignment

### 3.1 Availability verdict

All Nicla-replica signals exist on the ANNA-B402 **except nRF P0.24 and P0.25** (absent from Table 4). Module reset is **P0.18** (fixed pin 12), which conflicts with the Nicla's use of P0.18 as BHI260AP RESET. Two signals therefore move; see §3.3. 33 GPIOs are available (datasheet §3, "ANNA-B402 modules have 33 GPIO pins"); I/O levels = VCC (SIM §2.1.1: "the I/O voltage level is the same as the supply voltage and VCC_IO is internally connected to the supply input VCC") → **1.8 V logic** on our rail.

### 3.2 Pin assignment table (B402 pin data verbatim from Table 4, UBX-20032372-R06 pp. 19–20)

| B402 No. | Name (Table 4) | nRF port (Table 4) | SGC function | Notes |
|---|---|---|---|---|
| 9 | VCC | — | +1V8 system rail input | "Module supply voltage input, 1.7-3.6 V range" — DC/DC integrated (§2.1.1) |
| 12 | RESET_N | P0.18 | Module reset → SWD conn + TP | "System reset input, Active low" (fixed, red pin) — ⚠ Nicla-parity P0.18 use impossible, §3.3 |
| 17 | XL1/GPIO_17 | P0.00 | 32.768 kHz from Y1 | "Connection for 32.768 kHz crystal (LFXO)" |
| 18 | XL2/GPIO_18 | P0.01 | NC/GND per clock source | Datasheet: ground XL1+XL2 if unused; with external clock: "Apply external low swing signal to XL1, ground XL2" (SIT1532AI is a single-ended osc → XL2 GND, Nicla parity TBD-JP confirm vs Y1 datasheet) |
| 19 | GPIO_19 | P0.03 | &spi2 SCK (BHI + U7) | ⚠ Table 4 remark: "standard drive, low-frequency I/O only" — see §3.3 risk R1 |
| 20 | GPIO_20 | P0.02 | BUTTON (piezo, edge int) / v2 LDC INTB | analog-capable, "standard drive, low-frequency I/O only" — fine for button |
| 21 | VBUS | — | **NC** | "USB Power input (5V). Must be connected to 5 V for the USB interface to work" — module USB unused |
| 22 | NFC1/GPIO_22 | P0.09 | UART RX TP / beeper (DNP) | NFC pin warning: never apply an NFC field to NFC pins in GPIO mode (§3.1) — no NFC coil on board |
| 23 | NFC2/GPIO_23 | P0.10 | Spare (NC) | Qi-detect pin retired with Qi |
| 24 | GPIO_24 | P0.05 | &spi2 MISO (BHI + U7) | analog capable |
| 25 | GPIO_25 | P0.04 | &spi2 MOSI (BHI + U7) | analog capable |
| 26 | GPIO_26 | P0.21 | **MAX17048 /ALRT** (10 kΩ PU +1V8) | "General purpose I/O" — free on B402 (B112 used P0.21 as module reset; B402 does not) |
| 27 | GPIO_27 | P0.31 | BHI260AP CS | analog capable, "standard drive, low-frequency I/O only" — CS duty is fine |
| 28 | GPIO_28 | P0.30 | v2 UWB_PWR (NC v1) | "standard drive, low-frequency I/O only" |
| 29 | GPIO_29 | P0.29 | v2 UWB_CS (NC v1) | "standard drive, low-frequency I/O only" |
| 30 | GPIO_30 | P0.28 | v2 user-SPI MISO (NC v1) | "standard drive, low-frequency I/O only" |
| 31 | GPIO_31 | P0.27 | v2 user-SPI MOSI (NC v1) | General purpose I/O |
| 32 | GPIO_32 | P0.06 | Spare | General purpose I/O |
| 33 | GPIO_33 | P0.26 | U7 flash CS | General purpose I/O |
| 34 | USBDP | — | NC | Module USB unused |
| 35 | USBDM | — | NC | Module USB unused |
| 36 | GPIO_36 | P0.16 | &i2c0 SCL | General purpose I/O |
| 37 | GPIO_37 | P0.22 | &i2c1 SDA | General purpose I/O |
| 38 | GPIO_38 | P0.19 | SK6812 ×5 data | ⚠ Table 4 remark: "Standard drive, low frequency I/O only" — see §3.3 risk R2 |
| 39 | GPIO_39 | P0.23 | &i2c1 SCL | "Standard drive, low frequency I/O only" — 400 kHz I²C acceptable; note R1-class caution |
| 40 | GPIO_40 | P0.15 | &i2c0 SDA | General purpose I/O |
| 41 | SWDCLK | — | SWD connector | "Serial wire debug clock input for debug programming" |
| 42 | SWDIO | — | SWD connector | SWD I/O |
| 43–46, 53–56 | GND | — | GND | 53–56: "exposed pins in the center of the module should be connected to GND" |
| 47 | GPIO_47 | P1.07 | Spare | "Standard drive, low frequency I/O only" |
| 48 | GPIO_48 | P0.07 | Spare (trace clk) | |
| 49 | GPIO_49 | P0.17 | **Boost EN** (moved from P0.24) | "Standard drive, low frequency I/O only" — fine for a slow EN |
| 50 | GPIO_50 | P0.08 | **BQ25120A CD** (moved from P0.25) | General purpose I/O — see OD-8 |
| 51 | GPIO_51 | P1.01 | Spare | "Standard drive, low frequency I/O only" |
| 52 | GPIO_52 | P0.13 | **BHI260AP RESETN** (moved from P0.18) | General purpose I/O — see OD-7 |
| 10 | GPIO_10 | P0.20 | UART TX TP / v2 RFID_CS | General purpose I/O |
| 11 | GPIO_11 | P0.14 | BHI260AP HIRQ | General purpose I/O |
| 13 | GPIO_13 | P1.09 | Spare | |
| 14 | GPIO_14 | P0.11 | v2 user-SPI SCK (NC v1) | |
| 15 | GPIO_15 | P0.12 | Spare | |
| 16 | SWO/GPIO_16 | P1.00 | SWO → SWD connector (optional) | "Serial Wire debug trace data output" |
| 1 | ANT_PCB | — | antenna counterpoise net | corner placement → "GND pattern"; middle-of-side → NC (SIM Table 4) |
| 2, 3 | ANT_GND1, ANT_GND2 | — | antenna counterpoise net | middle-of-side → "GND pattern"; corner → NC (SIM Table 4) |
| 5 | ANT_INT | — | bridge to pin 6 | "Connect to pin 6 – ANT" for internal antenna |
| 6 | ANT | — | bridge to pin 5 | 50 Ω interface; internal-antenna bridge |

**Not available on the module (Table 4 complete):** nRF **P0.24, P0.25** do not appear in any row. Everything else we use is present.

### 3.3 Conflicts, risks, firmware impact

| ID | Finding | Resolution in this spec | FW impact |
|---|---|---|---|
| C1 | **P0.24 absent** — hardware doc §10 assigns Boost EN = P0.24 (v1) / RFID_EN = P0.24 (v2) | Boost EN → **P0.17** (module pin 49). v2 RFID_EN → P0.17 likewise (or P0.06/P0.12) | `boost_en` pin change in config/overlay |
| C2 | **P0.25 absent** — hardware doc §2 assigns BQ CD = P0.25 | CD → **P0.08** (module pin 50) + 100 kΩ pull-up to V_PMID so the board always boots active (CD low + battery-only = BQ Hi-Z ship mode = dead board). See OD-8 | `bq_cd` pin change |
| C3 | **Module RESET_N = P0.18** (fixed) vs Nicla's BHI260AP RESET on P0.18 (app.overlay pulses it low at bring-up; hardware doc §2 "P0.18 → BQ25120 MR reset network") | BHI RESETN → **P0.13** (module pin 52), pull-up retained. Do **not** wire anything external to pin 12 except SWD/TP. (The Nicla's "P0.18 → BQ MR" net is dropped — MR unused on P1, see §4.1) | overlay: BHI reset-gpios 18 → 13 |
| R1 | P0.03 (SPI SCK) carries Table 4 remark "standard drive, low-frequency I/O only… avoid using these pins for high-speed digital interfaces… Digital noise on these pins can reduce radio sensitivity" — vs app.overlay's 16 MHz BHI SPI | Keep Nicla-parity routing (function locked to the proven design); mitigation documented: if RF sensitivity regressions appear in bench S03/S04, drop SPI clock (BHI works at 8/4 MHz) and re-test. Decision point at EMC/range bench — JP informed | optional `spi-max-frequency` reduction |
| R2 | P0.19 (SK6812 data, 800 kHz NZR) carries the same "low frequency I/O only" remark | Same treatment as R1; the strip is 3 mm from the antenna zone — also see §4.9 keepout. Fallback alternative: move strip to a full-drive GPIO (P0.06/P0.12) with zero Nicla-parity loss (SK6812 is an SGC peripheral, not a Nicla part) — JP call (OD-9b) | overlay pin change if moved |
| C4 | Nicla's VDDIO_EXT header rail (BQ LS/LDO, programmable 1.8–3.3 V per ABX00050 pinout) has **no consumer on P1** (no external header, no level translators) | LS/LDO unused: LSCTRL → GND (disabled), LS/LDO pin → 1 µF to GND (stability, §7 pin table), VINLS → SYS (per SLUSD08A §9.3: "If the output of the LDO is less than the programmed V(SYS) voltage, connect VINLS to SYS") | none |

---

## 4. Per-subsystem net lists

### 4.1 Charger subsystem — BQ25120A (YFP 25-ball DSBGA; ball map verbatim from SLUSD08A §7)

| Ball | Name | Net / connection |
|---|---|---|
| A2 | IN | VBUS_USB (USB-C VBUS) + **C_IN 1 µF** ("Bypass IN to GND with at least 1 µF") |
| A3, B3 | PMID | V_PMID + **C_PMID ≥3 µF** ("at least 3 µF… as close as possible"; Nicla uses 4.7 µF C10) → IS31FL3194 VCC, CD pull-up rail |
| A1, D5 | GND | GND |
| A5 | PGND | GND (buck hot loop; output cap to this ground per §7) |
| A4 | SW | L1 **2.2 µH** (Nicla: MLZ1608A2R2WT000) → PGND side; SYS sense |
| B5 | SYS | **+1V8 rail** + **C_SYS ≥4.7 µF** (Nicla: C15 10 µF) — buck default **1.8 V** (SYS_VOUT "1.8 V through 3.3 V selection", default per §7 table "BQ25120A Enabled 1.8 V Load Switch") |
| B1, B2 | BAT | VBAT (cell +) + **C_BAT ≥1 µF** |
| C3 | TS | NTC net: **R_HI from VBUS/driver to TS**, **R_LO TS→GND**, pack NTC (TS→GND) per Fig. 18 + Eq. 1–2. R_HI/R_LO values **TBD-JP** (need Renata NTC R25/B constants; JEITA thresholds V(COLD/COOL/WARM/HOT) per datasheet) |
| C1 | ISET | **R_ISET = 1.82 kΩ 1%** → ICHG = K(ISET)/R(ISET) = 200/1820 ≈ **110 mA** (Table 5, KISET typ 200 AΩ; task-given 110 mA) |
| C2 | ILIM | **R_ILIM = 1.0 kΩ 1%** → 200 mA (Table 4 row "0.19/0.2/0.21 A — 1000 Ω") |
| D1 | IPRETERM | **4.99 kΩ** → termination/pre-charge = 10 % of ISET ≈ 11 mA (Table 3 row 10 % → 4990 Ω) |
| D2 | INT | open-drain status → 100 kΩ pull-up to +1V8 → **NC on MCU** (charging bit read from FAULTS reg over I²C, per AD-029 firmware note); TP |
| D3 | RESET | NC (BQ's MR-reset output unused on P1 — Nicla's "P0.18→MR" net dropped, see C3) |
| D4 | /PG | TP only (optional 100 kΩ pull-up to +1V8) |
| E1 | /MR | NC (internal logic handles; ship mode via I²C/CD per OD-8 decision) |
| E2 | /CD | **P0.08** (module pin 50) + **100 kΩ pull-up to V_PMID** — pull-up guarantees active mode at cold boot (CD has internal 900 kΩ pull-down; CD low + battery-only = Hi-Z). OD-8 |
| E3 | LSCTRL | GND (LS/LDO disabled — no consumer, C4) |
| E4 | SDA | &i2c0 SDA (P0.15) |
| E5 | SCL | &i2c0 SCL (P0.16) |
| C4, B4 | VINLS | → SYS (rule §9.3 quoted in C4) + 1 µF |
| C5 | LS/LDO | 1 µF to GND, otherwise NC |

VBATREG note: default register = **4.2 V** (§7 table "4.2 V" battery-regulation default; VBREG equation "VBATREG = 3.6 V + VBREGCODE × 10 mV", charge range 3.6–4.65 V) — correct for the cell; no divider/resistor needed.

### 4.2 Fuel gauge — MAX17048G+T10 (TDFN-8; pin functions verbatim from ADI datasheet Pin Description table)

| Pin | Name | Connection |
|---|---|---|
| 1 | CTG | GND ("Connect to Ground") |
| 2 | CELL | GND ("MAX17048: Not internally connected") |
| 3 | VDD | **VBAT** ("MAX17048: Voltage sense input. Connect to positive battery terminal") + **0.1 µF** ("Bypass with 0.1 µF to GND") |
| 4 | GND | GND ("negative battery terminal") |
| 5 | ALRT | "Open-Drain, Active-Low Alert Output" → 10 kΩ pull-up to +1V8 → **P0.21** (module pin 26). Wired for firmware freedom; use v2-optional per AD-029 |
| 6 | QSTRT | GND ("Connect to GND if not used") |
| 7 | SCL | &i2c0 SCL (P0.16) — "I2C Clock Input" (fixed addr **0x36**, per ADI datasheet/AD-029) |
| 8 | SDA | &i2c0 SDA (P0.15) |
| EP | — | GND |

**Note (OD-12):** verify MAX17048 VIH accepts 1.8 V pull-ups with VDD=VBAT (datasheet fetch was bot-blocked; pin table above is from the ADI datasheet text). BQ25120A VIH = 0.825 V and IS31FL3194 VIH ≤ 1.4 V are already 1.8 V-safe.

### 4.3 RGB LED (Nicla stock) + SK6812 strip (SGC)

**IS31FL3194-CLS2-TR (WLCSP-8)** + ROHM **SMLP34RGB2W3** LED (exact Nicla BOM, ABX00050 schematic U8/DL1):

| IS31 pin (datasheet Pin Description) | Connection |
|---|---|
| A1 SDB | "Shutdown the chip when pulled to low" → **V_PMID** (always-enabled; control via I²C) — Nicla SDB net **TBD-JP** (confirm on ABX00050 schematic at capture; do not guess) |
| A2 VCC | **V_PMID** (Nicla parity: U8 powered from PMID; IS31 VCC range 2.7–5.5 V covers 3.0–5.0 V battery/input) + 0.1 µF (value **TBD-JP** — typical-app figure is graphical; confirm vs Lumissil EVB/Nicla) |
| B1 SDA / C1 SCL | &i2c0 (P0.15/P0.16) — fixed 7-bit address **"101 0011" = 0x53**, no strap pins |
| B2/C2/D2 OUT1–OUT3 | SMLP34RGB2W3 R/G/B anodes (current-sink outputs; no resistors) |
| D1 GND | GND |

**SK6812-mini ×5** (SGC strip, P0.19): DIN ← P0.19 **through a 5 V-logic level translator** (boost rail is 5 V; SK6812 VIH ≈ 0.7·VDD — 1.8 V drive is insufficient). Dual-supply translator, VCCA=+1V8/VCCB=+5V, TXB0102/TXS0102-class — **part TBD-JP** (OD-9). Per-LED 0.1 µF; strip supply = +5V boost (hardware doc §7: full brightness for P1+).

### 4.4 Sensor cluster (Nicla replica — copy ABX00050 nets at capture)

- **BHI260AP** (44-pad LGA, 3.6×4.1×0.83 mm): HSCX pad 33 → P0.03 (SCK); HSDX pad 11 → P0.04 (MOSI); HSDO pad 32 → P0.05 (MISO); HCSB pad 34 → P0.31 (CS — "SPI mode"); HIRQ pad 10 → P0.14; **RESETN pad 9 → P0.13** (moved, C3; pad is "Input, Pull-Up, Reset input, active low"). VDD 1.71–3.6 V, **VDDIO 1.71–1.89 V** → both to +1V8 rail (this hard-pins the 1.8 V architecture — no 3.3 V sensor bus possible). Decoupling per Nicla bank: 1 µF + 100 nF ×3–4 (C5/C6/C7/C12/C13).
- **BMM150** (U4) + **BMP390L** (U3) + **BME688** (U6): sub-sensors on BHI260AP master port M1 (BMM150 SCK/SDI ← M1SCX/M1SDX; BMP390L via MCSB1; BME688 via MCSB2 per ABX00050 pinout). **Per-ball wiring = copy Nicla schematic verbatim at capture** — exact M1/M2/M3 pin assignment of the BHI cluster is a Nicla-internal net list; mark as "copy reference" in capture. BMP390L: 10-pin LGA (1 VDDIO, 2 SCK, 3 VSS, 4 SDI, 5 SDO, …, CSB; CSB→VDDIO selects I²C — here SPI to BHI).
- **BME688 populate-vs-DNP**: firmware never reads it → **OD-6** (footprint retained either way; Nicla parity = populate).
- **U2 "BHI Flash" MX25R1635FZUIH0** (Nicla U2, +1V8): BHI260AP QSPI (CLK/CS/D0–D3) — **⚠ missing from BOM v4.3** — BHY2 sensor firmware lives here (Arduino_BHY2 upload path). Must be added (OD-5).

### 4.5 Data flash — MX25R6435F (U7, SOIC-8 200 mil; pin config verbatim from Macronix v1.6)

| Pin | Name | Net |
|---|---|---|
| 1 | CS# | P0.26 (&spi2 hardware CS reg 0) |
| 2 | SO/SIO1 | P0.05 (MISO/CIPO) |
| 3 | WP#/SIO2 | NC (datasheet Note 1: "will remain internal pull up function while this pin is not physically connected") |
| 4 | GND | GND |
| 5 | SI/SIO0 | P0.04 (MOSI/COPI) |
| 6 | SCLK | P0.03 |
| 7 | RESET#/SIO3 or HOLD#/SIO3 | NC (same internal-pull-up note) |
| 8 | VCC | +1V8 + 100 nF |

RawRunStore 8 MB layout per hardware doc (pre-roll 0x0000–0x13FFF etc.) unchanged. U7 supply at +1V8 = Nicla parity (schematic shows U7 on +1V8).

### 4.6 DNP footprints (v2)

- **LDC1612** (WSON-12): SCL 1 / SDA 2 / CLKIN 3 / ADDR 4 → GND (**0x2A**, "when ADDR=L, I2C address = 0x2A") / INTB 5 → BUTTON net P0.02 ("Push-pull output; does not require pull-up") / SD 6 → GND / VDD 7 (+100 nF) / GND 8 / IN0A 9, IN0B 10 (coil ch.0; 14 mm spiral, footprint retained) / IN1A 11, IN1B 12 → GND or 2nd coil pads. Sensor-bus (&i2c1) placement per docs.
- **RV-3028-C7 RTC @ 0x52** on &i2c1: 7-pin C7 footprint, /INT → TP (unconnected test point, no GPIO steal — per hardware doc §2 I²C map + TOOLS notes). Exact pad map per Micro Crystal datasheet at capture (DNP — not fetched this pass).
- Beeper BZ1 (DNP): P0.09 via 100 Ω/NPN footprint retained.

### 4.7 USB-C — GCT USB4085-GF-A (16-pin; pins verbatim from GCT drawing)

| USB-C pin(s) | Net |
|---|---|
| A4, A9, B4, B9 | VBUS → VBUS_USB: **10 µF bulk at connector** (sgc_usb_c_charging_spec §1) + ESD (OD-13: PESD5V0U2UT vs TPD4E05U06) → BQ IN (+1 µF at ball) |
| A1, A12, B1, B12 + SHELL | GND (shell stitched to GND; TH shell stakes soldered for pull-out strength) |
| A5 (CC1) | 5.1 kΩ 1% → GND (Rd sink) |
| B5 (CC2) | 5.1 kΩ 1% → GND |
| A6/A7, B6/B7 (D+/D−) | NC (or future TP per spec §1) |
| A8/B8 (SBU1/2) | NC |

USB-IF note (BQ §9.3.13): inrush compliance assumes device input capacitance < 10 µF — our 10 µF bulk + 1 µF IN exceeds it → fine for our USB-A→C/legacy sourcing, note in quote package (OD-14).

### 4.8 Connectors & test points

- **Battery JST-3** (VBAT / GND / NTC): part **TBD-JP** — BOM v4.1 flags the connector provisional, varies with final cell pack. Pinout per ABX00050 pinout PDF (battery connector = VBAT/NTC/GND).
- **SWD**: SWDCLK (pin 41), SWDIO (pin 42), SWO (pin 16, optional), RESET_N (pin 12), VCC(+1V8) sense, GND. Style OD-2: Tag-Connect TC2050 footprint vs 2×5 1.27 mm Cortex Debug (EVK uses a 6-pin J-link needle adapter — either is defensible).
- **UART console TP**: TX P0.20 (module pin 10), RX P0.09 (module pin 22) — Nicla LPIO1/LPIO2 parity (ABX00050 pinout: "2/LPIO1/TX P0_20, 1/LPIO2/RX P0_09").
- **TP set** (OD-3): +1V8, +5V, VBAT, V_PMID, GND, BQ INT, SWD row, UART row, ALRT, LED-data.

### 4.9 Mechanical / antenna keepout (internal chip antenna; SIM UBX-21000517-R08 quotes)

- SIM Table 4 (p.13), internal antenna: pin 5 ANT_INT "Connect to pin 6 – ANT"; corner placement → pin 1 ANT_PCB = "GND pattern", ANT_GND1/2 NC; middle-of-side → pins 2+3 ANT_GND1/2 = "GND pattern", ANT_PCB NC. **Recommendation: module in a PCB corner** ("a corner location offers slightly better antenna performance", SIM §2.2.1).
- Keepout rule (SIM Appendix B, p.70, verbatim): "It is important not to route any traces in any layer in the antenna strip clearance area." — antenna strip = module edge + counterpoise region per SIM Figures 33–36; **no traces/vias in any layer** under/around the strip; counterpoise trace per the reference design.
- Certification caution (SIM App. B): "PCB layout must be identical to that provided by u-blox… The designer must use the stack-up provided by u-blox. RF traces on the carrier PCB are part of the certified design." → our 0.8 mm 4-layer stackup must be checked against the SIM reference stackup; if it differs, follow SIM App. B exactly or contact u-blox (flag in §8).
- Board outline: 22 × 55 mm, 4-layer, 0.8 mm, ENIG (hardware doc §9). BHI260AP antenna-adjacent and BMM150 keepouts (>10 mm from coil/RFID footprints) per hardware doc §9 table. SK6812 strip along top edge; button/LED face outward (§8).
- datasheet note: "The IC should be placed far away from the mobile antenna in order to prevent the EMI" (IS31FL3194 datasheet) — keep U8 clear of the module antenna strip.

---

## 5. Power tree with currents

```
USB-C 5 V ──IN── BQ25120A ──BAT── Renata 600 mAh (3.7 V, NTC 3-wire, JST-3)
   │               │  │ILIM 200 mA      │
   │               │  └ ICHG 110 mA     ├─► MAX17048 VDD (3 µA hibernate typ — AD-029)
   │               │                    └─► (nothing else on VBAT)
   │               ├─PMID──► IS31FL3194 VCC (≤ 3×10 mA LED band, I2C-set)
   │               └─SYS = +1V8 (300 mA buck max) ──► B402 VCC (module DC/DC int.)
   │                                            ──► BHI cluster + U2 + U7 (≤ ~15 mA active)
   │                                            ──► I²C pull-ups, Y1 osc
   └─(via boost, battery-side) MT3608 ── +5V ──► SK6812 ×5 (anim ~8–15 mA; 60 mA worst)
```

- System peaks: BLE TX peak handled by B402's internal DC/DC (datasheet §2.1.1: integrated step-down; battery must handle module peak per SIM §3.4.1); BQ buck 300 mA ceiling ≫ our ~25 mA ARMED/LOGGING budget (hardware doc §7 power table).
- Charge + system simultaneously: 110 mA (ICHG) + ~25 mA (system) + LEDs ~15 mA @5V→~23 mA battery-side ≈ 158 mA < ILIM 200 mA ✓ (with USB present the buck loads are served from VIN via power path, per BQ §9.3 DPPM description).
- Session budget unchanged from hardware doc §7 (~48 mAh/session → ~12.5 sessions @ 20 °C; gauge adds ~3 µA — AD-029).
- Battery capacity: docs say **Renata ICP622540PMT, 600 mAh** (sgc_system_design §746, hardware doc §7) — the task brief's "550 mAh" figure appears nowhere in the repo; **confirm OD-4**.

---

## 6. Decoupling summary (values cited, per-IC)

| Rail/IC | Caps | Source |
|---|---|---|
| BQ25120A IN | 1 µF | SLUSD08A §7 pin A2 |
| BQ25120A PMID | 4.7 µF (≥3 µF req.) | Nicla C10 / §7 ball A3-B3 |
| BQ25120A SYS | 10 µF (≥4.7 µF req.) | Nicla C15 / §7 ball B5 |
| BQ25120A VINLS, LS/LDO | 1 µF each | §7 balls C4/C5 |
| BQ25120A BAT | 1 µF | §7 ball B1-B2 |
| MAX17048 VDD | 0.1 µF | ADI pin-3 description |
| IS31FL3194 VCC | 0.1 µF (**TBD-JP** confirm) | typical-app figure graphical |
| BHI260AP cluster | 1 µF + 100 nF ×3 (+ BMP390/BME688 100 nF each) | Nicla bypass bank C5/C6/C7/C12/C13 (ABX00050 schematic) |
| U7 MX25R6435F VCC | 100 nF | standard practice; Macronix ds has no explicit value (**TBD-JP** keep 100 nF, Nicla parity) |
| U2 MX25R1635F VCC | 100 nF | Nicla parity |
| ANNA-B402 VCC | bulk + HF per **ANNA-B4 SIM §3.4 / reference design** — exact values TBD-JP vs SIM App. B layout | SIM §3.4.1 |
| USB VBUS | 10 µF bulk (+ ESD part OD-13) | sgc_usb_c_charging_spec §1 |
| MT3608 | L 4.7 µH/2 A, C_in 10 µF, C_out 22 µF, R_fb 75k/10k, C_ff 22 pF | hardware doc §7.3 |
| Y1 SIT1532AI | per SiTime datasheet (0.1 µF typical — TBD-JP) | Nicla parity |

---

## 7. OPEN DECISIONS for JP

| # | Decision | Default proposal |
|---|---|---|
| OD-1 | EDA tool for capture/layout | (JP's choice; spec is tool-agnostic) |
| OD-2 | SWD connector: Tag-Connect TC2050 vs 2×5 1.27 mm Cortex Debug | TC2050 footprint + castellated pads |
| OD-3 | Final test-point set | §4.8 list |
| OD-4 | Battery connector PN + cell confirm: docs say ICP622540PMT **600 mAh** (sgc_system_design.md:746); task brief said "550 mAh" — reconcile; NTC R25/B from Renata pack spec → then compute R_HI/R_LO (BQ Eq. 1–2) | keep 600 mAh doc value |
| OD-5 | **Add U2 (BHI FW flash, MX25R1635F, QSPI) to BOM** — present on Nicla ("BHI Flash" U2, ABX00050), absent from BOM v4.3 | add, Nicla parity |
| OD-6 | BME688 populate vs DNP (unused by FW) | DNP to save cost/power; footprint always |
| OD-7 | BHI RESETN → P0.13 + overlay pin change (18→13), or tie-high and drop the bring-up pulse | route to P0.13 (keeps bring-up reset) |
| OD-8 | BQ CD: GPIO P0.08 + 100 kΩ PU to V_PMID (ship-mode capable), vs hard-tie high (no ship mode, I²C-only charge control) | GPIO + pull-up |
| OD-9 | Boost EN → P0.17 confirm (P0.24 doesn't exist on B402); 5 V level-translator part for SK6812 data (TXB0102-class TBD); and OD-9b: keep strip on P0.19 (R2 risk) or move to full-drive GPIO | keep P0.19, TXB0102-class, decide after R2 bench note |
| OD-10 | 32 kHz: fit Y1 SIT1532AI (Nicla parity) vs LFRC-only (EVK does this; app note [8]) | fit SIT1532AI (power accuracy) |
| OD-11 | LDC1612 INTB shares the BUTTON net (P0.02) in v2 — fine (v2 arming replaces button) — confirm no v1 DNP interaction | accept |
| OD-12 | MAX17048 VIH vs 1.8 V I²C pull-ups (datasheet fetch blocked) — verify at schematic review | verify, then freeze pull-up rail |
| OD-13 | USB ESD part: PESD5V0U2UT vs TPD4E05U06 | pick one, both in spec §4.7 |
| OD-14 | USB-IF inrush <10 µF rule vs our 10 µF bulk — accept non-USB-IF sourcing (our use case) or drop bulk to 4.7 µF | accept as-is |
| OD-15 | Confirm ICHG 110 mA (R_ISET 1.82 kΩ) / ILIM 200 mA (1 kΩ) / ITERM 10 % (4.99 kΩ) | values as computed |
| OD-16 | Module order code/grade (ANNA-B402 standard/professional, internal antenna) + corner vs side-edge placement | corner, per §4.9 |
| OD-17 | IS31FL3194 SDB net on Nicla (TBD-JP confirm) + its VCC decap value | SDB→V_PMID |

---

## 8. Quote-package checklist (what the layout must deliver for fab/EMS quotes)

1. **Gerbers**: 22 × 55 mm, **4-layer FR4, 0.8 mm** (hardware doc §9), ENIG (required for BHI260AP LGA + B402 module lands), 1 oz outer / 0.5 oz inner (standard JLC/PCBWay 4L; **must be cross-checked against the u-blox reference stackup for the antenna strip** — SIM App. B certification requirement, §4.9). Min trace/space 0.1/0.1 mm per hardware doc.
2. **Stackup spec sheet** (JLC JLP04xx-class 0.8 mm 4L or equivalent) attached to the quote; impedance-controlled only where SIM requires.
3. **BOM**: sgc_bom.md **v4.3** + pending adds/changes from this spec: U2 BHI flash (OD-5), level translator (OD-9), Y1 SIT1532AI or none (OD-10), ESD part (OD-13), battery connector PN (OD-4), ANNA-B402 order code (OD-16). B402 module itself = EMS-furnished line item (verify reel/cut-tape + MSL at quote).
4. **Centroid (XY)** once layout exists; stencil notes: 0.1 mm laser steel; request module-pad aperture check (B402 LGA lands) — ENIG + 0.8 mm board; consider step-stencil not required (no tall fine-pitch mix beyond module).
5. **DNP list**: LDC1612 + coil, RV-3028-C7, BZ1 beeper, BME688 (if OD-6=DNP), v2 user-SPI/UWB/RFID nets.
6. **Assembly notes**: double-sided OK; module last (reflow profile per u-blox MSL/packing note); SWD/UART TP keep-solder-mask-open or pogo pads per OD-2/OD-3.
7. **Enclosure interfaces** for the mechanical quote: USB-C mid-mount board-edge flush (usb_c spec §3), Ø16 mm button boss, LED window, JST-3 service loop, IP67 cap recess.

---

### Parent double-check list (recommended verification pass)

- ANNA-B402 Table 4 rows quoted here vs the PDF (`/tmp/anna-b402.pdf`, pp. 19–20) — especially P0.24/P0.25 absence and RESET_N=P0.18.
- BOM v4.3 diff for: U2 flash, translator, Y1, ESD, connector, module order code.
- Nicla schematic regions this spec flags TBD-JP: U8 SDB net, IS31 decap value, exact BHI-cluster M1/M2/M3 wiring (copy at capture).
- MAX17048 VIH (OD-12) from the ADI PDF before freezing the +1V8 pull-up rail.
- R_HI/R_LO NTC divider once Renata NTC constants are available (OD-4).

*End of spec — v0.1. No code changed, nothing committed.*
