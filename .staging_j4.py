import os, re, glob

R   = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PCB = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1")
NET = os.path.join(PCB, "sgc_netlist.py")
src = open(NET, encoding="utf-8", errors="ignore").read()

def grab(ref):
    i = src.find('ref="%s"' % ref)
    if i < 0: return None
    start = src.rfind("dict(", 0, i); d = 0
    for j in range(start, len(src)):
        if src[j] == "(": d += 1
        elif src[j] == ")":
            d -= 1
            if d == 0: return src[start:j+1]
    return None

print("=== J4 identity ===")
print(grab("J4"))

print("\n=== sgc_p1.pretty contents ===")
pp = os.path.join(PCB, "sgc_p1.pretty")
print("exists:", os.path.isdir(pp))
if os.path.isdir(pp):
    for f in sorted(glob.glob(os.path.join(pp, "*.kicad_mod"))):
        print("   ", os.path.basename(f))

print("\n=== MH1/MH2 definition ===")
for r_ in ("MH1", "MH2"):
    print(grab(r_))

print("\n=== spec for MountingHole / jig ===")
for ln in src.splitlines():
    if "MountingHole" in ln or "MOUNTING" in ln or "1.6mm" in ln:
        print("  ", ln.strip()[:170])
