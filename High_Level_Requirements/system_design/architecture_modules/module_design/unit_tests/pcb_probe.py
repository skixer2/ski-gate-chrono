"""Inspect the PCB file: does it exist, what format, does it reference sheet uuids?"""
import io
import os
import re

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
for name in ("sgc_p1.kicad_pcb", "sgc_p1.kicad_pro"):
    p = os.path.join(D, name)
    if not os.path.isfile(p):
        print("%-22s MISSING" % name)
        continue
    st = os.stat(p)
    raw = io.open(p, "rb").read()
    try:
        t = raw.decode("utf-8")
    except UnicodeDecodeError:
        t = raw.decode("cp1252", "replace")
    ver = re.search(r'\(version (\d+)\)', t)
    gen = re.search(r'\(generator "?([\w.]+)"?\)', t)
    print("%-22s %8d bytes  version=%s  generator=%s"
          % (name, st.st_size, ver.group(1) if ver else "?",
             gen.group(1) if gen else "?"))
    if name.endswith(".kicad_pcb"):
        print("   footprints: %d" % t.count("(footprint "))
        print("   nets:       %d" % t.count("(net "))
        print("   has (path : %s" % ("(path " in t))
        uu = set(re.findall(r'\(path "([^"]*)"\)', t))
        print("   distinct path values: %d" % len(uu))
        for u in sorted(uu)[:8]:
            print("      %s" % u)
