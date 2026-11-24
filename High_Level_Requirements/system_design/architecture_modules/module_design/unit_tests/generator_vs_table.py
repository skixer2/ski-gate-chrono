"""Decisive: what position does the GENERATOR emit for C1/C6, vs the table?

Table (sgc_netlist.py): C1 x=152,y=42   C6 x=152,y=92
Committed sheet (6a3918f6): C1 (152,42)   C6 (152,92)
Working tree sheet:         C1 (199.64,57.66)

Where does (199.64,57.66) come from? Generate to a CLEAN temp dir and read back.
If the generator emits (199.64,...) it is applying a transform and the committed
file was authored differently. If it emits (152,42) then something ELSE rewrote
the working tree.
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
REFS = ("C1", "C6", "R1", "U3")


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def pos_in(text, ref):
    idx = text.find('(property "Reference" "%s"' % ref)
    if idx < 0:
        return None
    start = text.rfind("(symbol", 0, idx)
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', text[start:idx])
    return (float(m.group(1)), float(m.group(2))) if m else None


def report(label, path):
    if not os.path.isfile(path):
        print("%-28s <missing>" % label)
        return
    t = read(path)
    parts = []
    for r in REFS:
        p = pos_in(t, r)
        parts.append("%s(%s)" % (r, "%g,%g" % p if p else "-"))
    print("%-28s %s" % (label, "  ".join(parts)))


def main():
    shp = os.path.join(SRC, "sch_power.kicad_sch")
    net = os.path.join(SRC, "sgc_netlist.py")

    print("table (sgc_netlist.py PARTS):")
    nt = read(net)
    for r in REFS:
        m = re.search(r'dict\(\s*ref="%s"[^)]*' % r, nt, re.S)
        xm = re.search(r'\bx=(-?[\d.]+)', m.group(0)) if m else None
        ym = re.search(r'\by=(-?[\d.]+)', m.group(0)) if m else None
        print("   %-4s x=%s y=%s" % (r, xm.group(1) if xm else "?",
                                     ym.group(1) if ym else "?"))

    print()
    report("LIVE working tree", shp)

    work = os.path.join(tempfile.gettempdir(), "sgc_decide")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        if f.endswith((".py", ".kicad_sym", ".kicad_pro", ".txt")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))
    if os.path.isdir(os.path.join(SRC, "sgc_p1.pretty")):
        shutil.copytree(os.path.join(SRC, "sgc_p1.pretty"),
                        os.path.join(work, "sgc_p1.pretty"), dirs_exist_ok=True)
    r = subprocess.run([sys.executable, "gen_schematic.py"], cwd=work,
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    print("\ngenerator exit=%d (clean dir, no existing sheets)" % r.returncode)
    if r.returncode != 0:
        print("--- generator output ---")
        print(((r.stdout or "") + (r.stderr or ""))[-1200:])
    report("GENERATED fresh", os.path.join(work, "sch_power.kicad_sch"))

    # and the committed version
    repo = os.path.normpath(os.path.join(SRC, "..", "..", "..", "..", "..", ".."))
    rel = "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_power.kicad_sch"
    g = subprocess.run(["git", "-C", repo, "show", "HEAD:" + rel],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if g.returncode == 0:
        tmpf = os.path.join(work, "_head.kicad_sch")
        io.open(tmpf, "w", encoding="utf-8").write(g.stdout)
        report("COMMITTED (HEAD)", tmpf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
