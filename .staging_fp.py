import os, re, glob, json, subprocess, sys

R   = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PCB = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1")
KCLI = r"C:\Progra~1\KiCad\10.0\bin\kicad-cli.exe"
FPROOT = r"C:\Progra~1\KiCad\10.0\share\kicad\footprints"

out = []

# ---------- 1. Ports (did COM4 appear?) ----------
out.append("=== 1. SERIAL PORTS ===")
try:
    import serial.tools.list_ports as lp
    ps = list(lp.comports())
    out.append(f"count={len(ps)}")
    for p in ps:
        out.append(f"  {p.device} | {p.description} | hwid={p.hwid[:60]}")
except Exception as e:
    out.append(f"ERR {e}")

# ---------- 2. U2 / U7 nets ----------
out.append("\n=== 2. U2 vs U7 net maps ===")
NET = os.path.join(PCB, "sgc_netlist.py")
src = open(NET, encoding="utf-8", errors="ignore").read()

def grab(ref):
    i = src.find('ref="%s"' % ref)
    if i < 0: return None
    start = src.rfind("dict(", 0, i)
    d = 0
    for j in range(start, len(src)):
        if src[j] == "(": d += 1
        elif src[j] == ")":
            d -= 1
            if d == 0: return src[start:j+1]
    return None

for ref in ("U2", "U7"):
    b = grab(ref)
    out.append(f"\n--- {ref} ---")
    out.append(b[:1400] if b else "NOT FOUND")

# ---------- 3. Footprint library search ----------
out.append("\n=== 3. FOOTPRINT SEARCH ===")
want = {
    "MountingHole:MountingHole_1.6mm": ["MountingHole", "1.6"],
    "Package_SO:SOIC-8_5.23x5.23mm_P1.27mm": ["SOIC-8", "5.23"],
    "Connector_JST:JST_SH_BM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal": ["SH_BM03B", "SRSS"],
}
libdirs = sorted(glob.glob(os.path.join(FPROOT, "*.pretty")))
out.append(f"libdirs found: {len(libdirs)}")

def find_lib(name_frag):
    for d in libdirs:
        if name_frag.lower() in os.path.basename(d).lower():
            return d
    return None

for full, frags in want.items():
    lib, _, item = full.partition(":")
    out.append(f"\n>> {full}")
    d = find_lib(lib)
    if not d:
        out.append(f"   libdir '{lib}' NOT FOUND")
        cands = [os.path.basename(x) for x in libdirs if any(f.lower() in x.lower() for f in frags[:1])]
        if cands: out.append(f"   similar dirs: {cands[:5]}")
        continue
    mods = [os.path.basename(m)[:-10] for m in glob.glob(os.path.join(d, "*.kicad_mod"))]
    out.append(f"   dir={os.path.basename(d)}  total_mods={len(mods)}")
    exact = item in mods
    out.append(f"   exact '{item}': {exact}")
    if not exact:
        hits = [m for m in mods if all(f.lower() in m.lower() for f in frags)]
        out.append(f"   matches all frags ({len(hits)}):")
        for h in hits[:12]:
            out.append(f"      {h}")
        if not hits:
            partial = [m for m in mods if any(f.lower() in m.lower() for f in frags)]
            out.append(f"   partial ({len(partial)}): " + ", ".join(partial[:12]))

# ---------- 4. Schematic files + loadability ----------
out.append("\n=== 4. SCHEMATIC FILES ===")
for f in sorted(glob.glob(os.path.join(PCB, "*.kicad_sch")) +
                glob.glob(os.path.join(PCB, "*.kicad_pro"))):
    out.append(f"  {os.path.basename(f):45s} {os.path.getsize(f):9d}")

root = os.path.join(PCB, "sgc_p1.kicad_sch")
out.append(f"\nroot exists: {os.path.exists(root)}")
# try netlist export = a real load
nl = os.path.join(PCB, "_loadtest.net")
r = subprocess.run([KCLI, "sch", "export", "netlist", "-o", nl, root],
                   capture_output=True, text=True, errors="ignore", timeout=180)
out.append(f"netlist rc={r.returncode}")
out.append("stdout: " + (r.stdout or "").strip()[:400])
out.append("stderr: " + (r.stderr or "").strip()[:800])

# ---------- 5. Nicla reference ----------
out.append("\n=== 5. NICLA REFERENCE DOCS ===")
for pat in ("reference/*", "**/*nicla*", "**/*Nicla*"):
    for g in glob.glob(os.path.join(R, pat), recursive=True)[:20]:
        if os.path.isfile(g):
            out.append(f"  {os.path.relpath(g, R)}  {os.path.getsize(g)}")

print("\n".join(out))
