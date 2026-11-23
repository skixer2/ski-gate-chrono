import os, re, glob, json

R   = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PCB = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1")
FPROOT = r"C:\Progra~1\KiCad\10.0\share\kicad\footprints"

out = []
libdirs = sorted(glob.glob(os.path.join(FPROOT, "*.pretty")))
allmods = []
for d in libdirs:
    lib = os.path.basename(d)[:-7]
    for m in glob.glob(os.path.join(d, "*.kicad_mod")):
        allmods.append((lib, os.path.basename(m)[:-10]))
out.append(f"total footprints indexed: {len(allmods)} across {len(libdirs)} libs")

# --- A. Mounting holes near 1.6mm ---
out.append("\n=== A. MountingHole candidates (1.5-1.8mm) ===")
for lib, mod in allmods:
    if lib != "MountingHole":
        continue
    m = re.search(r"_([\d.]+)mm", mod)
    if not m:
        if "1.6" in mod:
            out.append(f"  EXACT-FRAG  {mod}")
        continue
    try:
        v = float(m.group(1))
    except ValueError:
        continue
    if 1.4 <= v <= 1.9:
        out.append(f"  {v:5.2f}  {mod}")
out.append("  -- also checking for 1.6 anywhere --")
for lib, mod in allmods:
    if "1.6mm" in mod and lib in ("MountingHole", "TestPoint"):
        out.append(f"  FRAG {lib}:{mod}")

# --- B. SOIC-8 near 5.2-5.4mm ---
out.append("\n=== B. SOIC-8 around 5.2-5.4mm ===")
for lib, mod in allmods:
    if lib not in ("Package_SO", "Package_SO-1", "Package_DIP"):
        continue
    if not mod.startswith("SOIC-8"):
        continue
    m = re.search(r"(\d+\.?\d*)x(\d+\.?\d*)mm_P(\d+\.?\d*)", mod)
    if m:
        w, h, p = float(m.group(1)), float(m.group(2)), float(m.group(3))
        if 5.0 <= w <= 5.5:
            out.append(f"  {w:.2f}x{h:.2f} P{p}  {lib}:{mod}")
out.append("  -- all SOIC-8 with P1.27 (first 25) --")
n = 0
for lib, mod in allmods:
    if mod.startswith("SOIC-8") and "P1.27" in mod and "EP_" not in mod:
        out.append(f"  {lib}:{mod}")
        n += 1
        if n >= 25:
            break

# --- C. JST SH 3-pin 1.00mm ---
out.append("\n=== C. JST SH 3-pin 1.00mm (all orientations) ===")
for lib, mod in allmods:
    if "SH_" in mod and "1x03" in mod and "P1.00" in mod:
        out.append(f"  {lib}:{mod}")

# --- D. What is U8? ---
out.append("\n=== D. U8 identity ===")
NET = os.path.join(PCB, "sgc_netlist.py")
src = open(NET, encoding="utf-8", errors="ignore").read()
i = src.find('ref="U8"')
if i >= 0:
    start = src.rfind("dict(", 0, i)
    d = 0
    for j in range(start, len(src)):
        if src[j] == "(":
            d += 1
        elif src[j] == ")":
            d -= 1
            if d == 0:
                out.append(src[start:j+1][:1600])
                break
else:
    out.append("U8 NOT FOUND in netlist.py")

# --- E. QSPI net usage ---
out.append("\n=== E. QSPI_* net occurrences in netlist ===")
for ln in src.splitlines():
    if "QSPI" in ln:
        out.append("  " + ln.strip()[:150])

print("\n".join(out))
