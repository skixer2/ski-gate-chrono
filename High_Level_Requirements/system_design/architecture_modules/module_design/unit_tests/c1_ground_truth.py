"""Direct ground-truth check of C1's position in the live sheet.

harvest_placement.py (kiutils) said C1 is at (199.64, 57.66).
placement_origin.py (regex, on HEAD blob) said (152, 42).
One is wrong. Print the raw text around C1 and let the file decide.
"""
import io
import os
import re

PCB = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "..", "pcb", "sgc_p1"))
for fname in ("sch_power.kicad_sch",):
    p = os.path.join(PCB, fname)
    raw = io.open(p, "rb").read()
    try:
        t = raw.decode("utf-8")
    except UnicodeDecodeError:
        t = raw.decode("cp1252", "replace")
    print("%s  %d bytes  CRLF=%s\n" % (fname, len(t), "\r\n" in t))

    idx = t.find('(property "Reference" "C1"')
    print("property index: %d" % idx)
    start = t.rfind("(symbol", 0, idx)
    print("enclosing (symbol index: %d\n" % start)
    print("--- raw block from (symbol to end of C1's Reference property ---")
    print(repr(t[start:idx + 40]))
    print()
    print("--- all (at ...) tokens in that block ---")
    for m in re.finditer(r'\(at ([-\d.]+) ([-\d.]+)', t[start:idx]):
        print("   (at %s %s)" % (m.group(1), m.group(2)))
    print()

    # and what kiutils reports
    try:
        from kiutils.schematic import Schematic
        sch = Schematic().from_file(p)
        for sym in sch.schematicSymbols:
            ref = next((pr.value for pr in sym.properties
                        if pr.key == "Reference"), None)
            if ref == "C1":
                print("kiutils: C1 position = (%.4f, %.4f)"
                      % (sym.position.X, sym.position.Y))
                print("kiutils: lib_id = %s" % sym.libId)
                break
        else:
            print("kiutils: C1 not found")
    except Exception as e:
        print("kiutils failed: %s" % e)
