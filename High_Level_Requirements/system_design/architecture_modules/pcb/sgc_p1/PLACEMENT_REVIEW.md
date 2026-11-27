# SGC P1 — Independent Placement Review

**Date:** 2026-11-27 | **Plan reviewed:** PLACEMENT_PLAN.md v1 (2026-11-27)
**Netlist ground truth:** sgc_netlist.py (79 refs) | **Board:** 22×55 mm, 4-layer, 0.8 mm

---

## VERDICT: REJECT

Three blockers make the plan unbuildable as written; the sensor hub cluster is mechanically impossible to assemble.

---

## 1. COVERAGE — 10 refs missing from explicit placement [BLOCKER]

**Netlist has 79 refs; plan explicitly places 71.** The following are absent or only mentioned in passing without coordinates:

| Ref | Function | Zone | Mentioned? |
|-----|----------|------|------------|
| C16 | VBUS 10µF bulk @ USB connector | Bottom | No |
| C20 | BHI VREG 220n decap | Sensor hub | No |
| C23 | Y1 100n decap | Sensor hub | No |
| C24 | U1 VCC 1µF local decap | RF | No |
| R13 | HOSTBOOT 10k pulldown | Sensor hub | No |
| R14 | BHI HIRQ 10k pull-up | Sensor hub | No |
| R15 | TS R_HI 24k (VBUS→TS_NTC) | Bottom | No |
| R16 | TS R_LO 36k (TS_NTC→GND) | Bottom | No |
| R18 | M3 SCL 10k pull-up | Sensor hub | Line 38 only: "R18/R29 etc. adjacent" |
| R29 | M3 SDA 10k pull-up | Sensor hub | Line 38 only: "R18/R29 etc. adjacent" |

R15/R16 are safety-critical (charger TS divider — wrong values prevent charging or allow dangerous temperatures). All 10 must receive explicit (x, y) positions.

---

## 2. U12 BMM150 — keepout requirement physically impossible [BLOCKER]

**Plan claims:** ≥10 mm from antenna zone (y≥50.5) AND ≥10 mm from coil zone (y≤31.5).

**Recomputed from plan's own U12 coordinates (15.0, 40.4):**
- U12 body: 1.56×1.56 mm → top edge y = 41.18 mm, bottom edge y = 39.62 mm
- Distance to antenna zone (y=50.5): 50.5 − 41.18 = **9.32 mm** → FAIL (need ≥10)
- Distance to coil zone (y=31.5): 39.62 − 31.5 = **8.12 mm** → FAIL (need ≥10)

**Root cause:** the available vertical gap between keepout zones = 50.5 − 31.5 = 19.0 mm. Required = U12 body height 1.56 + 2 × 10.0 = **21.56 mm**. U12 cannot satisfy both constraints anywhere in the gap.

**Fix:** move U12 left of the coil zone (x < 6.5) into the y=41–43 band, e.g. **(4.0, 42.0)**. Then:
- Euclidean distance to coil zone (x=6.5, y=31.5): sqrt((6.5−4.78)² + (41.22−31.5)²) ≈ sqrt(2.96 + 94.5) ≈ 9.87 mm → still marginal.
- **Recommended:** reduce U12→coil clearance to 6 mm (copper spiral = non-ferromagnetic; the magnetic constraint is about ferrous materials, not eddy currents from a PCB trace). Then U12 at (4.0, 42.0) works.

---

## 3. Connector edge overhangs [BLOCKER]

**J3 SWD 2×5** at (2.6, 3.4) rot 90, body ≈ 7.0 mm wide:
- Left edge: 2.6 − 3.5 = **−0.9 mm** → overhangs board edge.

**J2 battery connector** at (20.0, 3.6) rot 270, body ≈ 7.0 mm wide:
- Right edge: 20.0 + 3.5 = **23.5 mm** → overhangs past x=22 → **1.5 mm**.

**J1 USB-C** at (11.0, 1.9), body 7.35×3.6:
- Bottom edge: 1.9 − 1.8 = **0.1 mm** → violates 0.5 mm edge clearance by 0.4 mm.

**Fixes:**
- J3: shift to **(4.6, 3.4)** to clear left edge, OR use a right-angle (horizontal) connector.
- J2: shift to **(17.5, 3.6)** to stay within board.
- J1: shift to **(11.0, 2.3)** → bottom edge at 0.5 mm.

