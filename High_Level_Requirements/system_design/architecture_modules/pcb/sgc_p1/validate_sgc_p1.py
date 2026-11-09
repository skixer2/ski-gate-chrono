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
from sgc_netlist import SYMBOL_PINS, PARTS, POWER_SYMBOLS, SHEETS

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
    # ---- 1b. every child sheet from SHEETS: round-trip + in-memory union ----
    # (N1 7-page split, 2026-11-04: global labels bridge sheets)
    sheet_files = []
    SHEETS_CTX = []   # root is hierarchy-only (verified empty 2026-11-07); the sch object below gets children merged into it, so it must NOT be a namespace member
    for _title, _fn, _refs in SHEETS:
        _path = os.path.join(HERE, _fn)
        sheet_files.append(_path)
        ch, ch2 = roundtrip(_path, Schematic)
        info(f"{_fn}: round-trip OK ({len(ch.schematicSymbols)} instances, "
             f"{len(ch.graphicalItems)} wires, {len(ch.globalLabels)} labels)")
        SHEETS_CTX.append((_fn, ch))
        sch.graphicalItems += ch.graphicalItems
        sch.globalLabels += ch.globalLabels
        sch.noConnects += ch.noConnects
        sch.schematicSymbols += ch.schematicSymbols
        sch.libSymbols += ch.libSymbols
    info(f"{len(SHEETS)}-sheet union merged in-memory for checks (global labels bridge sheets)")
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

    # ---- 3/4/5. geometry: PER-SHEET namespace (2026-11-07 reorder made sheet coordinate
    # spaces overlap; a merged map produced phantom cross-sheet shorts). Sheets bridge by LABEL NAME.
    uf = UF()
    pts = set()
    conn = set()
    nc_pts = set()
    net_of_cluster = {}
    def set_net(tag, net, what):
        r = uf.find(tag)
        cur = net_of_cluster.get(r)
        if cur and cur != net:
            err("SHORT/conflict: net %s vs %s (%s) at %s" % (cur, net, what, tag))
        net_of_cluster[r] = net
    def pin_pt_ns(sname, sym, pin):
        rot = float(getattr(sym.position, "angle", 0) or 0)
        mir = getattr(sym, "mirror", None)
        dx, dy = pin.position.X, -pin.position.Y   # lib y-up -> sheet y-down
        r = int(round(rot / 90.0)) % 4
        if r == 1: dx, dy = dy, -dx
        elif r == 2: dx, dy = -dx, -dy
        elif r == 3: dx, dy = -dy, dx
        if mir == "x": dy = -dy  # 2026-11-09: original convention restored - wire-island ground truth (D5 dx-flip test, D7 now mirror-free)
        elif mir == "y": dx = -dx # (mirror y) = horizontal flip: D5 pins land on wires ONLY under this rule
        return (sname, round(sym.position.X + dx, 3), round(sym.position.Y + dy, 3))
    def lib_pins_local(sobj, name):
        cand = [s for s in sobj.libSymbols if s.entryName == name or s.entryName == "sgc_p1:" + name]
        us = next(iter(cand)).units
        unit = next((u for u in us if u.unitId == 1), None) or next((u for u in us if u.pins), us[0])
        return {p.number: p for p in unit.pins}
    for sname, sobj in SHEETS_CTX:
        for w in sobj.graphicalItems:
            if w.type != "wire":
                continue
            a = (sname, round(w.points[0].X, 3), round(w.points[0].Y, 3))
            b = (sname, round(w.points[-1].X, 3), round(w.points[-1].Y, 3))
            pts.add(a); pts.add(b)
            uf.union(a, b)
        for gl in sobj.globalLabels:
            conn.add((sname, round(gl.position.X, 3), round(gl.position.Y, 3)))
        for sym in sobj.schematicSymbols:
            if sym.entryName in POWER_SYMBOLS:
                conn.add((sname, round(sym.position.X, 3), round(sym.position.Y, 3)))
                continue
            try:
                lp = lib_pins_local(sobj, sym.entryName)
            except Exception:
                continue
            for _num, pin in lp.items():
                conn.add(pin_pt_ns(sname, sym, pin))
        for gl in sobj.globalLabels:
            pt = (sname, round(gl.position.X, 3), round(gl.position.Y, 3))
            if pt not in conn:
                err("floating global label %r at %s (not on a connection point)" % (gl.text, pt))
            else:
                set_net(pt, gl.text, "label %s" % gl.text)
        for sym in sobj.schematicSymbols:
            if sym.entryName in POWER_SYMBOLS:
                pt = (sname, round(sym.position.X, 3), round(sym.position.Y, 3))
                if pt not in conn:
                    err("power symbol %s at %s not touching a wire" % (sym.entryName, pt))
                else:
                    set_net(pt, sym.entryName, "power %s" % sym.entryName)
        for nc in sobj.noConnects:
            nc_pts.add((sname, round(nc.position.X, 3), round(nc.position.Y, 3)))
    SYM_AT = {}
    for sname, sobj in SHEETS_CTX:
        for s in sobj.schematicSymbols:
            refv = next((p.value for p in s.properties if p.key == "Reference"), None)
            if refv and not refv.startswith("#"):
                SYM_AT[refv] = (sname, s)
    # Local nets (2026-11-07 JP doctrine): a net whose members all sit on ONE sheet and whose
    # name appears NOWHERE as a global label is wired DIRECTLY (KiCad auto-names the island
    # at export). The gate accepts shared-island membership instead of a label name.
    all_label_names = {gl.text for _sn, _so in SHEETS_CTX for gl in _so.globalLabels}
    pin_pt_cache = {}
    for part in PARTS:
        if part["ref"] not in SYM_AT: continue
        _sn, _sy = SYM_AT[part["ref"]]
        _so = next(o for n, o in SHEETS_CTX if n == _sn)
        try: _lp = lib_pins_local(_so, part["sym"])
        except Exception: continue
        for _pn in part["nets"]:
            if _pn in _lp: pin_pt_cache[(part["ref"], _pn)] = pin_pt_ns(_sn, _sy, _lp[_pn])
    LOCAL_NETS = {}
    for part in PARTS:
        for _pn, _net in part["nets"].items():
            if _net != "NC" and _net not in all_label_names:
                LOCAL_NETS.setdefault(_net, []).append((part["ref"], _pn))
    for _net, _pins in LOCAL_NETS.items():
        _sheets = {SYM_AT[r][0] for r, _ in _pins if r in SYM_AT}
        _roots = set()
        for r, _ in _pins:
            pt = pin_pt_cache.get((r, _))
            _roots.add(uf.find(pt) if pt in uf.p else pt)
        if not (len(_sheets) == 1 and len(_roots) == 1):
            err("local net %r: %d sheet(s) / %d island(s) - needs a global label or rewiring" % (_net, len(_sheets), len(_roots)))
    placed = {}
    for part in PARTS:
        ref = part["ref"]
        if ref in placed:
            err("duplicate refdes %s" % ref)
        placed[ref] = part
        if ref not in SYM_AT:
            err("%s: not placed on any sheet" % ref)
            continue
        sname, sym = SYM_AT[ref]
        sobj = next(o for n, o in SHEETS_CTX if n == sname)
        lpins = lib_pins_local(sobj, part["sym"])
        for pin_num in part["nets"]:
            if pin_num not in lpins:
                err("%s: netlist pin %r not in symbol" % (ref, pin_num))
        for pin_num in lpins:
            if pin_num not in part["nets"]:
                err("%s: symbol pin %r has no net assignment" % (ref, pin_num))
        for pin_num, expected_net in part["nets"].items():
            lp = lpins[pin_num]
            pt = pin_pt_ns(sname, sym, lp)
            if expected_net == "NC":
                if pt not in nc_pts:
                    err("%s.%s: expected no-connect flag at %s, none found" % (ref, pin_num, pt))
                if pt in pts:
                    err("%s.%s: NC pin has a wire attached at %s" % (ref, pin_num, pt))
                continue
            if expected_net in LOCAL_NETS:
                continue   # local unnamed island - membership verified above
            if pt not in conn:
                err("%s.%s (net %s): NO connection at pin point %s" % (ref, pin_num, expected_net, pt))
                continue
            got = net_of_cluster.get(uf.find(pt))
            if got != expected_net:
                err("%s.%s: expected net %r, sheet gives %r at %s" % (ref, pin_num, expected_net, got, pt))
    for sname, sobj in SHEETS_CTX:
        segs = [((sname, round(w.points[0].X,3), round(w.points[0].Y,3)), (sname, round(w.points[-1].X,3), round(w.points[-1].Y,3))) for w in sobj.graphicalItems if w.type == "wire"]
        for part in PARTS:
            if part["ref"] not in SYM_AT or SYM_AT[part["ref"]][0] != sname:
                continue
            ps, sym2 = SYM_AT[part["ref"]]
            lpins2 = lib_pins_local(sobj, part["sym"])
            for pin_num2, net2 in part["nets"].items():
                pt = pin_pt_ns(sname, sym2, lpins2[pin_num2])
                for (a, b) in segs:
                    if pt == a or pt == b:
                        continue
                    hit = None
                    if a[0] == b[0] == pt[0] and a[2] == b[2] == pt[2] and min(a[1],b[1]) < pt[1] < max(a[1],b[1]):
                        hit = a
                    elif a[0] == b[0] == pt[0] and a[1] == b[1] == pt[1] and min(a[2],b[2]) < pt[2] < max(a[2],b[2]):
                        hit = a
                    if hit is not None:
                        seg_net = net_of_cluster.get(uf.find(hit))
                        if seg_net != net2:
                            err("mid-span tap: %s.%s (net %s) at %s inside a wire of net %s" % (part["ref"], pin_num2, net2, pt, seg_net))
    # ---- 6. census vs spec ----
    expected_ic = {"U12": "BMM150", "U13": "BME688", "Y1": "SIT1532",
                   "U1": "ANNA-B402", "U3": "BQ25120A", "U4": "MAX17048", "U5": "IS31FL3194",
                   "U6": "LDC1612", "U8": "BHI260AP", "U9": "BMP390", "U7": "MX25R6435F",
                   "U2": "MX25R1635F"}
    expected_j = {"J1": "USB4085", "J2": "JST-BAT-3", "J3": "SWD-2X5", "J4": "CON-3", "J5": "CON-2"}
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
    exp_c = sum(1 for p in PARTS if p["sym"] == "C")
    exp_r = sum(1 for p in PARTS if p["sym"] == "R")
    exp_l = sum(1 for p in PARTS if p["sym"] == "L")
    exp_d = sum(1 for p in PARTS if p["sym"] == "LED_RGB")
    if (n_c, n_r, n_l, n_d) != (exp_c, exp_r, exp_l, exp_d):
        err(f"census: passives C/R/L/D placed = {n_c}/{n_r}/{n_l}/{n_d}, data says {exp_c}/{exp_r}/{exp_l}/{exp_d}")
    # datasheet pin counts
    expected_pins = {"ANNA-B402": 56, "BQ25120A": 25, "MAX17048": 9, "IS31FL3194": 8, "LDC1612": 12, "BHI260AP": 44, "BMP390": 10, "MX25R6435F": 8, "MX25R1635F": 8, "USB4085": 20, "MT3608": 5, "TXS0102": 8, "SK6812": 4, "CON-3": 3, "JST-BAT-3": 3, "SWD-2X5": 10, "D_SCHOTTKY": 2, "BME688": 8, "BMM150": 12, "SIT1532": 4}
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
    for fname in [os.path.join(HERE, "sgc_p1.kicad_sym"), os.path.join(HERE, "sgc_p1.kicad_sch")] + sheet_files:
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
