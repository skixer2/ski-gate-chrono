"""Inspect sgc_p1.kicad_pro for anything that makes a load fail.

A stale or partially-written project file produces load errors that have
nothing to do with the .kicad_sch contents.
"""
import io
import json
import os
import re

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
P = os.path.join(D, "sgc_p1.kicad_pro")

raw = io.open(P, "rb").read()
print("size: %d bytes" % len(raw))
print("first bytes: %r" % raw[:80])
print()

try:
    j = json.loads(raw.decode("utf-8"))
    print("JSON: VALID")
except Exception as e:
    print("JSON: INVALID -> %s" % e)
    raise SystemExit(1)

print("meta: %s" % json.dumps(j.get("meta", {})))
print()

# sheets section records the hierarchy; a stale one can disagree with the files
sheets = j.get("sheets")
print("sheets key: %s" % ("absent" if sheets is None else json.dumps(sheets)[:400]))
print()

s = j.get("schematic", {})
print("schematic keys: %s" % sorted(s.keys()))
print()

# look for absolute paths anywhere (they break on another machine)
text = raw.decode("utf-8")
abs_paths = sorted(set(re.findall(r'"([A-Za-z]:\\\\[^"]{3,120})"', text)))
print("absolute paths: %s" % (abs_paths if abs_paths else "none"))
print()

# KiCad version markers
for k in ("version", "kicad_version", "generator"):
    if k in j:
        print("%s: %s" % (k, j[k]))

# referenced sheet files
refs = sorted(set(re.findall(r'"([^"]*\.kicad_sch)"', text)))
print("referenced sheets: %s" % (refs if refs else "none"))
for r in refs:
    exists = os.path.isfile(os.path.join(D, os.path.basename(r)))
    print("   %-40s exists=%s" % (r, exists))
