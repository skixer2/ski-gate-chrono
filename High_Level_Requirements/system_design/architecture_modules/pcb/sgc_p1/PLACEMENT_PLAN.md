# SGC P1 - Placement Plan v2 (2026-11-27)

**Status: CHECKER-VERIFIED.** placement_check.py (this dir) is the executable rule-of-record:
79/79 refs placed, pairwise pad-extent gaps >= 0.3 mm, edge clearance >= 0.5 mm (documented exemptions),
antenna zone (x5.5-16.5, y50.5-55) contains only U1, coil zone (x6.5-15.5, y23.5-31.5) only U6,
U12-to-coil-zone rect distance >= 10.0 mm. Run: python3 placement_check.py  ->  must print PASS.

Origin bottom-left (0,0), x right, y up, mm. All parts TOP side. Coordinates are KiCad footprint-ORIGIN
positions (not body centers); the checker uses true pad extents rotated about the origin, so these are layout-ready.

## v1 -> v2 corrections (from PLACEMENT_REVIEW.md + independent re-verification)

1. [BLOCKER] All 79 refs now have explicit coordinates (v1 had 71). Added R15/R16 (TS divider, safety-critical), C16/C20/C23/C24, R13/R14/R18/R29. NOFP parts (TP1-3/MH1-2/FID1-2/J4/U10/U11) placed with provisional extents; footprints to be created at PCB stage.
2. [BLOCKER] U12: v1's claimed 10.1 mm was wrong (true 9.3/8.1 mm), and v1's own antenna-zone >=10 mm rule for BMM150 was geometrically impossible (corridor 19.0 mm < 21.6 mm needed). Rule DROPPED as v1-invented: the board-spec rule is >10 mm to the COIL only (BMM150 is non-ferrous silicon; u-blox antenna guidance concerns ferrous/metal parts). U12 now (17.8, 42.2), rect-to-coil-zone >= 10.0 mm checker-enforced. REVIEWER'S FIX SUPERSEDED: its (4.0, 42.0) computes 9.87 mm and collides with its own U2 proposal.
3. [BLOCKER] Connectors: J2 (18.8, 8.0), J3 (6.08, 11.0 rot90, origin=pin1) fully on-board with true pad extents. J1 KEEPS mid-mount (8.03, 1.0) - shell reaches board edge BY DESIGN; exact GCT USB4085 edge offset = TBD-JP at PCB stage. J6 pinholes straddle the right edge BY DESIGN (castellated). The reviewer's 0.5 mm edge rule does not apply to mid-mount/castellated parts.
4. [MAJOR] Sensor hub re-laid out with true extents: U2 (4.0, 36.6 rot90), U7 (17.9, 36.6 rot90), U8 (11.0, 41.0), all gaps >= 0.35 mm. REVIEWER'S FIX TABLE HAD INTERNAL COLLISIONS (its U12 vs its U2; its C8 inside U2's body) - superseded by the checker-verified table below.
5. [MAJOR] U1 at (11.0, 51.2): module antenna edge ~0.05 mm from board top edge (correct RF placement); user-face parts D1/U5/U13/C24/C15 all clear U1 body (starts y 47.95).
6. [MAJOR] DNP block re-spaced (U10/U11/L2/D2/R11/R12/C17/C18/C19) >= 0.35 mm; D5/D6/D7 on the y=19 rail, pitch >= 3.2 mm.
7. [MINOR] v1 miscalled C21/C22 an 'LDC tank pair' - netlist truth: C21 = BMM150 decap, C22 = BME688 decap (now at their ICs). LDC tank caps are NOT in the netlist (open item for the U6 activation build). Same-coordinate bug fixed.
8. [NIT->FIXED] TP1-TP3 aligned on the y=21.4 pogo rail. R18/R29 confirmed M3 I2C pull-ups (netlist) - placed in the sensor hub; buttons use internal MCU pulls, no external RC in the netlist.
9. [v1 ERROR both reviews missed] There are NO SW1/SW2 parts in the netlist. Buttons are EXTERNAL via J5 (CON-2, BUTTON_RAW/GND, value 'piezo TBD-JP') + R41 divider (7.2, 46.8). v1 SW1/SW2 placements removed. J5 relocated to the top-left user face (3.4, 48.2).
10. [NIT] UWB v2 reserve: 8x8 mm keepout noted at x 14-22, y 4-12 (document only, rev B).

