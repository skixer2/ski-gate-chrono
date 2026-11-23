import glob
import io
import os
import re
import sys

BASE = sys.argv[1] if len(sys.argv) > 1 else r"C:\sgc\gentest"

tot_q = tot_b = 0
for f in sorted(glob.glob(os.path.join(BASE, "*.kicad_sch"))):
    t = io.open(f, encoding="utf-8", errors="replace").read()
    q = re.findall(r'\(uuid\s+"([^"]+)"', t)
    b = re.findall(r'\(uuid\s+([^"\s()][^\s()]*)', t)
    tot_q += len(q)
    tot_b += len(b)
    print("%-28s quoted %4d   bare %4d" % (os.path.basename(f), len(q), len(b)))

print("-" * 56)
print("TOTAL quoted %d   bare %d" % (tot_q, tot_b))
if tot_b:
    print("FAIL - bare uuids present")
    sys.exit(1)
print("PASS - every uuid is quoted")
