"""Dump the exact syntax of sheet boxes and instances blocks in the root sheet."""
import io
import os
import re

D = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "..", "..", "pcb", "sgc_p1"))
p = os.path.join(D, "sgc_p1.kicad_sch")
t = io.open(p, "rb").read().decode("utf-8", "replace")

print("file: %s  (%d bytes)" % (p, len(t)))
print("count '(sheet'        : %d" % t.count("(sheet"))
print("count '(sheet\\n'      : %d" % t.count("(sheet\n"))
print("count '(sheet '       : %d" % t.count("(sheet "))
print("count '(instances'    : %d" % t.count("(instances"))
print("count '(project '     : %d" % t.count("(project "))

# show the first sheet block verbatim
i = t.find("(sheet")
print("\n--- first 1400 chars from first (sheet ---")
print(t[i:i + 1400] if i >= 0 else "NO (sheet FOUND")
