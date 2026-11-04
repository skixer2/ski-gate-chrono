# N1 = "boosted Nicla CLONE" — verified gap analysis (v2, corrected)

**JP 2026-11-04 (binding):** *"Nicla-boosted clone is a CLONE: all on Nicla shall be present or boosted.
The minimal risk is to recreate the identical board."* Plus: BME688 must be there (temperature planned).

**Method correction (JP's own instruction — "don't just trust what I said, compare it with the V-cycle
documentation").** v1 of this file listed BMM150/BME688 as open decisions. That was **wrong**: the
V-cycle doc `sgc_schematic_spec.md` §1 and §4.4 already mandate them as part of a *strict replica*.
The real problem is worse and simpler: **the netlist never implemented them.** Parity is now measured
against three sources, not opinions:

1. `reference/ABX00050-schematics.pdf` — official Arduino schematic, 7 sheets, **100 components** (census below)
2. `sgc_schematic_spec.md` v0.1 §1/§4 — the V-cycle requirement ("strict replica … BHI260AP + BMM150 + BMP390L + BME688 + dual MX25R flash + IS31FL3194 + BQ25120A")
3. `sgc_netlist.py` — what is actually on the board today (49 parts)

---

## 1. Official Nicla component census (extracted from the PDF, all 7 sheets)

**100 components:** C×32, R×41, U×14, J×6, D×3, L×1, Y×1, DL×1 (RGB LED), MD×1 (ANNA module), plus PB1 button.

| Sheet | Refs |
|---|---|
| 1 TOP | hierarchy only |
| 2 CONNECTORS | J1 J2 J3 J4 J5 J7, R19 R20 R22 R36 R37 R38 R39, C29 C30, PB1, D1 |
| 3 LEVEL SHIFTERS | U11 TXB0108, U12 NTS0304, U13 NTS0304, C23–C27, C31 C32, R14 R33 R40 R41 |
| 4 POWER | U9 BQ25120A, L1 2.2µH, D3 D4 D5 PMEG4005, C1 C10 C14 C15 C21 C22, R1 R2 R5 R15 R16 R17 R24 R25 R26 R27 |
| 5 USB | U1 SAMD11, U14 AP2112K-3.3, U13?? , C18 C19 C20 C28, R28 R30 R34 R35, J7 |
| 6 SENSORS | U5 BHI260AP, U3 BMP390L, U4 BMM150, U2 BME688, U6 MX25R1635F, C3–C13, R8 R18 R21 R23 R29 |
| 7 ANNAB112 | MD1 ANNA-B112, U7 MX25R1635F, U8 IS31FL3194, DL1 SMLP34RGB2W3, **Y1 SIT1532AI**, C2 C17, R6 R7 R9 R12 R31 R32 |

*(Note: the PDF's internal ref designators differ from the reader's labels — e.g. U2 = BME688 on sheet 6 but the Arduino page calls the flash "U2 BHI Flash". Use the sheet, not the legend, when capturing.)*

---

## 2. Parity table — Nicla function by function vs our netlist TODAY

Legend: ✅ present & equivalent · 🔵 **better (boosted)** · ❌ **MISSING** · ➖ deliberately not copied

| Nicla function | Nicla part | Ours | Status |
|---|---|---|---|
| MCU module | ANNA-B112 (nRF52832, 64 KB) | ANNA-B402 (nRF52833, **128 KB**) | 🔵 |
| BHI260AP program flash | MX25R1635F (2 MB), QSPI | U2 MX25R1635F (2 MB) | ✅ |
| Data-logging flash | MX25R1635F (2 MB) | U7 MX25R6435F (**8 MB**) | 🔵 |
| Charger / PMIC | BQ25120A | U3 BQ25120A | ✅ |
| Fuel gauge | — (BQ band-gauge only) | U4 MAX17048 @0x36 | 🔵 |
| IMU | BHI260AP | U8 BHI260AP | ✅ |
| Pressure | BMP390L | U9 BMP390 | ✅ |
| **Magnetometer** | **BMM150 (U4, sheet 6)** | **— none —** | ❌ **MISSING** |
| **Environment (T/RH/gas)** | **BME688 (U2, sheet 6)** | **— none — (only BHI pad 18 MCSB2 is wired to NC)** | ❌ **MISSING** |
| RGB status LED | IS31FL3194 + SMLP34RGB2W3 | U5 + D1 (same parts) | ✅ |
| **32.768 kHz oscillator** | **Y1 SIT1532AI-J4-DCC-32.768E** | **— none —** (`32K_XL1` net has zero parts) | ❌ **MISSING** |
| Reverse-protection on VBUS | D3 PMEG4005EPK → BQ IN | — none — | ❌ MISSING |
| Reverse-protection on 5 V aux | D4/D5 PMEG4005 | — | ➖ (no ESLOV) |
| USB **connector** | J7 micro-USB ZX62-AB-5PA | J1 USB-C USB4085, 5.1 kΩ Rd | 🔵 |
| USB ESD | (in bridge circuit) | — none — | ❌ verify |
| **USB↔serial bridge** | **U1 SAMD11 + U14 AP2112K-3.3 + U13 NTS0304 + 100 nF×2 + 100 k×4** | — none — (SWD J3 + BLE OTA instead) | ➖ **deliberate (JP OK 2026-11-04; rev B footprint reserved)** |
| **External IO (ESLOV)** | J5 SM05B-SRSS + R36/R37 10k + R38/R39 0R | — | ❌ MISSING |
| **10-pin expansion header** | J1 2×10 (JTAG_ANNA/JTAG_BHI/SPI1_EXT/I2C1_EXT/LPIO/ADC0/ADC1) | J3 2×5 SWD only | ⚠ partial |
| Aux 3-pin | J4 BM03B-ACHSS | J4 = LED strip (different) | ➖ |
| Level shifters (IO) | U11 TXB0108 + U12 NTS0304 | U11 TXS0102 (LED data only, DNP) | ⚠ partial |
| Arming input | PB1 pushbutton on board | J5 off-board Langir piezo | ➖ deliberate (JP) |
| Battery connector | J2 (VBAT/NTC/GND) | J2 JST-3 with NTC | ✅ |
| 5 V rail | — | MT3608 + L2 + D2 SS34 + TXS0102 + J4 strip | ➕ boosted |
| Proximity | — | LDC1612 (DNP) | ➕ |
| RTC | — | RV-3028-C7 (DNP, footprint) | ➕ |

---

## 3. What this means — and my recommendation

> **Doc defects found while answering JP's questions (2026-11-04) — fix before rev A snapshot:**
> - **D1** `sgc_architecture_hardware.md:162` says "P0.18 → BQ25120 MR (reset network)". **Wrong.**
>   Verified in ABX00050: net **`RESET_BHI260`** (BHI260 pad 9 / E3 `PIU509`) goes to the B112 **P0.18**.
>   Corroborated by our own Zephyr overlay comment: "BHI260 RESET pin 9 = Arduino variant pin 13 =
>   P0.18 (see variant.cpp:91)". P0.18 on Nicla is the **BHI reset**, not BQ MR.
> - **D2** `sgc_netlist.py:492` R4 net is **`+1V8`** but its own comment and spec §4.1 E2 say **`V_PMID`**.
>   V_PMID is the correct rail (present with battery alone; +1V8/SYS does not exist in ship mode, so the
>   pull-up would not guarantee active mode at cold boot — the whole point of OD-8). **Fix net, not doc.**

**JP's instinct is right, and the docs back it.** The V-cycle already demands a strict replica; the
netlist is simply behind the documentation. But "recreate the identical board" needs one important
qualification, because **three Nicla blocks must NOT be copied verbatim**:

1. **ANNA-B112 → B402 is forced.** nRF **P0.24 and P0.25 do not exist** on the B402 (Table 4 complete).
   Boost EN moves to P0.17, BQ CD to P0.08, and BHI RESETN to P0.13 because B402 pin 12 (RESET_N) is
   fixed on P0.18 — which Nicla uses for BHI reset. Already resolved in spec §3.3 (C1/C2/C3).
2. **Qi charging → USB-C** (Qi dropped v4.2).
3. **SAMD11 USB bridge — DECIDED (JP "OK!" 2026-11-04): OMIT in rev A, reserve the footprint block for
   rev B.** It exists on Nicla to give USB programming + serial. We have SWD (J3) and a working BLE OTA
   path. Copying it costs ~5 ICs (SAMD11, AP2112K-3.3, NTS0304 ×2, TXB0108), a second 3.3 V rail, and 4
   level-shift nets — and it serves *only* programming/console. Not a parity gap; a deliberate delta.
   Everything else is cheap and should be closed now.

### Close now (all cheap, all pure parity, mostly zero new nRF pins)
| # | Item | Effort | Note |
|---|---|---|---|
| G1 | **BMM150 magnetometer** | ~8 parts, **0 nRF pins** | rides the BHI260AP M-port (M3 I²C) — no MCU cost |
| G2 | **BME688 environment sensor** | ~6 parts, **0 nRF pins** | rides BHI M-port (MCSB2, pad 18 already reserved). **JP wants temperature** → populate, supersedes OD-6 "DNP" |
| G3 | **Y1 SIT1532AI 32.768 kHz + 100 nF** | 2 parts, **0 nRF pins** | XL1 driven, XL2 → GND. Net `32K_XL1` is currently dangling. **No stock KiCad footprint for the 1.5×0.8 mm 1508 CSP** — must draw it. |
| G4 | **D3 PMEG4005EPK** series diode VBUS → BQ IN | 1 part | Nicla parity + protects against 5 V-strip back-feed |
| G5 | **BME688 + BMM150 decoupling** | ~5 caps | per Nicla bypass bank (C5 1 µF, C6/C7/C12/C13 100 nF) |

Zero new nRF GPIO needed for G1–G3 — they sit on the BHI260AP's own master port and its XTAL input.
That is why closing them is nearly free and why omitting them was pure oversight, not a design choice.

### Keep as documented deltas (not gaps)
SAMD11 bridge, ESLOV J5, J1 2×10 header, PB1 button, Qi, micro-USB → all either deliberately dropped,
replaced by a better option, or subject to JP's call.

---

## 4. Corrected sheet structure (Arduino style, one function per page)

```
sgc_n1.kicad_sch              (root, hierarchical)
├── 01_top.kicad_sch           TOP — block diagram + rail index (no parts, like ABX00050 sheet 1)
├── 02_power.kicad_sch         BQ25120A + straps + PMID/VBAT/+1V8 + MAX17048 + D3 protection + NTC
├── 03_mcu.kicad_sch           ANNA-B402 + Y1 32k + decoupling + SWD + antenna counterpoise
├── 04_sensors.kicad_sch       BHI260AP + BMP390 + BMM150 + BME688 + LDC1612 + bypass bank
├── 05_storage.kicad_sch       U2 MX25R1635F (BHI QSPI) + U7 MX25R6435F (logging)
├── 06_boost_led.kicad_sch     MT3608 + L2 + D2 + TXS0102 + J4 strip + SK6812 reserve  ← today's page 2, as-is
└── 07_connectors.kicad_sch    USB-C + J3 SWD + J2 battery + J5 piezo + TPs
```

**Gate on every split:** `netdiff.py` must print **IDENTICAL MEMBERSHIP**. Any membership change = a
net broke; revert that step.

---

## 5. About the "KiCad Library for Arduino Modules" (JP added it — verified, and it does not apply)

Found at `C:\Users\v17ni\Documents\KiCad\10.0\3rdparty\symbols\com_github_alarm-siren_arduino-kicad-library\`
(+ `footprints\...\arduino-library.pretty`), v4.2.0, by Nicholas Parks Young, CC-BY-SA-4.0.

**Verified contents:** the library is *board-to-board connectors* — `Arduino_Nicla_Sense_ME_Socket`,
`Arduino_Nicla_Sense_ME_Tile`, plus Uno/Mega/MKR/Nano shield and socket footprints. It contains
**no component-level symbols**: no BHI260AP, no ANNA module, no BQ25120A, no BMM150/BME688.

**Consequence:** it is the right library for the *carrier-board* concept (a Nicla soldered/socketed onto
our PCB) — the approach we dropped in v2.4. It cannot supply any part of the clone itself. All
component symbols/footprints for N1 must come from the vendor datasheets + the ABX00050 reference, as
we have been doing. **No action needed on the library** — it stays installed, just unused for this board.
