"""Prove harvest_placement.py round-trips a GUI move without touching the repo.

Builds a sandbox copy of the project, simulates a group move in a sheet, harvests
it into the table copy, regenerates, and checks the position SURVIVED.

This is the answer to JP's question made executable:
  "if I move things in the GUI, can you keep them?"

Sandbox only. The live repo and sgc_netlist.py are never modified.
"""
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
REF = "C1"
SHIFT = (30.0, 15.0)


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def write(p, t):
    io.open(p, "w", encoding="utf-8", newline="").write(t)


def pos_in(text, ref):
    i = text.find('(property "Reference" "%s"' % ref)
    if i < 0:
        return None
    s = text.rfind("(symbol", 0, i)
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', text[s:i])
    return (float(m.group(1)), float(m.group(2))) if m else None


def table_xy(text, ref):
    m = re.search(r'dict\(\s*ref="%s"[^)]*' % re.escape(ref), text, re.S)
    if not m:
        return None
    xm = re.search(r'\bx=(-?[\d.]+)', m.group(0))
    ym = re.search(r'\by=(-?[\d.]+)', m.group(0))
    return (float(xm.group(1)), float(ym.group(1))) if xm and ym else None


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_harvest_rt")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        s = os.path.join(SRC, f)
        if os.path.isfile(s) and not f.endswith((".pdf", ".kicad_pcb")):
            shutil.copy2(s, os.path.join(work, f))
    if os.path.isdir(os.path.join(SRC, "sgc_p1.pretty")):
        shutil.copytree(os.path.join(SRC, "sgc_p1.pretty"),
                        os.path.join(work, "sgc_p1.pretty"), dirs_exist_ok=True)

    shp = os.path.join(work, "sch_power.kicad_sch")
    net = os.path.join(work, "sgc_netlist.py")

    t = read(shp)
    start = pos_in(t, REF)
    if not start:
        print("setup: %s not found" % REF)
        return 2
    print("STEP 1  simulate a GUI move of %s by %s" % (REF, SHIFT))
    print("        before: (%g, %g)" % start)

    # move BOTH the symbol's (at) and the table's x/y stay untouched (table is
    # the stale source we must harvest FROM the sheet)
    i = t.find('(property "Reference" "%s"' % REF)
    s = t.rfind("(symbol", 0, i)
    block = t[s:i]
    newx, newy = start[0] + SHIFT[0], start[1] + SHIFT[1]
    nb = re.sub(r'\(at [-\d.]+ [-\d.]+', "(at %g %g" % (newx, newy), block, count=1)
    write(shp, t[:s] + nb + t[i:])
    moved = pos_in(read(shp), REF)
    print("        after : (%g, %g)" % moved)

    print("\nSTEP 2  table before harvest: %s" % (table_xy(read(net), REF),))

    print("\nSTEP 3  harvest --apply (sandbox)")
    r = subprocess.run([sys.executable, os.path.join(HERE, "harvest_placement.py"),
                        "--apply"], cwd=work, capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    out = (r.stdout or "") + (r.stderr or "")
    for ln in out.splitlines():
        if any(k in ln for k in ("APPLIED", "backup", "!")):
            print("        " + ln)
    if r.returncode != 0:
        print(out[-600:])
        return 1
    tbl = table_xy(read(net), REF)
    print("        table after harvest: %s" % (tbl,))

    print("\nSTEP 4  regenerate in the sandbox")
    g = subprocess.run([sys.executable, "gen_schematic.py"], cwd=work,
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    if g.returncode != 0:
        print("        generator failed")
        print(((g.stdout or "") + (g.stderr or ""))[-600:])
        return 1
    final = pos_in(read(shp), REF)
    print("        %s after regeneration: %s" % (REF, final))

    print()
    ok = final and abs(final[0] - newx) < 0.01 and abs(final[1] - newy) < 0.01
    if ok:
        print("ROUND TRIP WORKS:")
        print("  GUI move (%g,%g) -> harvested into table -> regeneration kept it."
              % (newx, newy))
        print("  So JP's placements CAN be preserved: run harvest_placement.py")
        print("  --apply BEFORE gen_schematic.py.")
        return 0
    print("ROUND TRIP FAILED - placement still lost after harvest.")
    print("  expected (%g,%g), got %s" % (newx, newy, final))
    return 1


if __name__ == "__main__":
    sys.exit(main())
