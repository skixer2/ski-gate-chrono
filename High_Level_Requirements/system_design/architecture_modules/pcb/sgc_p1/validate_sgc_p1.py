#!/usr/bin/env python3
"""
validate_sgc_p1.py — mandatory validation for the generated SGC P1 KiCad files.

Checks:
 1. kiutils round-trip parse of sgc_p1.kicad_sym, sgc_p1.kicad_sch (write-to-temp, re-read)
    and JSON parse of sgc_p1.kicad_pro.
 2. Every placed symbol's library symbol exists in BOTH the schematic lib_symbols and
    the project symbol library.
 3. Pin geometry: every pin of every placed component has a wire endpoint, no-connect,
    or power-symbol pin exactly at its computed connection point (KiCad lib->sheet Y-flip).
 4. Independent netlist extraction (union-find over wire segments + labels + power
    symbols) compared against the sgc_netlist.py expectation for EVERY pin.
 5. No floating global labels; no wire cluster carrying conflicting net identities.
 6. Component census vs the spec net lists; pin-count census vs datasheets.
 7. Sheet-bounds sanity (A3) and no duplicate placements.

Run: python3 validate_sgc_p1.py   (exit 0 = all good)
"""
import json
import os
import sys
import tempfile

from kiutils.symbol import SymbolLib
from kiutils.schematic import Schematic

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sgc_netlist import SYMBOL_PINS, PARTS, POWER_SYMBOLS

ERRORS = []
INFOS = []


def err(msg):
    ERRORS.append(msg)


def info(msg):
    INFOS.append(msg)


def roundtrip(path, cls):
    obj = cls.from_file(path)
    tmp = tempfile.NamedTemporaryFile(suffix=os.path.splitext(path)[1], delete=False)
    tmp.close()
    obj.to_file(tmp.name)
    obj2 = cls.from_file(tmp.name)
    os.unlink(tmp.name)
    return obj, obj2


def key(p):  # quantized point key
    return (round(p.X, 3), round(p.Y, 3))


class UF:
    def __init__(self):
        self.p = {}

    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


