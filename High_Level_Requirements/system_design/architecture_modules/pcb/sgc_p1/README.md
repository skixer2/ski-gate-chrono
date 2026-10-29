# SGC P1 — KiCad 8 schematic (generated capture)

**Board:** SGC P1 standalone carrier for u-blox ANNA-B402 — 22 × 55 mm, 4-layer, 0.8 mm, ENIG.
**Source of truth:** `../../sgc_schematic_spec.md` v0.1 (commit 92b352e). Every pin number, pin name
and net in this capture is traceable to that spec (or the datasheet texts it cites). Nothing invented;
everything the spec marks TBD-JP appears as a **text note**, never as a guessed value/net.

## Files

| File | What |
|---|---|
| `sgc_p1.kicad_pro` | KiCad 8 project (minimal) |
| `sgc_p1.kicad_sym` | Project symbol library — 21 symbols (12 ICs/connectors + R/C/L/LED_RGB + 5 power symbols) |
| `sgc_p1.kicad_sch` | Single A3 landscape sheet, KiCad 8 format (version 20231120) |
| `sgc_netlist.py` | **Data**: symbol pin tables + per-component net assignment (edit here first) |
| `gen_symbols.py` | Regenerates `sgc_p1.kicad_sym` from the netlist |
| `gen_schematic.py` | Regenerates `sgc_p1.kicad_sch` + `sgc_p1.kicad_pro` |
| `validate_sgc_p1.py` | Mandatory validation (see below) |

## How to open (KiCad 8.x)

1. Open KiCad 8 → *File → Open Project* → select `sgc_p1.kicad_pro`.
2. Add the symbol library once: *Preferences → Manage Symbol Libraries → Project Libraries → add*
   → `sgc_p1.kicad_sym` (nickname `sgc_p1`). The schematic already embeds every used symbol in its
   `lib_symbols` section, so the sheet displays even before you register the library.
3. Open `sgc_p1.kicad_sch` in Eeschema.

**Regenerate after edits:** `python3 gen_symbols.py && python3 gen_schematic.py && python3 validate_sgc_p1.py`
(requires `pip install kiutils`). Edit `sgc_netlist.py`, not the generated files.

## Layout of the sheet (grouped by subsystem)

- **Top-left:** USB-C (J1), CC Rd resistors, VBUS bulk, BQ25120A charger + its R/C network.
- **Bottom-left:** MAX17048 gauge, JST-3 battery connector, SWD header (J3).
- **Center:** ANNA-B402 (U1, all 56 pins incl. 4/7/8 GND from Table 4) + antenna keepout note.
- **Right:** BHI260AP (U8, all 44 pads), BMP390L (U9), flash U7/U2, IS31FL3194 (U5) + RGB LED (D1).
- **Bottom-middle:** LDC1612 (U6, DNP v2) + peripheral/v2 notes (boost, SK6812, Y1, TPs, DNP list).

Connectivity style: **global labels on short stubs** for every pin (61 named nets) + **power symbols**
(`#PWR01…`, per zone anchors) on the power nets GND / +1V8 / VBAT / V_PMID / VBUS_USB. No sprawling wires.
Note boxes carry all OD callouts, the antenna keepout (SIM App. B), I²C address map and the peripheral/v2 notes.

## Refdes scheme (per task; spec's Nicla numbering preserved where it matters)

U1 ANNA-B402 · U2 MX25R1635F (BHI FW flash, OD-5) · U3 BQ25120A · U4 MAX17048 · U5 IS31FL3194 ·
U6 LDC1612 (DNP) · U7 MX25R6435F · U8 BHI260AP · U9 BMP390L · J1 USB4085 · J2 JST-3 battery ·
J3 SWD 2×5 · D1 SMLP34RGB2W3 · C1–C16 · R1–R10 · L1. (Nicla-schematic numbering had IS31=U8 — remapped here.)

## What is decided vs TBD-JP (OD summary — all also on the sheet as notes)

