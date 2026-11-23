import os, re, subprocess, glob, datetime

BASE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
RPT = os.path.join(BASE, "sgc_p1_erc.rpt")

print("=" * 72)
print("A. ERC detail — item references for the meaningful violations")
print("=" * 72)
lines = open(RPT, encoding="utf-8", errors="ignore").read().splitlines()
COSMETIC = {"endpoint_off_grid", "lib_symbol_mismatch"}
cur = None
for ln in lines:
    m = re.search(r"\[(\w+)\]:\s*(.*)", ln)
    if m:
        t = m.group(1)
        cur = None if t in COSMETIC else {"t": t, "msg": m.group(2).strip(), "items": []}
        if cur: print(f"\n[{t}] {cur['msg'][:120]}")
        continue
    if cur is not None:
        s = ln.strip()
        if s.startswith("@") or s.startswith("Item") or s.startswith("Pin") or s.startswith("Symbol") or s.startswith("Label") or s.startswith("Footprint"):
            print("     ", s[:150])
        elif s == "":
            cur = None

print()
print("=" * 72)
print("B. USB devices currently STARTED (looking for the SWD probe)")
print("=" * 72)
out = subprocess.run(["pnputil", "/enum-devices", "/class", "USB", "/connected"],
                     capture_output=True, text=True, errors="ignore").stdout
blocks = out.split("\n\n")
for b in blocks:
    if "Status:                Started" in b:
        inst = re.search(r"Instance ID:\s*(\S+)", b)
        desc = re.search(r"Device Description:\s*(.+)", b)
        if inst and desc:
            print(f"  {inst.group(1)[:55]:55s} {desc.group(1).strip()[:45]}")

print()
print("=" * 72)
print("C. Any 'Unknown' / Problem devices (failed enumeration)")
print("=" * 72)
full = subprocess.run(["pnputil", "/enum-devices", "/problem"], capture_output=True, text=True, errors="ignore").stdout
for b in full.split("\n\n"):
    if "Instance ID" in b:
        inst = re.search(r"Instance ID:\s*(\S+)", b)
        desc = re.search(r"Device Description:\s*(.+)", b)
        prob = re.search(r"Problem Code:\s*(.+)", b)
        if inst and desc:
            print(f"  {desc.group(1).strip()[:50]:50s} {inst.group(1)[:40]:40s} {prob.group(1).strip()[:20] if prob else ''}")

print()
print("=" * 72)
print("D. hexsafe contents (proves whether a flash ever happened)")
print("=" * 72)
for g in glob.glob(r"C:\sgc\hexsafe\*"):
    st = os.stat(g)
    print(f"  {os.path.basename(g):30s} {st.st_size:9d}  {datetime.datetime.fromtimestamp(st.st_mtime)}")

print()
print("=" * 72)
print("E. USB serial adapters by driver (broader than pyserial)")
print("=" * 72)
w = subprocess.run(["wmic", "path", "Win32_PnPEntity", "where",
                    "Caption like '%COM%'", "get", "Caption,Status"],
                   capture_output=True, text=True, errors="ignore").stdout
for ln in w.splitlines():
    if ln.strip():
        print("  ", ln.strip()[:130])
