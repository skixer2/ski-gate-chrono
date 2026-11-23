import subprocess, os, glob

PCB = (r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements"
       r"\system_design\architecture_modules\pcb\sgc_p1")
KCLI = r"C:\Progra~1\KiCad\10.0\bin\kicad-cli.exe"
OUT = r"C:\Users\v17ni\zio_claw\_b"

for f in ["sgc_p1.kicad_sch"] + sorted(os.path.basename(p) for p in glob.glob(os.path.join(PCB, "sch_*.kicad_sch"))):
    r = subprocess.run([KCLI, "sch", "export", "netlist", "-o", OUT + ".net",
                        os.path.join(PCB, f)],
                       capture_output=True, text=True, errors="ignore", timeout=180)
    err = ((r.stderr or "") + (r.stdout or "")).strip()
    flag = "WARN" if "annotation" in err.lower() else "ok  "
    print(f"{flag}  {f:26s} rc={r.returncode}  {err[:90]}")
