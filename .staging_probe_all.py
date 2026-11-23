import os, re, collections, subprocess, glob

BASE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
RPT = os.path.join(BASE, "sgc_p1_erc.rpt")

print("=" * 70)
print("1. ERC — non-cosmetic violations")
print("=" * 70)
txt = open(RPT, encoding="utf-8", errors="ignore").read()
# each violation is: [type]: message  possibly followed by @ (x, y): Item
pat = re.compile(r"\[(\w+)\]:\s*(.*)")
COSMETIC = {"endpoint_off_grid", "lib_symbol_mismatch"}
groups = collections.defaultdict(list)
for m in pat.finditer(txt):
    t, msg = m.group(1), m.group(2).strip()
    if t not in COSMETIC:
        groups[t].append(msg)

for t in sorted(groups, key=lambda k: -len(groups[k])):
    print(f"\n### {t}  ({len(groups[t])})")
    seen = collections.Counter(groups[t])
    for msg, c in seen.most_common(40):
        print(f"   [{c}x] {msg[:150]}")

print()
print("=" * 70)
print("2. Serial ports")
print("=" * 70)
import serial.tools.list_ports
ports = list(serial.tools.list_ports.comports())
print("count:", len(ports))
for p in ports:
    print("  ", p.device, "|", p.description)

print()
print("=" * 70)
print("3. Flash tooling — what probe does it use?")
print("=" * 70)
for f in ("zflash.bat", "autoflash.bat"):
    fp = os.path.join(r"C:\sgc\tools", f)
    if os.path.exists(fp):
        s = open(fp, encoding="utf-8", errors="ignore").read()
        print(f"\n--- {f} ---")
        for kw in ("openocd", "nrfjprog", "west flash", "jlink", "cmsis", "dap", "interface", "adapter"):
            for ln in s.splitlines():
                if kw in ln.lower():
                    print(f"  [{kw}] {ln.strip()[:130]}")
        print("  (file size", os.path.getsize(fp), "bytes)")

print()
print("=" * 70)
print("4. Any OpenOCD config in tree?")
print("=" * 70)
for pat2 in (r"C:\sgc\tools\*\*.cfg", r"C:\sgc\*.cfg"):
    for g in glob.glob(pat2):
        print("  ", g)
