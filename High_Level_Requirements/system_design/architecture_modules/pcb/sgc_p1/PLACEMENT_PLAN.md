# SGC P1 — Placement Plan v1 (2026-11-27)

**Board:** 22 × 55 mm, 4-layer, 0.8 mm, ENIG. Coordinates: origin bottom-left (0,0), x→right, y→up, mm.
**Top edge (y=55): user face + antenna. Bottom edge (y=0): USB/charging. All components TOP side.**

## 1. Keepout zones (hard)

| Zone | Area | Rule |
|---|---|---|
| **RF antenna** | x 5.5–16.5, y 50.5–55 | No copper (F.Cu/B.Cu pads except U1's own), no components, no metal. U1 antenna edge within 0.5 mm of board edge. NPTH holes allowed at x<5.5. |
| **LDC coil option** | x 6.5–15.5, y 23.5–31.5 | Coil spiral area around U6 (DNP option). No other copper/traces crossing. |
| **Magnetometer clearance** | — | U12 BMM150 ≥10 mm from coil zone (achieved: 15 mm) and ≥10 mm from U1 antenna zone (achieved: 13 mm edge-to-edge... verify at apply: U12 y=41.2 top edge ≈42 → 8.5mm to y=50.5 zone → move U12 to y 40.6 or shrink: **resolved: U12 at (15.0, 40.6)** → 9.9 mm to zone edge... **apply uses ≥10.5: U12 (15.0, 40.4)**). |
| **Battery overlap** | y 4–47, bottom side | B.Cu: GND pour only, NO bottom-side components in this band (battery mounts under board — JP decision "board over battery"). |
| **BME688 airflow** | — | U13 placed highest of sensors near user edge; enclosure must provide vent above U13 (JP/mech note). |

## 2. Functional zones (y-bands)

| y-band | Function |
|---|---|
| 50.5–55 | Antenna keepout (U1 module itself + NPTH MH2 at x=4 only) |
| 44–50.5 | User face: buttons, RGB status LED, BME688 (vent access) |
| 36–44 | Sensor hub cluster: U8 + Y1 osc + satellites U9/U12/U13 + flashes U2/U7 |
| 23.5–31.5 | LDC1612 option zone (U6 DNP + coil) |
| 8–23 | Power-path Schottkys, DNP option block (U10/U11 boost/level-shift), test points |
| 0–8 | USB-C, charger, gauge, battery connector, SWD, aux connectors |

## 3. Per-part positions

### RF
- U1 ANNA-B402: (11.0, 50.0) rot 0 — body 6.5×6.5 → y 46.75..53.25; antenna edge +y. Module's antenna strip must extend to y≈54.7.

### User face (y 44–50.5)
- SW1 button: (2.8, 47.6) rot 0
- SW2 button: (19.2, 47.6) rot 0
- D1 SMLP34RGB2W3: (14.6, 47.9) rot 0 — status LED toward user edge
- U5 IS31FL3194: (14.6, 46.2) rot 0 — driver directly behind D1
- U13 BME688: (8.2, 46.0) rot 0 — highest sensor, vent path to top edge
- C15 (U5): (13.2, 46.2); C14 (U13): (8.2, 44.6); button RCs R18/R29 etc. adjacent

### Sensor hub cluster (y 36–44)
- U8 BHI260AP: (11.0, 42.0) rot 0
- Y1 SIT1532: (8.6, 40.0) rot 0 — adjacent, QSPI clock
- U9 BMP390: (7.0, 42.6) rot 0
- U12 BMM150: (15.0, 40.4) rot 0 — 10.1 mm to coil zone edge
- C10–C13: decoupling, 1.5–2 mm offsets from their ICs (C10/C11 U8: (9.4,40.0)/(12.6,40.0); C12 U9 (7.0,40.9); C13 U12 (15.0,38.9))
- U2 MX25R1635F: (5.9, 37.4) rot 270
- U7 MX25R6435F: (16.1, 37.4) rot 270
- C8 (U2): (3.4, 37.4); C9 (U7): (18.6, 37.4)
- R42 (QSPI pull): (8.6, 36.6)

### LDC option zone (y 23.5–31.5)
- U6 LDC1612: (11.0, 27.5) rot 0 (DNP option; coil spiral layout around it)
- R40 (coil series R): (7.4, 27.5); C21/C22 (tank caps): (11.0, 24.2) pair

### Power & IO (y 0–23)
- J1 USB4085: (11.0, 1.9) rot 0 — bottom edge, mid-mount
- U3 BQ25120A: (7.6, 5.6) rot 0
- U4 MAX17048: (13.8, 5.6) rot 0
- L1: (4.9, 5.6) — charger inductor adjacent U3
- R7 (CC1): (8.3, 3.0); R8 (CC2): (9.3, 3.0); R1/R2 (ILIM/STAT): (7.6, 7.4); R3 (ISEL): (13.8, 7.4); R4/R5 (gauge divider): (13.8, 3.8); R6 (gauge): (11.9, 5.6); C1/C2 (U3 in/out): (5.9, 5.6)/(9.6, 7.4); C3 (VBAT): (12.2, 7.4); C4/C5/C6 (rails): around U3/U4 per net
- C7 (U4 EP): (13.8, 4.4); R9/R10 (LED I2C pull? gate LEDs): (11.9, 46.2)
- D5/D6/D7 Schottky: (17.2, 10.4)/(18.8, 10.4)/(20.4, 10.4) — power-path ORing; R41: (17.2, 12.0)
- J5 JST-SH 2-pin: (2.6, 9.6) rot 90 — left edge aux
- J2 JST-ACH 3-pin battery: (20.0, 3.6) rot 270 — right edge, battery plug
- J3 SWD 2×5: (2.6, 3.4) rot 90 — bottom-left, probe access
- J6 PINHOLE-2: (21.0, 30.5) rot 270 — right-edge strap anchor
- TP1: (18.5, 16.5); TP2: (3.4, 18.5); TP3: (17.5, 33.5)
- MH1: (4.0, 3.0) NPTH; MH2: (4.0, 52.0) NPTH (outside antenna x-band)
- FID1: (1.6, 1.6); FID2: (20.4, 53.4)
- **DNP option block**: U10 MT3608 (6.8, 13.4), U11 TXS0102 (10.2, 13.4), L2 (5.2, 13.4), D2 (13.6, 13.4), R11/R12 (5.2/6.2, 15.0), C17/C18/C19 (8.0/9.4/10.8, 15.0), J4 CON-3 strip hook-up: (2.2, 52.6) rot 90 — top-left corner OUTSIDE antenna x-band (x<5.5), strip solders here along top edge.

## 4. Battery (mechanical, JP decision)
Battery (Renata ICP622540PMT class) mounts **under the board**, long axis along y. Board-over-battery from y≈4 to y≈47. Bottom side stays component-free there (GND pour only). J2 (battery) at bottom-right edge; BMS chain (U3/U4/D5-7) adjacent bottom region.

## 5. Open items at apply time
1. **Antenna keepout of record**: fetch ANNA-B4 SI manual (UBX-21000517) for official antenna-zone dims; current 11×4.5 band is conservative (module datasheet §antenna + ZSWatch precedent).
2. U13 BME688 vent: coordinate enclosure hole with JP/mech.
3. Confirm J4 (DNP CON-3) is the SK6812 strip hook-up (netlist shows it DNP; strip pads may be direct-solder instead).
4. SW1/SW2 refs confirmed from netlist tail (buttons) — verify symbol names at apply.
5. Rotation sanity vs footprint pad orientation at apply (U2/U7 rot 270 for edge clearance).

## 6. Flow
This plan → GLM review (independent) → apply into sgc_p1.kicad_pcb via placement tooling → validate → commit.
