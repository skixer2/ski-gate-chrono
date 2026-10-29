#!/usr/bin/env python3
"""Extract standard symbol definitions from the LOCAL KiCad 10 install
(Device:R/C/L, power:GND/VBAT) for the standard-symbols refactor.
Run on the PC with KiCad 10 installed. Writes std_symbols.txt next to this file.
Stdlib only (no kiutils needed)."""
import os, sys

LIBDIR = r"C:\Program Files\KiCad\10.0\share\kicad\symbols"

def extract(path, names):
    out = []
    src = open(path, encoding="utf-8", errors="ignore").read()
    for n in names:
        key = '(symbol "' + n + '"'
        i = src.find(key)
        while i != -1 and src[i + len(key)] not in (" ", "\n", "\t"):
            i = src.find(key, i + 1)
        if i == -1:
            out.append("# MISSING " + n + " in " + os.path.basename(path))
            continue
        depth, k = 0, i
        while k < len(src):
            c = src[k]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        out.append(src[i:k + 1])
    return out

blocks = extract(os.path.join(LIBDIR, "Device.kicad_sym"), ["R", "C", "L"])
blocks += extract(os.path.join(LIBDIR, "power.kicad_sym"), ["GND", "VBAT"])
here = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(here, "std_symbols.txt"), "w", encoding="utf-8").write("\n\n".join(blocks))
ok = len([b for b in blocks if not b.startswith("#")])
print("extracted %d/%d blocks -> std_symbols.txt" % (ok, len(blocks)))
