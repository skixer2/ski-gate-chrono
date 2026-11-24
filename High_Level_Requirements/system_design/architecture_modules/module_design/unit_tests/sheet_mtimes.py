"""File mtimes for the sheets - whose edit is the uncommitted sch_power change?"""
import os
import time

PCB = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
rows = []
for f in sorted(os.listdir(PCB)):
    if f.endswith((".kicad_sch", ".kicad_pro", ".py")):
        p = os.path.join(PCB, f)
        st = os.stat(p)
        rows.append((st.st_mtime, f, st.st_size))
rows.sort(reverse=True)
print("%-32s %8s  %s" % ("file", "bytes", "last write (local)"))
for mt, f, sz in rows:
    print("%-32s %8d  %s" % (f, sz, time.strftime("%Y-%m-%d %H:%M:%S",
                                                  time.localtime(mt))))
