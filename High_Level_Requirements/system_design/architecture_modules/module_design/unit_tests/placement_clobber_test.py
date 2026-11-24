"""Does regenerating DISCARD placement edits made in the KiCad GUI?

JP's question, verbatim: "If I modify the schematic, should I regenerate? Will
you be able to incorporate my modifications so that further ones introduced by
you don't completely discard my component placements?"

This test answers it empirically instead of by opinion:

  1. copy the live project to a scratch dir
  2. move a component in the .kicad_sch, exactly as the GUI would (change its
     (at X Y) and its instance path stays the same) -- simulate a JP edit
  3. run gen_schematic.py in place
  4. did the move survive?

Result tells us which workflow is safe.
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
GEN = "gen_schematic.py"
SHEET = "sch_power.kicad_sch"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def write(p, t):
    io.open(p, "w", encoding="utf-8", newline="").write(t)


def find_symbol_pos(text, ref):
    """Return (start, end, x, y) of the (at X Y ...) of the symbol with ref."""
    i = text.find('(property "Reference" "%s"' % ref)
    if i < 0:
        return None
    # the symbol's own (at ...) appears before the properties
    j = text.rfind("(symbol", 0, i)
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', text[j:i])
    if not m:
        return None
    return j, m.start(), float(m.group(1)), float(m.group(2))


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_place")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        s = os.path.join(SRC, f)
        if os.path.isfile(s) and not f.endswith((".pdf", ".kicad_pcb")):
            shutil.copy2(s, os.path.join(work, f))

    p = os.path.join(work, SHEET)
    t = read(p)

    # pick a real component present on that sheet
    cands = re.findall(r'\(property "Reference" "([A-Z]+\d+)"', t)
    cands = [c for c in cands if not c.startswith("#")]
    if not cands:
        print("no components on %s" % SHEET)
        return 2
    ref = cands[0]
    info = find_symbol_pos(t, ref)
    if not info:
        print("could not locate symbol %s" % ref)
        return 2
    _, at, x0, y0 = info
    print("simulating a GUI edit: move %s from (%g, %g)" % (ref, x0, y0))

    # shift by +30mm, exactly what dragging does
    newx, newy = x0 + 30.0, y0 + 15.0
    t2 = t[:at] + "(at %g %g" % (newx, newy) + t[at + len("(at %g %g" % (x0, y0)):]
    write(p, t2)

    after_edit = find_symbol_pos(read(p), ref)
    print("  moved to (%.4g, %.4g)\n" % (after_edit[2], after_edit[3]))

    r = subprocess.run([sys.executable, GEN], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    print("regenerate exit=%d" % r.returncode)
    if r.returncode != 0:
        print(((r.stdout or "") + (r.stderr or ""))[-500:])
        return 1

    final = find_symbol_pos(read(p), ref)
    if not final:
        print("%s VANISHED after regeneration" % ref)
        return 1
    fx, fy = final[2], final[3]
    print("after regeneration: %s at (%.4g, %.4g)" % (ref, fx, fy))

    print()
    kept = abs(fx - newx) < 1e-6 and abs(fy - newy) < 1e-6
    if kept:
        print("PLACEMENT PRESERVED - the generator picked up the GUI edit.")
        return 0

    reverted = abs(fx - x0) < 1e-6 and abs(fy - y0) < 1e-6
    print("PLACEMENT DISCARDED.")
    print("  GUI edit:  (%g, %g)" % (newx, newy))
    print("  after regen: (%g, %g)" % (fx, fy))
    if reverted:
        print("  -> reverted to the ORIGINAL coordinate from sgc_netlist.py")
        print()
        print("CONFIRMED ANSWER TO JP:")
        print("  Component positions are owned by gen_schematic.py / sgc_netlist.py")
        print("  (the PARTS table), NOT by the .kicad_sch. Any move made in the")
        print("  KiCad GUI is overwritten on the next regeneration.")
    else:
        print("  -> moved somewhere unexpected; investigate generator logic")
    return 1


if __name__ == "__main__":
    sys.exit(main())