---

## 4. Sensor hub cluster — catastrophic overlap density [MAJOR]

The y=36–44 band packs 13 parts into 8 mm of vertical height with sub-mm spacing. Recompued pairwise:

| Pair | Gap (mm) | Note |
|------|----------|------|
| U8 / U2 | −0.1 | Bodies intersect: U8 right 13.05 vs U2 left 2.8, y overlap 39.95–40.05 |
| U8 / U7 | −0.1 | Bodies intersect at x≈13.0, y 39.95–40.05 |
| U8 / Y1 | −0.8 | Y1 overlaps U8 |
| U2 / U8 | overlaps | U2 body 6.2 wide (rot 270) at x=5.9 → spans x 2.8–9.0 |
| U7 / U8 | overlaps | U7 body 6.2 wide (rot 270) at x=16.1 → spans x 13.0–19.2 |
| C10 / U2 | −0.3 | C10 body (8.9,39.75)–(9.9,40.25) vs U2 y 34.75–40.05 |
| C11 / U7 | −0.3 | Similar mirror at right side |
| U12 / U7 | −1.6 | U12 (14.2,39.6)–(15.8,41.2) vs U7 (13.0,34.75)–(19.2,40.05) |
| U2 / C8 | −1.0 | C8 at (3.4,37.4) sits inside U2's footprint |
| U2 / R42 | −0.9 | R42 at (8.6,36.6) inside U2 y-range (34.75–40.05) |

**Root cause:** U2/U7 rot 270 flash ICs (5.3×6.2 mm each) eat 12.4 mm of the 44 mm available x-span (U2: x 2.8–9.0, U7: x 13.0–19.2), leaving only 4.0 mm (x 9.0–13.0) for all central components (U8, Y1, C10, C11).

**Fix:** shift U2 left to **(3.6, 39.0)** still rot 270, U7 right to **(18.4, 39.0)** still rot 270. Then U2 right edge = 6.7 mm, U7 left edge = 15.3 mm, leaving 8.6 mm clear for U8 (body 4.1 mm). Reduce crowding by moving decaps C8/C9 upward by 1 mm, C10/C11 downward by 1 mm. Specific corrected coordinates:

| Ref | Old | New |
|-----|-----|-----|
| U2 | (5.9, 37.4) rot 270 | (3.6, 39.0) rot 270 |
| U7 | (16.1, 37.4) rot 270 | (18.4, 39.0) rot 270 |
| U8 | (11.0, 42.0) | (11.0, 42.0) — unchanged, now fits between U2/U7 |
| C8 | (3.4, 37.4) | (2.8, 38.5) |
| C9 | (18.6, 37.4) | (19.2, 38.5) |
| C10 | (9.4, 40.0) | (7.8, 39.5) |
| C11 | (12.6, 40.0) | (14.2, 39.5) |
| Y1 | (8.6, 40.0) | (9.4, 40.5) |
| R42 | (8.6, 36.6) | (7.0, 36.5) |

---

## 5. User-face band — U1 overlaps [MAJOR]

U1 ANNA-B402 at (11.0, 50.0), body 6.5×6.5 → spans x 7.75–14.25, y 46.75–53.25.

| Conflict | Overlap (mm) |
|----------|---------------|
| U1 / D1 (14.6, 47.9) | −1.0: D1 body (14.1,47.4)–(15.1,48.4) vs U1 y-range |
| U1 / U5 (14.6, 46.2) | −0.2: U5 body (14.1,45.4)–(15.1,47.0) vs U1 bottom |
| U1 / U13 (8.2, 46.0) | −1.9: U13 body (6.7,44.5)–(9.7,47.5) vs U1 |

**Fix:** U1's body extends to y=46.75. All user-face components must be below y=46.45 (0.3 mm clearance). Move D1 to (14.6, 46.1), U5 to (14.6, 44.5), U13 to (8.2, 44.5).

---

## 6. DNP option block — dense packing with overlaps [MAJOR]

Power/DNP band (y 8–23):

