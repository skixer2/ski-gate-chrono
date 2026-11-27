#!/usr/bin/env python3
# verify_pcb.py - semantic gate for the GENERATED sgc_p1.kicad_pcb.
# Reads the actual board file (not the plan), transforms placements back to
# plan space (y-up), and enforces the same rules as placement_check.py using
# TRUE pad geometry from the placed footprints. Run AFTER gen_pcb.py.
import math, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from kiutils.board import Board
from placement_check import P, W, H, ANT, COIL
from sgc_netlist import PARTS

b = Board.from_file(os.path.join(HERE, "sgc_p1.kicad_pcb"))
fails = []
for f in b.footprints:
    ref = f.properties.get("Reference")
    if ref not in P:
        fails.append("unknown ref: %s" % ref)
        continue
    px, py, prot = P[ref][0], P[ref][1], P[ref][2]
    if abs(f.position.X - px) > 0.01 or abs((H - f.position.Y) - py) > 0.01:
        fails.append("%s pos mismatch" % ref)
    brot = f.position.angle or 0
    if abs((brot - (360 - prot)) % 360) > 0.01:
        fails.append("%s rot mismatch" % ref)
rects = {}
for f in b.footprints:
    ref = f.properties.get("Reference")
    cx, cy = f.position.X, f.position.Y
    th = math.radians(f.position.angle or 0)
    cos, sn = math.cos(th), math.sin(th)
    pts = []
    for p in f.pads:
        gx = cx + p.position.X * cos + p.position.Y * sn
        gy = cy - p.position.X * sn + p.position.Y * cos
        pts.append((gx - p.size.X / 2.0, gy - p.size.Y / 2.0, gx + p.size.X / 2.0, gy + p.size.Y / 2.0))
    rects[ref] = (min(q[0] for q in pts), H - max(q[3] for q in pts), max(q[2] for q in pts), H - min(q[1] for q in pts))
def gap(a, b2):
    dx = max(b2[0] - a[2], a[0] - b2[2], 0)
    dy = max(b2[1] - a[3], a[1] - b2[3], 0)
    return (dx * dx + dy * dy) ** 0.5 if dx and dy else max(dx, dy)
refs = sorted(rects)
for i in range(len(refs)):
    for j in range(i + 1, len(refs)):
        g = gap(rects[refs[i]], rects[refs[j]])
        if g < 0.25:
            fails.append("PCB GAP %.2f %s-%s" % (g, refs[i], refs[j]))
for ref in refs:
    x0, y0, x1, y1 = rects[ref]
    ex = P[ref][4] if ref in P else set()
    if (x0 < -0.05 and "xmin" not in ex) or (y0 < -0.05 and "ymin" not in ex) or (x1 > W + 0.05 and "xmax" not in ex) or (y1 > H + 0.05 and "ymax" not in ex):
        fails.append("PCB EDGE %s" % ref)
    if ref not in ("U1", "J6") and ref in P:
        gx = max(ANT[0] - x1, x0 - ANT[2], 0); gy = max(ANT[1] - y1, y0 - ANT[3], 0)
        d = (gx * gx + gy * gy) ** 0.5 if gx and gy else max(gx, gy)
        if d < 0.3:
            fails.append("PCB ANT-ZONE %s d=%.2f" % (ref, d))
x0, y0, x1, y1 = rects["U12"]
gx = max(COIL[0] - x1, x0 - COIL[2], 0); gy = max(COIL[1] - y1, y0 - COIL[3], 0)
d12 = (gx * gx + gy * gy) ** 0.5 if gx and gy else max(gx, gy)
if d12 < 10.0:
    fails.append("PCB U12->COIL %.2f" % d12)
nets_expected = set()
for p in PARTS:
    for net in p["nets"].values():
        if net not in ("NC", "no_net"):
            nets_expected.add(net)
nets_board = set(n.name for n in b.nets if n.name)
if nets_expected - nets_board:
    fails.append("missing nets: %s" % sorted(nets_expected - nets_board))
print("SEMANTIC FAILS:", len(fails))
for f in fails:
    print("  ", f)
print("U12->coil: %.2f mm" % d12)
sys.exit(1 if fails else 0)