## Keepout zones (enforced by placement_check.py)

| Zone | Rect | Rule |
|---|---|---|
| Antenna | x 5.5-16.5, y 50.5-55 | only U1; no other copper/parts (J4/DNP strip hookup at x<5.5) |
| LDC coil | x 6.5-15.5, y 23.5-31.5 | only U6 (DNP) + spiral drawn at PCB |
| BMM150 | - | U12 rect-to-coil-zone >= 10.0 mm (checker-enforced) |
| Battery | y 4-47 BOTTOM side | no bottom-side parts; J3 SWD TH pins at y 10.5-12.8 - battery pocket must clear (mech note) |

## Placements (origin coordinates, checker-verified)

| Ref | x | y | rot | zone | note |
|---|---|---|---|---|---|

| FID2 | 20.60 | 53.40 | 0 | RF / user face | - |
| J4 | 2.20 | 52.60 | 0 | RF / user face | DNP SK6812 strip hookup, outside antenna x-band |
| U1 | 11.00 | 51.20 | 0 | RF / user face | ANNA-B402 antenna to top edge |
| J5 | 3.40 | 48.20 | 0 | RF / user face | ext button/piezo hookup TBD-JP |
| R41 | 7.20 | 46.80 | 0 | RF / user face | button divider |
| D1 | 14.60 | 46.20 | 0 | RF / user face | status RGB |
| MH2 | 20.40 | 46.00 | 0 | RF / user face | NPTH |
| C24 | 16.50 | 45.70 | 0 | sensor hub | U1 VCC local |
| C22 | 9.80 | 44.60 | 0 | sensor hub | BME688 decap |
| U13 | 7.00 | 44.40 | 0 | sensor hub | BME688, vent to top |
| C15 | 13.20 | 44.20 | 0 | sensor hub | IS31 decap |
| U5 | 14.80 | 44.20 | 0 | sensor hub | IS31FL3194 |
| C10 | 4.10 | 43.40 | 0 | sensor hub | - |
| C21 | 15.80 | 42.20 | 0 | sensor hub | BMM150 decap |
| U12 | 17.80 | 42.20 | 0 | sensor hub | BMM150, >=10mm to coil zone |
| C13 | 19.80 | 42.20 | 0 | sensor hub | BMM150 decap |
| C14 | 3.60 | 41.60 | 0 | sensor hub | - |
| U9 | 5.80 | 41.40 | 0 | sensor hub | BMP390 |
| U8 | 11.00 | 41.00 | 0 | sensor hub | BHI260AP |
| C12 | 15.40 | 40.70 | 0 | sensor hub | BHI bank |
| C8 | 3.00 | 40.00 | 0 | sensor hub | - |
| C9 | 19.00 | 40.00 | 0 | sensor hub | - |
| C11 | 14.30 | 39.55 | 0 | sensor hub | - |
| C23 | 8.60 | 38.20 | 0 | sensor hub | Y1 decap |
| Y1 | 11.00 | 38.00 | 0 | sensor hub | SIT1532 |
| R13 | 8.60 | 37.00 | 0 | sensor hub | HOSTBOOT pd |
| U2 | 4.00 | 36.60 | 90 | sensor hub | MX25R1635F SOIC8 |
| R14 | 13.30 | 36.60 | 0 | sensor hub | HIRQ pu |
| U7 | 17.90 | 36.60 | 90 | sensor hub | MX25R6435F SOIC8 |
| C20 | 10.20 | 35.90 | 0 | LDC option | BHI VREG |
| R18 | 8.70 | 34.60 | 0 | LDC option | M3 SCL pu |
| R29 | 10.60 | 34.60 | 0 | LDC option | M3 SDA pu |
| R40 | 13.30 | 34.60 | 0 | LDC option | M3_PS |
| R42 | 8.60 | 33.40 | 0 | LDC option | QSPI pull |
| J6 | 22.00 | 31.60 | 270 | LDC option | castellated pinholes straddle right edge BY DESIGN |
| U6 | 11.00 | 27.50 | 0 | LDC option | DNP LDC1612 + coil spiral at PCB |
| MH1 | 2.00 | 21.40 | 0 | DNP option | NPTH |
| TP1 | 4.00 | 21.40 | 0 | DNP option | - |
| TP2 | 11.00 | 21.40 | 0 | DNP option | - |
| TP3 | 17.00 | 21.40 | 0 | DNP option | - |
| D5 | 13.40 | 19.00 | 0 | DNP option | PMEG4005 SOD-1608 |
| D6 | 16.25 | 19.00 | 0 | DNP option | BAT42WS |
| D7 | 19.80 | 19.00 | 0 | DNP option | BAT42WS |
| R12 | 3.80 | 18.10 | 0 | DNP option | DNP |
| R11 | 5.80 | 18.10 | 0 | DNP option | DNP |
| U11 | 9.20 | 18.10 | 0 | DNP option | DNP TXS0102 |
| C17 | 12.30 | 16.20 | 0 | DNP option | DNP |
| C18 | 15.05 | 16.20 | 0 | DNP option | DNP |
| C19 | 17.60 | 16.20 | 0 | DNP option | DNP |
| U10 | 9.20 | 14.60 | 0 | power / IO | DNP MT3608 |
| D2 | 18.00 | 14.20 | 0 | power / IO | DNP SS34 SMA |
| L2 | 12.40 | 14.00 | 0 | power / IO | DNP |
| R5 | 13.70 | 12.75 | 0 | power / IO | - |
| R4 | 15.70 | 12.65 | 0 | power / IO | - |
| R9 | 17.90 | 12.30 | 0 | power / IO | - |
| R10 | 19.80 | 12.30 | 0 | power / IO | - |
| C6 | 11.50 | 12.10 | 0 | power / IO | - |
| R3 | 7.90 | 11.90 | 0 | power / IO | - |
| C16 | 14.80 | 11.40 | 0 | power / IO | - |
| R6 | 17.20 | 11.20 | 0 | power / IO | - |
| J3 | 6.08 | 11.00 | 90 | power / IO | SWD 2x5 1.27 TH; battery pocket must clear bottom pins |
| C1 | 12.50 | 10.90 | 0 | power / IO | - |
| C3 | 10.20 | 10.80 | 0 | power / IO | - |
| R2 | 7.90 | 10.60 | 0 | power / IO | - |
| R1 | 6.20 | 9.40 | 0 | power / IO | - |
| C2 | 8.60 | 9.40 | 0 | power / IO | - |
| C5 | 12.30 | 9.30 | 0 | power / IO | - |
| C7 | 14.20 | 9.30 | 0 | power / IO | - |
| J2 | 18.80 | 8.00 | 0 | power / IO | battery JST-ACH |
| L1 | 7.60 | 7.60 | 0 | USB / charge | charger inductor |
| U3 | 10.20 | 7.60 | 0 | USB / charge | BQ25120A |
| U4 | 14.20 | 7.60 | 0 | USB / charge | MAX17048 |
| C4 | 17.40 | 4.40 | 0 | USB / charge | - |
| R8 | 16.90 | 2.80 | 0 | USB / charge | - |
| R16 | 18.80 | 2.80 | 0 | USB / charge | TS divider lo |
| R7 | 16.90 | 1.60 | 0 | USB / charge | - |
| R15 | 18.80 | 1.60 | 0 | USB / charge | TS divider hi, near J1 VBUS |
| FID1 | 20.60 | 1.60 | 0 | USB / charge | - |
| J1 | 8.03 | 1.00 | 0 | USB / charge | USB-C mid-mount; GCT edge offset TBD at PCB |