**Decided & captured:** charger resistor network (1.82k/1k/4.99k/100k), all decoupling per spec §6,
I²C pull-ups 100 k→+1V8 (i2c0), BHI RESETN→P0.13 (C3/OD-7), BQ CD→P0.08 + 100k PU V_PMID (C2/OD-8),
boost EN→P0.17 (C1), gauge ALRT→P0.21 + 10k PU, XL2→GND, ANT_INT↔ANT bridge, ANT_PCB→GND pattern
(corner placement), U2 QSPI nets, LDC ADDR→GND (0x2A) + INTB→BUTTON (OD-11), SWD pinout.

**TBD-JP — represented as notes only (never guessed):**
- **OD-4** battery connector PN + cell confirm; NTC R_HI/R_LO values (need Renata R25/B).
- **OD-5** U2 added to BOM (drawn populated, Nicla parity).
- **OD-6** BME688 populate-vs-DNP (not drawn; note at U8 — rides BHI M1 with BMM150).
- **OD-9 / OD-9b** +5 V level translator part (TXB0102-class); SK6812 strip stays on P0.19 pending R2 bench.
- **OD-10** Y1 SIT1532AI fit (default fit; XL1 net `32K_XL1`, note only).
- **OD-12** MAX17048 VIH vs 1.8 V pull-ups — verify before freezing pull-up rail.
- **OD-13** USB ESD part (PESD5V0U2UT vs TPD4E05U06) — note at J1, not drawn.
- **OD-16** module order code + corner placement confirm.
- **OD-17** IS31 SDB net on Nicla + its decap value (SDB→V_PMID drawn per default).
- BHI **HOSTBOOT(3)/VREG(6)** NC v1 (not in spec §6 — decoupling TBD-JP vs Bosch ds); M1/M2/M3 cluster
  wiring = **copy ABX00050 at capture** (BMM150/BME688 not drawn); BMP390 INT(7) NC (DNC per ds);
  RGB LED common polarity verify (IS31 OUTs are current sinks; spec text says anodes to OUTs).
- Boost (MT3608 + SK6812 ×5), RV-3028-C7, BZ1, U2/strip passives: **note boxes only** — capture at layout.
- TP set per OD-3: note only.

## Footprint guidance

Pre-assigned (standard lib): R/C/L 0402 (caps ≥4.7 µF 0603), U7/U2 `Package_SO:SOIC-8_5.23x5.23mm_P1.27mm`.
Left EMPTY → **custom footprints needed:** U1 ANNA-B402 LGA pattern (u-blox land pattern), U3 BQ25120A
YFP-25 DSBGA, U4 MAX17048 TDFN-8, U8 BHI260AP LGA-44, U5 IS31 WLCSP-8, U9 BMP390 LGA-10, U6 LDC WSON-12,
J1 USB4085 mid-mount, J2 JST-3 (PN per OD-4), J3 (2×5 1.27 mm Cortex-Debug style **or Tag-Connect
TC2050** per OD-2), D1 SMLP34RGB2W3. These can be generated later from the datasheet land patterns.

## ERC / validation status

- **Syntax:** both KiCad files parse clean with kiutils (round-trip write + re-read) — see below.
- **Netlist:** independently re-extracted from sheet geometry (wire endpoints + labels + power symbols)
  and compared pin-by-pin against `sgc_netlist.py`: **271/271 pin connections correct, 0 shorts,
  0 floating labels, 0 mid-span taps** (the hidden-short class KiCad auto-junctions).
- **Census:** 40 components (9 U + 3 J + 16 C + 10 R + 1 L + 1 D) vs spec net lists; pin counts:
  B402 56, BQ 25, MAX17048 9 (incl. EP), BHI 44, BMP390 10, LDC 12, IS31 8, MX25R 8+8, USB4085 20, JST 3, SWD 10.
- **NOT claimed:** no electrical-correctness/ERC guarantee beyond the above — run KiCad's ERC after JP
  review; expect informational warnings (single-sheet, NC pins on unused pads, v2 reserved nets).

Run validation: `python3 validate_sgc_p1.py` (exit 0 = green).
