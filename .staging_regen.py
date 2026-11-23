import os, subprocess, re, json

PCB = (r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements"
       r"\system_design\architecture_modules\pcb\sgc_p1")
KCLI = r"C:\Progra~1\KiCad\10.0\bin\kicad-cli.exe"
PY = r"C:\Users\v17ni\AppData\Local\Programs\Python\Python313\python.exe"

def run(args, cwd=PCB, timeout=300):
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                       errors="ignore", timeout=timeout)
    return r.returncode, (r.stdout or ""), (r.stderr or "")

print("=== 1. regenerate schematic from netlist ===")
for gen in ("gen_schematic.py",):
    gp = os.path.join(PCB, gen)
    if not os.path.exists(gp):
        print(f"  {gen} MISSING"); continue
    rc, o, e = run([PY, "-X", "utf8", gen])
    print(f"  {gen} rc={rc}")
    tail = (o + e).strip().splitlines()
    for ln in tail[-8:]:
        print("    ", ln[:130])

print("\n=== 2. validate ===")
vp = os.path.join(PCB, "validate_sgc_p1.py")
if os.path.exists(vp):
    rc, o, e = run([PY, "-X", "utf8", "validate_sgc_p1.py"])
    print(f"  validate rc={rc}")
    for ln in (o + e).strip().splitlines()[-10:]:
        print("    ", ln[:130])

print("\n=== 3. ERC again ===")
root = os.path.join(PCB, "sgc_p1.kicad_sch")
rpt = os.path.join(PCB, "sgc_p1_erc2.rpt")
rc, o, e = run([KCLI, "sch", "erc", "-o", rpt, "--severity-all", root], timeout=300)
print(f"  erc rc={rc}")
for ln in (o + e).strip().splitlines()[-4:]:
    print("    ", ln[:150])

if os.path.exists(rpt):
    txt = open(rpt, encoding="utf-8", errors="ignore").read()
    import collections
    types = collections.Counter(re.findall(r"\[(\w+)\]:", txt))
    print("\n  --- after fix ---")
    for k, v in types.most_common():
        print(f"    {v:5d}  {k}")
    print(f"    total: {sum(types.values())}")

print("\n=== 4. export fresh PDF ===")
pdf = os.path.join(PCB, "sgc_p1_review.pdf")
rc, o, e = run([KCLI, "sch", "export", "pdf", "-o", pdf, root], timeout=300)
print(f"  pdf rc={rc}  size={os.path.getsize(pdf) if os.path.exists(pdf) else 0}")
for ln in (o + e).strip().splitlines()[-3:]:
    print("    ", ln[:130])
