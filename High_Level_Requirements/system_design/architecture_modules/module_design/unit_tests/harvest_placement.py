"""Harvest KiCad GUI placement back into sgc_netlist.py.

THE PROBLEM (proven by placement_clobber_test.py)
------------------------------------------------
gen_schematic.py does not care what the .kicad_sch says -- it rebuilds every
sheet from PARTS in sgc_netlist.py. So a component JP drags in the GUI snaps
back to its hardcoded x,y on the next regeneration. Placement lives in a Python
table, not in the file JP is editing.

THE FIX THIS TOOL PROVIDES
--------------------------
Read the positions OUT of the .kicad_sch (where JP actually moves things) and
write them BACK into PARTS. Then regeneration reproduces JP's layout instead of
discarding it.

Two directions, deliberately separate:
  --report   show every part where the sheet differs from the table (read-only)
  --apply    write the sheet positions into sgc_netlist.py

--apply edits surgically: it only rewrites the x=<num>, y=<num> tokens inside
the matching dict(ref="..."). Comments, formatting and nets are untouched.
A timestamped backup is written first.

Usage:
    python harvest_placement.py --report
    python harvest_placement.py --apply
"""
import io
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
NETLIST = os.path.join(PCB, "sgc_netlist.py")


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def write(p, t):
    io.open(p, "w", encoding="utf-8", newline="").write(t)


# ---------------------------------------------------------------- sheet side

def sheet_positions():
    """{ref: (x, y, sheetfile)} from every child .kicad_sch.

    Uses kiutils when available (correct S-expression parsing); falls back to a
    structural scan that takes the (at ...) belonging to the SYMBOL itself, not
    to one of its properties.
    """
    try:
        from kiutils.schematic import Schematic
    except ImportError:
        Schematic = None

    out = {}
    for f in sorted(os.listdir(PCB)):
        if not (f.startswith("sch_") and f.endswith(".kicad_sch")):
            continue
        path = os.path.join(PCB, f)
        if Schematic is not None:
            sch = Schematic().from_file(path)
            for sym in sch.schematicSymbols:
                ref = None
                for prop in sym.properties:
                    if prop.key == "Reference":
                        ref = prop.value
                        break
                if not ref or ref.startswith("#"):
                    continue
                p = sym.position
                out[ref] = (round(float(p.X), 4), round(float(p.Y), 4), f)
        else:
            out.update(_regex_positions(path, f))
    return out


def _regex_positions(path, fname):
    """Fallback: symbol block's own (at X Y), skipping lib_symbols."""
    text = read(path)
    # cut off the lib_symbols block so definitions are not mistaken for instances
    end = text.find("(lib_symbols")
    if end >= 0:
        depth = 0
        i = end
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = text[:end] + text[i + 1:]
    else:
        body = text

    out = {}
    for m in re.finditer(r'\(symbol\b', body):
        start = m.start()
        depth = 0
        i = start
        while i < len(body):
            if body[i] == "(":
                depth += 1
            elif body[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = body[start:i + 1]
        rm = re.search(r'\(property "Reference" "([^"]+)"', block)
        am = re.search(r'\(at ([-\d.]+) ([-\d.]+)', block)
        if not rm or not am:
            continue
        ref = rm.group(1)
        if ref.startswith("#") or not re.match(r'^[A-Z]+\d+$', ref):
            continue
        out[ref] = (round(float(am.group(1)), 4), round(float(am.group(2)), 4), fname)
    return out


# --------------------------------------------------------------- table side

def table_positions():
    """{ref: (x, y)} parsed from the PARTS table."""
    text = read(NETLIST)
    out = {}
    for m in re.finditer(r'dict\(\s*ref="([^"]+)"', text):
        ref = m.group(1)
        # scan forward to the closing of this dict( at depth 0
        depth = 0
        i = m.start() + len("dict(")
        depth = 1
        while i < len(text) and depth > 0:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        block = text[m.start():i]
        xm = re.search(r'\bx=(-?[\d.]+)', block)
        ym = re.search(r'\by=(-?[\d.]+)', block)
        if xm and ym:
            out[ref] = (float(xm.group(1)), float(ym.group(1)))
    return out


def apply_positions(moves):
    """Rewrite only x= and y= inside the dict for each ref in `moves`."""
    text = read(NETLIST)
    applied = 0
    for ref, (nx, ny) in sorted(moves.items()):
        m = re.search(r'dict\(\s*ref="%s"' % re.escape(ref), text)
        if not m:
            print("  ! %s not found in table" % ref)
            continue
        depth = 1
        i = m.start() + len("dict(")
        while i < len(text) and depth > 0:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        block = text[m.start():i]

        def fmt(v):
            return ("%g" % v)

        new = re.sub(r'\bx=-?[\d.]+', "x=" + fmt(nx), block, count=1)
        new = re.sub(r'\by=-?[\d.]+', "y=" + fmt(ny), new, count=1)
        if new == block:
            print("  ! %s: no x=/y= token replaced" % ref)
            continue
        text = text[:m.start()] + new + text[i:]
        applied += 1
    if applied:
        bak = NETLIST + ".bak_" + time.strftime("%Y%m%d_%H%M%S")
        shutil.copy2(NETLIST, bak)
        print("  backup: %s" % os.path.basename(bak))
        write(NETLIST, text)
    return applied


def main():
    apply = "--apply" in sys.argv
    if not os.path.isfile(NETLIST):
        print("not found: %s" % NETLIST)
        return 2

    sheet = sheet_positions()
    table = table_positions()
    print("sheets: %d placed component(s)   table: %d part(s)\n"
          % (len(sheet), len(table)))

    moves = {}
    only_sheet, only_table = [], []
    for ref, (sx, sy, f) in sorted(sheet.items()):
        if ref not in table:
            only_sheet.append((ref, f))
            continue
        tx, ty = table[ref]
        if abs(sx - tx) > 0.01 or abs(sy - ty) > 0.01:
            moves[ref] = (sx, sy)
    for ref in sorted(table):
        if ref not in sheet:
            only_table.append(ref)

    if moves:
        print("MOVED IN THE GUI, not yet in the table (%d):" % len(moves))
        for ref, (sx, sy) in sorted(moves.items()):
            tx, ty = table[ref]
            print("  %-6s table(%8.2f,%8.2f) -> sheet(%8.2f,%8.2f)"
                  % (ref, tx, ty, sx, sy))
    else:
        print("No differences: the table already matches the sheets.")

    if only_sheet:
        print("\nIn a sheet but NOT in PARTS (%d) - would be dropped on regen:"
              % len(only_sheet))
        for ref, f in only_sheet[:15]:
            print("  %-6s %s" % (ref, f))
        if len(only_sheet) > 15:
            print("  ... and %d more" % (len(only_sheet) - 15))

    if only_table:
        print("\nIn PARTS but not found on any sheet (%d):" % len(only_table))
        print("  " + ", ".join(only_table[:20])
              + (" ..." if len(only_table) > 20 else ""))

    print()
    if not apply:
        if moves:
            print("Run with --apply to write these %d position(s) into "
                  "sgc_netlist.py." % len(moves))
        return 0

    if not moves:
        print("Nothing to apply.")
        return 0
    n = apply_positions(moves)
    print("APPLIED %d position(s) to sgc_netlist.py" % n)
    print("Regenerating now reproduces the GUI layout.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
