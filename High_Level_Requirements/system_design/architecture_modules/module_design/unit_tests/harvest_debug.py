"""Why did harvest --apply report success but leave C1 unchanged?

apply_positions() applied 18 changes, yet C1 in the table is still (152,42).
Likely the regex `dict\(\\s*ref="C1"` matched and the sub replaced, but the text
slice boundaries were wrong -- i.e. `block` did not contain the x=/y= tokens, or
the replacement was written to a stale copy.

Print exactly what apply_positions() computes for C1 in the sandbox.
"""
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harvest_placement as H

WORK = os.path.join(os.environ.get("TEMP", r"C:\WINDOWS\TEMP"), "sgc_harvest_rt")
net = os.path.join(WORK, "sgc_netlist.py")
print("file: %s  exists=%s\n" % (net, os.path.isfile(net)))

text = H.read(net)
print("table_positions() says C1 = %s\n" % (H.table_positions().get("C1"),))

m = re.search(r'dict\(\s*ref="C1"', text)
print("regex match on C1: %s" % (m.span() if m else None))
if m:
    depth = 1
    i = m.start() + len("dict(")
    while i < len(text) and depth > 0:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
        i += 1
    block = text[m.start():i]
    print("\n--- computed block (len %d) ---" % len(block))
    print(repr(block[:400]))
    xm = re.search(r'\bx=(-?[\d.]+)', block)
    ym = re.search(r'\by=(-?[\d.]+)', block)
    print("\nx match=%s  y match=%s" % (xm.group(0) if xm else None,
                                        ym.group(0) if ym else None))
    new = re.sub(r'\bx=-?[\d.]+', "x=999", block, count=1)
    print("did sub change anything: %s" % (new != block))
