#!/usr/bin/env python3
# one-shot: install official USB4085 fp, fix Reference labels to local coords,
# center board on A4 sheet, carry Dwgs.User fp_text (PCB Edge marker), update J1 plan.
import os
D = chr(34)
NL = chr(10)
HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)

# 1) install official footprint (backup custom)
import shutil
src = "/tmp/usb4085_official.kicad_mod"
dst = os.path.join("sgc_p1.pretty", "USB_C_Receptacle_GCT_USB4085.kicad_mod")
shutil.copy(dst, "/tmp/usb4085_custom_backup.kicad_mod")
shutil.copy(src, dst)
print("official USB4085 footprint installed")

# 2) gen_pcb patches
g = open("gen_pcb.py", encoding="utf-8").read()
old = "W, H = 22.0, 55.0"
assert old in g, "g1"
g = g.replace(old, old + NL + "OFF_X, OFF_Y = 139.0, 56.5   # A4 sheet placement of board origin", 1)
old = "    fx, fy, fr = x, H - y, (360 - rot) % 360"
assert old in g, "g2"
g = g.replace(old, "    fx, fy, fr = OFF_X + x, OFF_Y + (H - y), (360 - rot) % 360", 1)
old = "    body.append(TB * 2 + " + D + "(at %g %g 0)" + D + " % (fx, fy - 3.0))"
assert old in g, "g3"
g = g.replace(old, "    body.append(TB * 2 + (D + (at 0 -3.5 0) + D))".replace("(at 0 -3.5 0)", "(at 0 -3.5 0)"), 1)
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

# 3) placement_check J1 update
t = open("placement_check.py", encoding="utf-8").read()
import re
m = re.search(D + "J1" + D + ":  \(8\.03,  1\.0,  0,.*", t)
assert m, "J1 anchor"
t = t.replace(m.group(0), D + "J1" + D + ":  (13.975, 6.1, 180, (-1.8, -0.35, 7.75, 5.21), set(), " + D + "USB-C mid-mount TH; origin=pad A1; official PCB Edge marker lands on board edge" + D + ",", 1)
open("placement_check.py", "w", encoding="utf-8").write(t)
print("J1 plan updated")

# 4) verify_pcb: same sheet-offset transform
v = open("verify_pcb.py", encoding="utf-8").read()
old = "from placement_check import P, W, H, ANT, COIL"
assert old in v
v = v.replace(old, old + NL + "OFF_X, OFF_Y = 139.0, 56.5", 1)
old = "    if abs(f.position.X - px) > 0.01 or abs((H - f.position.Y) - py) > 0.01:"
assert old in v
v = v.replace(old, "    if abs(f.position.X - (OFF_X + px)) > 0.01 or abs(f.position.Y - (OFF_Y + H - py)) > 0.01:", 1)
old = "    rects[ref] = (min(q[0] for q in pts), H - max(q[3] for q in pts), max(q[2] for q in pts), H - min(q[1] for q in pts))"
assert old in v
v = v.replace(old, "    top = OFF_Y + H" + NL +
    "    rects[ref] = (min(q[0] for q in pts) - OFF_X, top - max(q[3] for q in pts), max(q[2] for q in pts) - OFF_X, top - min(q[1] for q in pts))", 1)
open("verify_pcb.py", "w", encoding="utf-8").write(v)
print("verify_pcb patched")