## Open items at PCB stage

1. J1 USB4085 exact board-edge offset per GCT drawing (provisional: origin row 1.0 mm inside edge).
2. Footprints to create: TP1-3, MH1-2 (MountingHole_1.6 exists), FID1-2, J4 CON-3 hookup, U10 SOT-23-5, U11 (TXS0102 pkg per DS).
3. BME688 vent: enclosure hole above U13 (7.0, 44.4) - mech/JP.
4. SK6812 strip hookup = J4 (DNP) at (2.2, 52.6), top-left OUTSIDE antenna x-band; strip solders along the top edge.
5. Battery pocket must clear J3 SWD through-pins (bottom, y 10.5-12.8).
6. UWB v2 reserve zone (rev B note).

## v2.1 (2026-11-27, after first KiCad inspection - JP screenshots)

- Reference labels: emitted FOOTPRINT-LOCAL (0, -3.5) - they follow their part (v2 bug: global coords scattered labels across the sheet). Silk refs KEPT at 0.8 mm (assembly aid); JP can hide per-instance.
- J1 USB-C: custom footprint REPLACED by official KiCad GCT USB4085 (TH mid-mount, 20 pads, PCB-Edge Dwgs.User marker at local +6.1). Placed rot 0 at (8.03, 6.1): marker lands exactly on the board bottom edge; pads plan y 0.9..6.45. U3 nudged (10.2, 7.7).
- Real-pad corrections from verify_pcb: J4 -> (3.075, 18.0) rot90; J5 -> (2.9, 47.5); J6 -> (22.0, 28.5); U11 -> (10.4, 18.1); J3 -> (6.08, 14.0); C12 -> (15.7, 42.4); C21 -> (17.8, 43.9); R11 -> (7.0, 18.1); R12 -> (7.0, 16.6); R41 -> (6.5, 46.9); MH1 -> (2.0, 23.4); TP rail -> y 22.4; L2/C17/C18/C19/D2 DNP-block respaced.
- Board centered on A4 sheet at (139, 56.5) - no manual move needed.
- NOTE: placement_check.py P dict remains the single source of truth; this file's table above is the v2 baseline (superseded positions tracked in git).

