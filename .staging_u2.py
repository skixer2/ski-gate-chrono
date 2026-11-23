import re, os, ast

P = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1\sgc_netlist.py"
src = open(P, encoding="utf-8", errors="ignore").read()

# find the component definition blocks for U2, U7, U9, U13, D6
for ref in ("U2", "U7", "U9", "U13", "D6", "J3"):
    m = re.search(r'dict\(ref="%s",.*?\n\s*\)' % ref, src, re.S)
    if not m:
        print(f"{ref}: NOT FOUND"); continue
    blk = m.group(0)
    print("=" * 60)
    print(blk[:900])
    print()

print("#" * 60)
print("POWER SYMBOL DEFINITIONS (#PWR01 / #PWR02)")
for m in re.finditer(r'.*#PWR01.*', src):
    print(" ", m.group(0).strip()[:160])
for m in re.finditer(r'.*#PWR02.*', src):
    print(" ", m.group(0).strip()[:160])

print()
print("#" * 60)
print("RAIL DEFINITIONS - what drives +1V8")
for ln in src.splitlines():
    if "+1V8" in ln and ("power" in ln.lower() or "PWR" in ln or "VOUT" in ln or "reg" in ln.lower()):
        print(" ", ln.strip()[:150])