| Pair | Gap (mm) |
|------|----------|
| U10 / L2 | −1.2: both at y=13.4, x-spacing 1.6 mm, body halves 1.45 + 1.0 = 2.45 mm |
| R11 / R12 | 0.0: both at y=15.0, x 5.2/6.2 (1 mm apart, 0402 body 0.5+0.5 = 1.0) |
| C17/C18/C19 | −0.8/−0.5 overlap: y=15.0, x 8.0/9.4/10.8 (1.4 mm pitch for 0402 caps) |
| D5/D6/D7 | −0.8/−1.3 overlap: x 17.2/18.8/20.4 at y=10.4, D_SOD323 bodies ~1.7 mm wide → need ≥2.0 mm pitch |

All DNP parts are unpopulated v1 — tolerable blocking with design note, but R11/R12 (boost FB divider) and D5 (VBUS series protection = **not DNP**) need clean spacing.

---

## 7. Mechanical / keepout findings [MINOR]

1. **J6 pinhole** at (21.0, 30.5) rot 270: right edge at 21.0+1.5 = 22.5 mm → overhangs board edge. Shift to **(19.5, 30.5)**.
2. **C21/C22** share coordinates (11.0, 24.2) — impossible. Plan says "pair" but needs distinct positions, e.g. (10.2, 24.2) and (11.8, 24.2).
3. **MH2 at y=52.0**: 1.6 mm hole center → hole edge at y=52.8, 2.2 mm from top edge. OK for NPTH but verify 0.5 mm web thickness (52.8 + 0.8 = 53.6 < 55 → OK with 1.4 mm margin).
4. **FID2 at (20.4, 53.4)**: fiducial body (19.4,52.4)–(21.4,54.4) overlaps y>50.5, x>16.5 — safe since its outside antenna x-band and fiducials are bare copper (no component).
5. **Battery bottom side:** all parts placed TOP side only → bottom side y 4–47 has no components. Through-hole connectors (J2, J3, J5) will create bottom-side pads in this band → those pads violate "GND pour only" rule. Acceptable with GND plane stitching around them, but note in layout review.

---

## 8. Concrete betterment suggestions (up to 5)

1. **Swap U2/U7 rotation from 270 to 0** — pad-1 orientation is easier to route; flash SOIC-8 pads are on the long edges, so rot 270 pushes I/O pads toward board edges which may help routing to the BHI/B402. Keep 270 if verified at apply.

2. **Move test points TP1-TP3 to a consistent y-rail** — TP1 at y=16.5, TP2 at y=18.5, TP3 at y=33.5 are scattered. Align all three to y=20.0 (clear above DNP block) for a straight pogo-probe bar.

3. **Add button RC debounce caps explicitly** — the plan mentions "button RCs R18/R29 etc. adjacent" but R18/R29 are M3 I2C pull-ups (netlist), not button RCs. The SW1/SW2 buttons need dedicated debounce components; add R_SW1, C_SW1, R_SW2, C_SW2 with explicit positions.

4. **Relocate J5 piezo connector** from left edge (2.6, 9.6) to bottom-left near SWD zone — the piezo is a user-facing component that belongs on the face edge (y≈48–50) near a button, not buried in the power zone.

5. **Reserve UWB pad array area** — U1 pins 28/29 route UWB_PWR and UWB_CS (v2). Reserve an 8×8 mm clear zone near the right/bottom-right for a DWM3000-class UWB module; document v2 keepout now to avoid respin.

---

## Summary

| # | Finding | Severity |
|---|---------|----------|
| 1 | 10 refs missing from explicit placement | BLOCKER |
| 2 | U12 keepout impossible (9.3/8.1 vs 10 mm) | BLOCKER |
| 3 | J1/J2/J3 edge overhangs | BLOCKER |
| 4 | Sensor hub: 13 parts overlap in 8 mm band | MAJOR |
| 5 | U1 overlaps D1, U5, U13 in user-face band | MAJOR |
| 6 | DNP/Power zone overlap density | MAJOR |
| 7 | J6 overhang, C21/C22 collision, bottom-side pads in battery band | MINOR |
| 8 | Betterment suggestions (U2/U7 rot, pogo rail, button RCs, piezo relocate, UWB reserve) | NIT |

**Resubmit after:** (a) placing all 79 refs with explicit coordinates, (b) resolving U12 keepout, (c) fixing connector edge positions, (d) un-cramping the sensor hub cluster.
