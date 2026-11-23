import os, re, collections

BASE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
OUT  = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\erc_review"
os.makedirs(OUT, exist_ok=True)

COSMETIC = {"endpoint_off_grid", "lib_symbol_mismatch"}

txt = open(os.path.join(BASE, "sgc_p1_erc.rpt"), encoding="utf-8", errors="ignore").read()

# ERC report format: blocks separated by blank lines, each with [type]: message
lines = txt.splitlines()
blocks, cur = [], []
for ln in lines:
    if ln.strip() == "":
        if cur:
            blocks.append(cur); cur = []
    else:
        cur.append(ln)
if cur:
    blocks.append(cur)

real, cosmetic_n = [], 0
for b in blocks:
    joined = "\n".join(b)
    m = re.search(r"\[(\w+)\]:", joined)
    if not m:
        continue
    if m.group(1) in COSMETIC:
        cosmetic_n += 1
    else:
        real.append(joined)

hdr = (
    "SGC P1 — ERC, non-cosmetic violations only\n"
    "==========================================\n"
    f"cosmetic (ignored here): {cosmetic_n}  "
    "(endpoint_off_grid + lib_symbol_mismatch — grid/annotation noise from the generator)\n"
    f"electrically meaningful: {len(real)}\n\n"
)
open(os.path.join(OUT, "erc_real_issues.txt"), "w", encoding="utf-8").write(hdr + "\n\n---\n\n".join(real))

# also: which components lack a footprint (the known fp=E bug)
for rpt in ("sgc_p1_erc.rpt",):
    pass
print("wrote erc_real_issues.txt:", len(real), "blocks; cosmetic:", cosmetic_n)
