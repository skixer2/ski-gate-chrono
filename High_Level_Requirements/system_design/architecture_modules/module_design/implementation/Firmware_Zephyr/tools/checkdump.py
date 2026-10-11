#!/usr/bin/env python3
"""checkdump.py — inspect decoded la_y/qz around given times in dumps.
Usage: py checkdump.py <dump.log> <t1> <t2> ...   (times in payload ms)
"""
import sys
from sgc_gate_sim import frames_from_dump

fr = frames_from_dump(sys.argv[1])
print(f"{sys.argv[1]}: {len(fr)} frames")
for t in sys.argv[2:]:
    i = min(len(fr) - 1, int(t) // 10)
    seg = [(f[2][1], f[1][3]) for f in fr[max(0, i - 20):i + 5]]
    lay = [s[0] for s in seg]
    qz = [s[1] for s in seg]
    print(f"t={t}: la_y[{i-20}..{i+4}] min={min(lay)} max={max(lay)} "
          f"mean={sum(lay)//len(lay)}  qz mean={sum(qz)//len(qz)}")
