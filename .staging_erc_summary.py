import os, shutil, collections, re

BASE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
OUT = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\erc_review"

os.makedirs(OUT, exist_ok=True)

# 1. copy the PDF + raw ERC report into the repo so they can be committed
for name in ("sgc_p1_review.pdf", "sgc_p1_erc.rpt"):
    src = os.path.join(BASE, name)
    if os.path.exists(src):
        shutil.copy2(src, os.path.join(OUT, name))
        print("copied", name, os.path.getsize(src), "bytes")
    else:
        print("MISSING", name)

# 2. summarise the ERC report by violation type
rpt = os.path.join(OUT, "sgc_p1_erc.rpt")
if os.path.exists(rpt):
    txt = open(rpt, encoding="utf-8", errors="ignore").read()
    types = collections.Counter(re.findall(r"\[(\w+)\]:", txt))
    print("\n--- violation types ---")
    for k, v in types.most_common():
        print(f"{v:5d}  {k}")
    print("total:", sum(types.values()))
    open(os.path.join(OUT, "erc_summary.txt"), "w", encoding="utf-8").write(
        "\n".join(f"{v:5d}  {k}" for k, v in types.most_common()) + f"\ntotal: {sum(types.values())}\n")
