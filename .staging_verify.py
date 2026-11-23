import os, re, json

R   = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PCB = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1")
NET = os.path.join(PCB, "sgc_netlist.py")
src = open(NET, encoding="utf-8", errors="ignore").read()

print("=== CON-3 (J4) spec ===")
i = src.find('"CON-3"')
if i >= 0:
    print(src[i:i+700])

print("\n=== check QSPI pin types in BHI260AP symbol ===")
m = re.search(r'"BHI260AP":\s*dict\((.*?)\n\s{4}\)', src, re.S)
if m:
    blk = m.group(1)
    for ln in blk.splitlines():
        if "QSPI" in ln:
            print("  ", ln.strip())

print("\n=== nets with ONLY input-type pins (QSPI bus both ends?) ===")
# find which components use QSPI nets
for net in ("QSPI_CS", "QSPI_SCK", "QSPI_IO0"):
    print(f"\n  {net}:")
    for m2 in re.finditer(r'ref="([^"]+)"[^)]*?nets=\{([^}]*)\}', src):
        if net in m2.group(2):
            print(f"     used by {m2.group(1)}")

print("\n=== PWR_FLAG present anywhere? ===")
print("  PWR_FLAG in netlist.py:", "PWR_FLAG" in src)
print("  PWR_FLAG in gen_schematic.py:",
      "PWR_FLAG" in open(os.path.join(PCB, "gen_schematic.py"),
                         encoding="utf-8", errors="ignore").read())
