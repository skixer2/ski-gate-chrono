#!/usr/bin/env python3
import os, re
D = chr(34)
NL = chr(10)
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# gen_pcb / verify_pcb patches are idempotent: skip if already applied
g = open("gen_pcb.py", encoding="utf-8").read()
if "OFF_X" not in g:
    old = "W, H = 22.0, 55.0"
    assert old in g
    g = g.replace(old, old + NL + "OFF_X, OFF_Y = 139.0, 56.5   # A4 sheet placement of board origin", 1)
    old = "    fx, fy, fr = x, H - y, (360 - rot) % 360"
    assert old in g
    g = g.replace(old, "    fx, fy, fr = OFF_X + x, OFF_Y + (H - y), (360 - rot) % 360", 1)
    old = "    body.append(TB * 2 + (D + (at 0 -3.5 0) + D))" if False else None
    old = "    body.append(TB * 2 + " + D + "(at %g %g 0)" + D + " % (fx, fy - 3.0))"
    assert old in g, "g3"
    g = g.replace(old, "    body.append(TB * 2 + (at 0 -3.5 0))" if False else "    body.append(TB * 2 + (at 0 -3.5 0))", 1)
    old = "            if st.startswith(" + D + "(fp_text" + D + "):"
    assert old in g, "g4"
    add = ("            if st.startswith(" + D + "(fp_text" + D + ") and " + D + "Dwgs.User" + D + " in st:" + NL +
           "                body.append(TB + st)" + NL +
           "                continue" + NL)
    g = g.replace(old, add + old, 1)
    old = "    out.append('(gr_rect (start 0 0) (end 22 55)"
    assert old in g, "g5"
    g = g.replace(old, "    out.append('(gr_rect (start 139 56.5) (end 161 111.5)", 1)
    open("gen_pcb.py", "w", encoding="utf-8").write(g)
    print("gen_pcb patched")
else:
    print("gen_pcb already patched")

v = open("verify_pcb.py", encoding="utf-8").read()
if "OFF_X" not in v:
    old = "from placement_check import P, W, H, ANT, COIL"
    assert old in v
    v = v.replace(old, old + NL + "OFF_X, OFF_Y = 139.0, 56.5", 1)
    old = "    if abs(f.position.X - px) > 0.01 or abs((H - f.position.Y) - py) > 0.01:"
    assert old in v
    v = v.replace(old, "    if abs(f.position.X - (OFF_X + px)) > 0.01 or abs(f.position.Y - (OFF_Y + H - py)) > 0.01:", 1)
    old = "    rects[ref] = (min(q[0] for q in pts), H - max(q[3] for q in pts), max(q[2] for q in pts), H - min(q[1] for q in pts))"
    assert old in v
    top = "OFF_Y + H"
    v = v.replace(old, "    top = " + top + NL + "    rects[ref] = (min(q[0] for q in pts) - OFF_X, top - max(q[3] for q in pts), max(q[2] for q in pts) - OFF_X, top - min(q[1] for q in pts))", 1)
    open("verify_pcb.py", "w", encoding="utf-8").write(v)
    print("verify_pcb patched")
else:
    print("verify_pcb already patched")

# J1 plan line: line-based replacement (robust to spacing)
t = open("placement_check.py", encoding="utf-8").read()
lines = t.split(NL)
for i, line in enumerate(lines):
    if line.strip().startswith(D + "J1" + D + ":"):
        lines[i] = " " + D + "J1" + D + ":  (13.975, 6.1, 180, (-1.8, -0.35, 7.75, 5.21), set(), " + D + "USB-C mid-mount TH; origin=pad A1; official PCB Edge marker lands on board edge" + D + "),"
        break
else:
    raise SystemExit("J1 line not found")
open("placement_check.py", "w", encoding="utf-8").write(t if False else NL.join(lines))
print("J1 plan updated")
