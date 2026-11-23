import os, re, glob, json

R   = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PCB = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1")
FPROOT = r"C:\Progra~1\KiCad\10.0\share\kicad\footprints"

print("=== lib tables in project ===")
for f in ("fp-lib-table", "sym-lib-table"):
    p = os.path.join(PCB, f)
    print(f"  {f}: {'EXISTS' if os.path.exists(p) else 'MISSING'}")
    if os.path.exists(p):
        print(open(p, encoding="utf-8", errors="ignore").read()[:600])

print("\n=== template: MountingHole_2.2mm_M2 ===")
tpl = os.path.join(FPROOT, "MountingHole.pretty", "MountingHole_2.2mm_M2.kicad_mod")
if os.path.exists(tpl):
    print(open(tpl, encoding="utf-8", errors="ignore").read())
else:
    # find any plain NPTH mounting hole
    for g in sorted(glob.glob(os.path.join(FPROOT, "MountingHole.pretty", "MountingHole_*mm.kicad_mod")))[:3]:
        print("---", os.path.basename(g), "---")
        print(open(g, encoding="utf-8", errors="ignore").read())

print("\n=== all parts with fp=E ===")
NET = os.path.join(PCB, "sgc_netlist.py")
src = open(NET, encoding="utf-8", errors="ignore").read()
for m in re.finditer(r'ref="([^"]+)",\s*sym="([^"]+)",[^)]*?fp=(E|"([^"]*)")', src):
    if m.group(3) == "E":
        print(f"  {m.group(1):8s} {m.group(2)}")

print("\n=== how gen_schematic handles fp ===")
for f in ("gen_schematic.py", "validate_sgc_p1.py"):
    p = os.path.join(PCB, f)
    if not os.path.exists(p):
        print(f"  {f}: MISSING"); continue
    s = open(p, encoding="utf-8", errors="ignore").read()
    print(f"\n--- {f} (fp references) ---")
    for i, ln in enumerate(s.splitlines(), 1):
        if "fp=" in ln or "Footprint" in ln or "'fp'" in ln or '"fp"' in ln:
            print(f"  {i}: {ln.strip()[:130]}")
