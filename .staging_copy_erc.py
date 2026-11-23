import os, shutil
BASE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
OUT  = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\erc_review"
os.makedirs(OUT, exist_ok=True)
for n in ("sgc_p1_review.pdf", "sgc_p1_erc.rpt"):
    s = os.path.join(BASE, n)
    if os.path.exists(s):
        shutil.copy2(s, os.path.join(OUT, n))
        print("copied", n)
    else:
        print("MISSING", n)