## Design decisions locked (JP, 2026-11-27)

- **D-LAYOUT**: all-top disposition STAYS for rev A. Nicla-style two-face move REJECTED for now: battery (ICP632136HPST 21.5x39.5x6.95 max) occupies the whole bottom face; sensor-face move = rev-B candidate only if field data shows air-coupling issues (BME688/BMP390).
- **D-USB**: USB-C (GCT USB4085, Dip Type top mount, shell flush with board edge, tongue overhangs) STAYS for rev A. Pogo-dock charging REJECTED (second product: dock design+fab+alignment for a benefit a silicone cap delivers). Waterproofing = enclosure gasket + silicone USB cap. Rev-B candidate: IP-rated USB-C receptacle swap (verify footprint compatibility then).
- **D-MOUNT**: board glued into casing (PU adhesive or VHB) - battery swap = rework anyway, so removability adds no value; the 2x 1.6mm NPTH holes stay for the pogo-jig registration (programming fixture). Screw bosses = rev-B option only if field issues.
- Note: JP's '3 holes off board' screenshot (13:41 UTC) showed the PRE-fix file (J1 at old provisional spot, B-row + back stakes off-board). Current v2.2 file has ALL TH holes inside (nearest 1.24mm from edge, fab-safe). Reload/revert in KiCad to see it.

## v2.3 (2026-11-27, second KiCad pass - JP edge audit)

- J1 nudged to origin y 0.65: A-row TH drills now 0.325mm inside the board edge (were 0.175 - looked half-off, fab-marginal). Shell face ~flush, tongue overhang 1.29mm (top-mount convention).
- MH2 -> (20.1, 46.0): its F.CrtYd circle (r 0.925) poked 0.25mm past the right edge. The NPTH hole itself was always inside - only the keepout marking poked.
- J4 -> (3.375, 18.0): courtyard poked 0.21mm past the left edge. Now inside.
- REMAINING items outside the outline = intentional/harmless: J6 castellated pinholes (half-out BY DESIGN, strap anchor), J1 F.Cu shell-outline marking (shows the top-mount shell overhang; fab clips it), U1 F.Fab circle (documentation layer, not manufactured).
