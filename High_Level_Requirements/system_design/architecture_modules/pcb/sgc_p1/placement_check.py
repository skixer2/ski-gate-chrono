#!/usr/bin/env python3
# SGC P1 placement checker - mechanical rules on the PLACEMENTS table below.
# Extents = pad extents (+body union) relative to footprint ORIGIN (KiCad convention).
# "c" = extents are origin-centered; "o" = origin at rect corner reference (raw extents).
# Rules: pairwise gap >= 0.3 mm; edge clearance >= 0.5 mm (exempt list);
#        antenna zone (5.5..16.5, 50.5..55) only U1; coil zone (6.5..15.5, 23.5..31.5) only U6;
#        U12->coil zone rect distance >= 10.0 mm; all 79 refs placed.
import sys

W, H = 22.0, 55.0
ANT = (5.5, 50.5, 16.5, 55.0)
COIL = (6.5, 23.5, 15.5, 31.5)

# ext: (x0, y0, x1, y1) relative to origin; mode c = centered check uses rect as-is
P = {
 # ref: (x, y, rot, ext(x0,y0,x1,y1), exempt_edge, note)
 "U1":  (11.0, 51.2, 0,  (-3.05, -3.05, 3.05, 3.05), set(), "ANNA-B402 antenna to top edge"),
 "J1":  (8.03, 6.1, 0, (-1.8, -0.35, 7.75, 5.21), set(), "USB-C mid-mount TH (official fp); PCB Edge marker lands at plan (11.0, 0) = board bottom edge"),
 "J3":  (6.08, 14.0, 90, (-0.5, -0.5, 1.77, 5.58),   set(), "SWD 2x5 1.27 TH; battery pocket must clear bottom pins"),
 "J2":  (18.8, 8.0,  0,  (-2.7, -2.3, 2.7, 2.3),     set(), "battery JST-ACH"),
 "J5":  (2.9,  47.5, 0,  (-2.6, -1.75, 2.6, 1.75),   set(), "ext button/piezo hookup TBD-JP"),
 "J4":  (3.075, 18.0, 90,  (-1.6, -0.8, 1.6, 0.8),     set(), "DNP SK6812 strip hookup, outside antenna x-band"),
 "J6":  (22.0, 28.5, 270,(-1.1, -1.1, 3.64, 1.1),    set(("xmax",)), "castellated pinholes straddle right edge BY DESIGN"),
 "U3":  (10.2, 7.7,  0,  (-0.91, -0.91, 0.91, 0.91), set(), "BQ25120A"),
 "U4":  (14.2, 7.6,  0,  (-1.35, -0.89, 1.35, 0.89), set(), "MAX17048"),
 "L1":  (7.6,  7.6,  0,  (-1.1, -0.5, 1.1, 0.5),     set(), "charger inductor"),
 "C1":  (12.5, 10.9,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C2":  (8.6,  9.4,  0,  (-1.1, -0.5, 1.1, 0.5),     set(), ""),
 "C3":  (10.2, 10.8, 0,  (-1.1, -0.5, 1.1, 0.5),     set(), ""),
 "C4":  (17.4, 4.4,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C5":  (12.3, 9.3,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C6":  (11.5, 12.1, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C7":  (14.2, 9.3,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C16": (14.8, 11.4, 0,  (-1.1, -0.5, 1.1, 0.5),     set(), ""),
 "R1":  (6.2,  9.4,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R2":  (7.9,  10.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R3":  (7.9,  11.9, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R4":  (15.7, 12.65, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R5":  (13.7, 12.75, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R6":  (17.2, 11.2, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R7":  (16.9, 1.6,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R8":  (16.9, 2.8,  0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R9":  (17.9, 12.3, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R10": (19.8, 12.3, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "R15": (18.8, 1.6,  0,  (-0.75, -0.35, 0.75, 0.35), set(), "TS divider hi, near J1 VBUS"),
 "R16": (18.8, 2.8,  0,  (-0.75, -0.35, 0.75, 0.35), set(), "TS divider lo"),
 "U8":  (11.0, 41.0, 0,  (-2.05, -2.05, 2.05, 2.05), set(), "BHI260AP"),
 "Y1":  (11.0, 38.0, 0,  (-0.75, -0.4, 0.75, 0.4),   set(), "SIT1532"),
 "U9":  (5.8,  41.4, 0,  (-1.0, -1.0, 1.0, 1.0),     set(), "BMP390"),
 "U12": (17.8, 42.2, 0,  (-0.78, -0.78, 0.78, 0.78), set(), "BMM150, >=10mm to coil zone"),
 "U13": (7.0,  44.4, 0,  (-1.5, -1.5, 1.5, 1.5),     set(), "BME688, vent to top"),
 "C10": (4.3,  43.4, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C11": (14.3, 41.3, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C12": (15.7, 42.4, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "BHI bank"),
 "C13": (19.8, 42.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "BMM150 decap"),
 "C20": (10.2, 35.9, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "BHI VREG"),
 "R13": (8.6,  37.0, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "HOSTBOOT pd"),
 "R14": (13.3, 36.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "HIRQ pu"),
 "R18": (8.7,  34.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "M3 SCL pu"),
 "R29": (10.6, 34.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "M3 SDA pu"),
 "R40": (13.3, 34.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "M3_PS"),
 "R42": (8.6,  33.4, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "QSPI pull"),
 "U2":  (4.0,  36.6, 90, (-2.2, -3.4, 2.2, 3.4),     set(), "MX25R1635F SOIC8"),
 "U7":  (17.9, 36.6, 90, (-2.2, -3.4, 2.2, 3.4),     set(), "MX25R6435F SOIC8"),
 "C8":  (2.6,  41.15, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C9":  (19.9, 41.4, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C14": (3.4,  42.2, 0,  (-0.75, -0.35, 0.75, 0.35), set(), ""),
 "C21": (17.8, 43.9, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "BMM150 decap"),
 "C22": (9.8,  44.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "BME688 decap"),
 "C23": (8.6,  38.2, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "Y1 decap"),
 "C24": (16.5, 45.7, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "U1 VCC local"),
 "U5":  (14.8, 44.2, 0,  (-0.5, -0.8, 0.5, 0.8),     set(), "IS31FL3194"),
 "D1":  (14.6, 46.2, 0,  (-0.65, -0.65, 0.65, 0.65), set(), "status RGB"),
 "C15": (13.2, 44.2, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "IS31 decap"),
 "R41": (6.5,  46.9, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "button divider"),
 "U6":  (11.0, 27.5, 0,  (-2.0, -1.6, 2.0, 1.6),     set(), "DNP LDC1612 + coil spiral at PCB"),
 # DNP boost/level-shift option block
 "U10": (9.2,  14.6, 0,  (-1.5, -1.5, 1.5, 1.5),     set(), "DNP MT3608"),
 "U11": (10.4, 18.1, 0,  (-1.6, -1.6, 1.6, 1.6),     set(), "DNP TXS0102"),
 "L2":  (13.0, 14.0, 0,  (-1.35, -0.5, 1.35, 0.5),   set(), "DNP"),
 "D2":  (18.5, 14.2, 0,  (-3.3, -0.8, 3.3, 0.8),     set(), "DNP SS34 SMA"),
 "R11": (7.0,  18.1, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "DNP"),
 "R12": (7.0,  16.6, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "DNP"),
 "C17": (12.9, 16.2, 0,  (-1.1, -0.5, 1.1, 0.5),     set(), "DNP"),
 "C18": (15.9, 16.2, 0,  (-1.35, -0.5, 1.35, 0.5),   set(), "DNP"),
 "C19": (18.5, 16.2, 0,  (-0.75, -0.35, 0.75, 0.35), set(), "DNP"),
 "D5":  (13.4, 19.0, 0,  (-0.87, -0.4, 0.87, 0.4),   set(), "PMEG4005 SOD-1608"),
 "D6":  (16.25, 19.0, 0,  (-1.6, -0.65, 1.6, 0.65),   set(), "BAT42WS"),
 "D7":  (19.8, 19.0, 0,  (-1.6, -0.65, 1.6, 0.65),   set(), "BAT42WS"),
 "TP1": (4.0,  22.4, 0,  (-0.75, -0.75, 0.75, 0.75), set(), ""),
 "TP2": (11.0, 22.4, 0,  (-0.75, -0.75, 0.75, 0.75), set(), ""),
 "TP3": (17.0, 22.4, 0,  (-0.75, -0.75, 0.75, 0.75), set(), ""),
 "MH1": (2.0,  23.4, 0,  (-0.8, -0.8, 0.8, 0.8),     set(), "NPTH"),
 "MH2": (20.4, 46.0, 0,  (-0.8, -0.8, 0.8, 0.8),     set(), "NPTH"),
 "FID1":(20.6, 1.6,  0,  (-0.75, -0.75, 0.75, 0.75), set(), ""),
 "FID2":(20.6, 53.4, 0,  (-0.75, -0.75, 0.75, 0.75), set(), ""),
}

ALL_REFS = set("""U11 U10 L2 D2 R15 R16 C24 R14 R13 C20 J5 R11 R12 C17 C18 C19 J4 J1 R7 R8 C16 U3
C1 C2 C3 C4 C5 C6 L1 R1 R2 R3 R4 R5 R9 R10 U4 C7 R6 J2 J6 R42 U1 U8 C10 C11 C12 C13 U9 C14 U5
D1 C15 U7 C8 U2 C9 U6 U12 R18 R29 C21 R40 U13 C22 Y1 C23 D5 R41 D6 D7 TP1 TP2 TP3 MH1 MH2 FID1 FID2 J3""".split())

def rect(ref):
    x, y, rot, ext, ex, note = P[ref]
    x0, y0, x1, y1 = ext
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    rp = []
    for px, py in pts:
        a = rot % 360
        if a == 90:
            rx, ry = -py, px
        elif a == 180:
            rx, ry = -px, -py
        elif a == 270:
            rx, ry = py, -px
        else:
            rx, ry = px, py
        rp.append((rx + x, ry + y))
    xs = [p[0] for p in rp]; ys = [p[1] for p in rp]
    return min(xs), min(ys), max(xs), max(ys), ex

def gap(a, b):
    dx = max(b[0] - a[2], a[0] - b[2], 0)
    dy = max(b[1] - a[3], a[1] - b[3], 0)
    return (dx * dx + dy * dy) ** 0.5 if dx and dy else max(dx, dy)

def run_checks():
    fails = []
    placed = set(P)
    missing = ALL_REFS - placed
    extra = placed - ALL_REFS
    if missing: fails.append("MISSING refs: " + ",".join(sorted(missing)))
    if extra: fails.append("EXTRA refs: " + ",".join(sorted(extra)))

    R = {k: rect(k) for k in P}
    refs = sorted(P)
    for i in range(len(refs)):
        for j in range(i + 1, len(refs)):
            a, b = refs[i], refs[j]
            g = gap(R[a][:4], R[b][:4])
            if g < 0.3:
                fails.append("OVERLAP/GAP %.2f %s-%s" % (g, a, b))

    for ref in refs:
        x0, y0, x1, y1, ex = R[ref]
        if "xmin" not in ex and x0 < 0.5: fails.append("EDGE %s xmin=%.2f" % (ref, x0))
        if "ymin" not in ex and y0 < 0.5 and ref != "J1": fails.append("EDGE %s ymin=%.2f" % (ref, y0))
        if "xmax" not in ex and x1 > W - 0.5: fails.append("EDGE %s xmax=%.2f" % (ref, x1))
        if "ymax" not in ex and y1 > H - 0.5: fails.append("EDGE %s ymax=%.2f" % (ref, y1))
        if ref != "U1":
            gx = max(ANT[0] - x1, x0 - ANT[2], 0); gy = max(ANT[1] - y1, y0 - ANT[3], 0)
            d = (gx * gx + gy * gy) ** 0.5 if gx and gy else max(gx, gy)
            if d < 0.3: fails.append("ANTENNA-ZONE %s d=%.2f" % (ref, d))
        if ref not in ("U6",):
            gx = max(COIL[0] - x1, x0 - COIL[2], 0); gy = max(COIL[1] - y1, y0 - COIL[3], 0)
            d = (gx * gx + gy * gy) ** 0.5 if gx and gy else max(gx, gy)
            if d < 0.3: fails.append("COIL-ZONE %s d=%.2f" % (ref, d))

    x0, y0, x1, y1, _ = R["U12"]
    gx = max(COIL[0] - x1, x0 - COIL[2], 0); gy = max(COIL[1] - y1, y0 - COIL[3], 0)
    d12 = (gx * gx + gy * gy) ** 0.5 if gx and gy else max(gx, gy)
    if d12 < 10.0: fails.append("U12->COIL %.2f < 10.0" % d12)

    print("FAIL %d" % len(fails) if fails else "PASS")
    for f in fails: print(" ", f)
    return fails

if __name__ == "__main__":
    sys.exit(1 if run_checks() else 0)