def main():
    # ---- 1. round-trips ----
    lib, lib2 = roundtrip(os.path.join(HERE, "sgc_p1.kicad_sym"), SymbolLib)
    info(f"sgc_p1.kicad_sym: kiutils round-trip OK ({len(lib.symbols)} symbol entries, "
         f"{sum(1 for s in lib.symbols if s.unitId is None)} top-level)")
    sch, sch2 = roundtrip(os.path.join(HERE, "sgc_p1.kicad_sch"), Schematic)
    info(f"sgc_p1.kicad_sch: kiutils round-trip OK ({len(sch.schematicSymbols)} instances, "
         f"{len(sch.graphicalItems)} wires, {len(sch.globalLabels)} labels, {len(sch.noConnects)} no-connects)")
    # ---- 1b. child sheet (LED reserve, v2 DNP): round-trip + in-memory union ----
    child_path = os.path.join(HERE, "led_reserve.kicad_sch")
    if os.path.exists(child_path):
        ch, ch2 = roundtrip(child_path, Schematic)
        info(f"led_reserve.kicad_sch: kiutils round-trip OK ({len(ch.schematicSymbols)} instances, "
             f"{len(ch.graphicalItems)} wires, {len(ch.globalLabels)} labels)")
        sch.graphicalItems += ch.graphicalItems
        sch.globalLabels += ch.globalLabels
        sch.noConnects += ch.noConnects
        sch.schematicSymbols += ch.schematicSymbols
        sch.libSymbols += ch.libSymbols
        info("two-sheet union: child merged in-memory for checks (global labels bridge sheets)")
    with open(os.path.join(HERE, "sgc_p1.kicad_pro")) as f:
        pro = json.load(f)
    assert pro["meta"]["filename"] == "sgc_p1.kicad_pro"
    info("sgc_p1.kicad_pro: JSON parse OK")

    STD_EXTERNAL = {"R", "C", "L", "GND"}   # KiCad standard libs (Device:*/power:GND)
    # ---- 2. lib consistency ----
    lib_names = {s.entryName for s in lib.symbols if s.unitId is None}
    sch_lib_names = {s.entryName for s in sch.libSymbols if s.unitId is None}
    for sym in sch.schematicSymbols:
        name = sym.entryName
        if name not in sch_lib_names:
            err(f"{sym.properties[0].value}: lib_id '{name}' missing from schematic lib_symbols")
        if name not in lib_names and name not in STD_EXTERNAL and not name.startswith("#"):
            err(f"{sym.properties[0].value}: lib_id '{name}' missing from sgc_p1.kicad_sym")

    # pin lookup per symbol (lib coords)
    def lib_pins(name):
        # embedded cache keys are prefixed ("sgc_p1:NAME") since the ?? fix
        cand = [s for s in sch.libSymbols if s.entryName == name or s.entryName == "sgc_p1:" + name]
        us = next(iter(cand)).units
        unit = next((u for u in us if u.unitId == 1), None) or next((u for u in us if u.pins), us[0])
        return {p.number: p for p in unit.pins}

    # ---- 3/4/5. geometry: wires -> UF, labels & power pins as net sources ----
    uf = UF()
    pts = set()
    for w in sch.graphicalItems:
        if w.type != "wire":
            continue
        a, b = key(w.points[0]), key(w.points[-1])
        pts.add(a); pts.add(b)
        uf.union(a, b)

    net_of_cluster = {}   # root -> netname (with conflict detection)
    def set_net(pt, net, what):
        r = uf.find(pt)
        cur = net_of_cluster.get(r)
        if cur and cur != net:
            err(f"SHORT/conflict at {pt}: net '{cur}' vs '{net}' ({what})")
        net_of_cluster[r] = net

    for gl in sch.globalLabels:
        pt = key(gl.position)
        if pt not in pts:
            err(f"floating global label '{gl.text}' at {pt} (not on a wire)")
        else:
            set_net(pt, gl.text, f"label {gl.text}")

    # power symbol instances impose their Value on the cluster at their pin (origin)
    for sym in sch.schematicSymbols:
        if sym.entryName in POWER_SYMBOLS:
            pt = (round(sym.position.X, 3), round(sym.position.Y, 3))
            if pt not in pts:
                err(f"power symbol {sym.properties[0].value} ({sym.entryName}) at {pt} not touching a wire")
            else:
                set_net(pt, sym.entryName, f"power {sym.entryName}")

    # no-connect positions
    nc_pts = {key(nc.position) for nc in sch.noConnects}

    # ---- per-component pin checks ----
    placed = {}  # ref -> part
    for part in PARTS:
        ref = part["ref"]
        if ref in placed:
            err(f"duplicate refdes {ref}")
        placed[ref] = part
        sym = next((s for s in sch.schematicSymbols
                    if s.entryName == part["sym"] and
                    any(p.key == "Reference" and p.value == ref for p in s.properties)), None)
        if sym is None:
            err(f"{ref}: not placed on sheet")
            continue
        lpins = lib_pins(part["sym"])
        # every netlist pin exists in symbol and vice versa
        for pin_num in part["nets"]:
            if pin_num not in lpins:
                err(f"{ref}: netlist pin '{pin_num}' not in symbol {part['sym']}")
        for pin_num in lpins:
            if pin_num not in part["nets"]:
                err(f"{ref}: symbol pin '{pin_num}' has no net assignment")
        # geometry
        for pin_num, expected_net in part["nets"].items():
            lp = lpins[pin_num]
            pt = (round(sym.position.X + lp.position.X, 3),
                  round(sym.position.Y - lp.position.Y, 3))
            if expected_net == "NC":
                if pt not in nc_pts:
                    err(f"{ref}.{pin_num}: expected no-connect flag at {pt}, none found")
                if pt in pts:
                    err(f"{ref}.{pin_num}: NC pin has a wire attached at {pt}")
                continue
            if pt not in pts:
                err(f"{ref}.{pin_num} (net {expected_net}): NO wire/label at pin point {pt}")
                continue
            got = net_of_cluster.get(uf.find(pt))
            if got != expected_net:
                err(f"{ref}.{pin_num}: expected net '{expected_net}', sheet gives '{got}' at {pt}")

    # ---- mid-span tap check: no pin point may lie strictly inside a foreign wire
    # (KiCad auto-junctions wire-end-on-mid-span taps -> hidden shorts) ----
    all_points = []
    for part in PARTS:
        sym2 = next((s for s in sch.schematicSymbols
                     if s.entryName == part["sym"] and
                     any(p.key == "Reference" and p.value == part["ref"] for p in s.properties)), None)
        if not sym2:
            continue
        lpins2 = lib_pins(part["sym"])
        for pin_num2, net2 in part["nets"].items():
            lp2 = lpins2[pin_num2]
            all_points.append(((round(sym2.position.X + lp2.position.X, 3),
                                round(sym2.position.Y - lp2.position.Y, 3)), net2, f"{part['ref']}.{pin_num2}"))
    segs = [(key(w.points[0]), key(w.points[-1])) for w in sch.graphicalItems if w.type == "wire"]
    for (pt, net, what) in all_points:
        for (a, b) in segs:
            if pt == a or pt == b:
                continue
            hit = None
            if a[0] == b[0] and pt[0] == a[0] and min(a[1], b[1]) < pt[1] < max(a[1], b[1]):
                hit = a
            elif a[1] == b[1] and pt[1] == a[1] and min(a[0], b[0]) < pt[0] < max(a[0], b[0]):
                hit = a
            if hit is not None:
                seg_net = net_of_cluster.get(uf.find(hit))
                if seg_net != net:
                    err(f"mid-span tap: {what} (net {net}) at {pt} lies inside a wire of net '{seg_net}'")

    # every no-connect belongs to a declared NC pin
    declared_nc = set()
    for part in PARTS:
        sym = next((s for s in sch.schematicSymbols
                    if s.entryName == part["sym"] and
                    any(p.key == "Reference" and p.value == part["ref"] for p in s.properties)), None)
        if not sym:
            continue
        lpins = lib_pins(part["sym"])
        for pin_num, net in part["nets"].items():
            if net == "NC":
                lp = lpins[pin_num]
                declared_nc.add((round(sym.position.X + lp.position.X, 3),
                                 round(sym.position.Y - lp.position.Y, 3)))
    for nc in nc_pts:
        if nc not in declared_nc:
            err(f"stray no-connect flag at {nc}")

    # ---- 6. census vs spec ----
    expected_ic = {"U1": "ANNA-B402", "U3": "BQ25120A", "U4": "MAX17048", "U5": "IS31FL3194",
                   "U6": "LDC1612", "U8": "BHI260AP", "U9": "BMP390", "U7": "MX25R6435F",
                   "U2": "MX25R1635F"}
    expected_j = {"J1": "USB4085", "J2": "JST-BAT-3", "J3": "SWD-2X5", "J4": "CON-3"}
    for ref, symname in expected_ic.items():
        if ref not in placed or placed[ref]["sym"] != symname:
            err(f"census: {ref} ({symname}) missing/mismatched")
    for ref, symname in expected_j.items():
        if ref not in placed or placed[ref]["sym"] != symname:
            err(f"census: {ref} ({symname}) missing/mismatched")
    n_c = sum(1 for p in placed.values() if p["sym"] == "C")
    n_r = sum(1 for p in placed.values() if p["sym"] == "R")
    n_l = sum(1 for p in placed.values() if p["sym"] == "L")
    n_d = sum(1 for p in placed.values() if p["sym"] == "LED_RGB")
    if (n_c, n_r, n_l, n_d) != (19, 12, 2, 1):
        err(f"census: passives C/R/L/D = {n_c}/{n_r}/{n_l}/{n_d}, expected 19/12/2/1 (+5 SK6812 DNP separate)")
    # datasheet pin counts
    expected_pins = {"ANNA-B402": 56, "BQ25120A": 25, "MAX17048": 9, "IS31FL3194": 8,
                     "LDC1612": 12, "BHI260AP": 44, "BMP390": 10, "MX25R6435F": 8,
                     "MX25R1635F": 8, "USB4085": 20, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "JST-BAT-3": 3, "SWD-2X5": 10}
    for name, n in expected_pins.items():
        got = len(SYMBOL_PINS[name]["pins"])
        if got != n:
            err(f"symbol {name}: {got} pins, expected {n}")

    # ---- 7. bounds & duplicates ----
    seen_at = {}
    for part in PARTS:
        x, y = part["x"], part["y"]
        if not (5 <= x <= 415 and 5 <= y <= 292):
            err(f"{part['ref']} at ({x},{y}) outside sane A3 area")
        if (x, y) in seen_at:
            err(f"{part['ref']} overlaps {seen_at[(x, y)]} at ({x},{y})")
        seen_at[(x, y)] = part["ref"]

    # ---- report ----
    total_pin_conns = sum(len(p["nets"]) for p in PARTS)
    n_labels = len(sch.globalLabels)
    info(f"census: {len(PARTS)} components "
         f"(9 U + 4 J + {n_c} C + {n_r} R + {n_l} L + {n_d} D) + {pwr_count(sch)} power symbols; "
         f"{total_pin_conns} pin connections checked; {n_labels} global labels on "
         f"{len({gl.text for gl in sch.globalLabels})} distinct nets")
    dnp = [p["ref"] for p in PARTS if p.get("dnp")]
    info(f"DNP placements: {', '.join(dnp) if dnp else 'none'}")

    # --- strict KiCad string gate: raw newline inside a quoted string = KiCad parse error ---
    for fname in (os.path.join(HERE, "sgc_p1.kicad_sym"), os.path.join(HERE, "sgc_p1.kicad_sch"), child_path):
        s = open(fname, encoding="utf-8").read()
        in_str = False
        bad = 0
        for i, ch in enumerate(s):
            if ch == '"' and (i == 0 or s[i-1] != chr(92)):
                in_str = not in_str
            elif ch == chr(10) and in_str:
                bad += 1
        if bad:
            ERRORS.append(f"{fname}: {bad} raw-newline-in-string defect(s) - KiCad parser rejects these")
        else:
            INFOS.append(f"{fname}: no raw newlines inside strings (KiCad-strict)")

    print("\n".join("  [ok] " + i for i in INFOS))
    if ERRORS:
        print("\n".join("  [FAIL] " + e for e in ERRORS))
        print(f"\nVALIDATION FAILED: {len(ERRORS)} error(s)")
        sys.exit(1)
    print("\nVALIDATION PASSED: all checks green (syntax + geometry + netlist + census)")


def pwr_count(sch):
    return sum(1 for s in sch.schematicSymbols if s.entryName in POWER_SYMBOLS)


if __name__ == "__main__":
    main()
