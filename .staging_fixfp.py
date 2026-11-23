import os, re, json, shutil

PCB = (r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements"
       r"\system_design\architecture_modules\pcb\sgc_p1")
PRETTY = os.path.join(PCB, "sgc_p1.pretty")
NET = os.path.join(PCB, "sgc_netlist.py")

log = []

# ============ 1. Generate MountingHole_1.6mm into the project-local lib ============
# KiCad's MountingHole library has no 1.6 mm variant (goes 2.2, 2.5, 2.7, 3.0 ...).
# MH1/MH2 are jig registration holes -> plain NPTH, no annular ring, no BOM entry.
FP = '''(footprint "MountingHole_1.6mm"
\t(version 20260206)
\t(generator "sgc_p1 generator")
\t(layer "F.Cu")
\t(descr "Mounting Hole 1.6mm, no annular ring, NPTH. Generated for SGC P1 (MH1/MH2 pogo-jig registration). Not in KiCad's stock MountingHole library.")
\t(tags "mountinghole NPTH 1.6mm jig registration")
\t(attr exclude_from_pos_files exclude_from_bom)
\t(fp_circle
\t\t(center 0 0)
\t\t(end 1.6 0)
\t\t(stroke (width 0.15) (type solid))
\t\t(fill no)
\t\t(layer "Cmts.User")
\t)
\t(fp_circle
\t\t(center 0 0)
\t\t(end 1.85 0)
\t\t(stroke (width 0.05) (type solid))
\t\t(fill no)
\t\t(layer "F.CrtYd")
\t)
\t(fp_text user "${REFERENCE}"
\t\t(at 0 0 0)
\t\t(layer "F.Fab")
\t\t(effects (font (size 1 1) (thickness 0.15)))
\t)
\t(pad "" np_thru_hole circle
\t\t(at 0 0)
\t\t(size 1.6 1.6)
\t\t(drill 1.6)
\t\t(layers "*.Cu" "*.Mask")
\t)
\t(embedded_fonts no)
)
'''
os.makedirs(PRETTY, exist_ok=True)
fp_path = os.path.join(PRETTY, "MountingHole_1.6mm.kicad_mod")
with open(fp_path, "w", encoding="utf-8", newline="\n") as f:
    f.write(FP)
log.append(f"CREATED {os.path.basename(fp_path)} ({os.path.getsize(fp_path)} B)")

# ============ 2. Fix the two wrong footprint names ============
src = open(NET, encoding="utf-8", errors="ignore").read()
orig = src

FIXES = [
    # MH1/MH2 -> project-local lib
    ('fp="MountingHole:MountingHole_1.6mm"',
     'fp="sgc_p1:MountingHole_1.6mm"',
     "MountingHole 1.6mm -> sgc_p1 (generated)"),

    # 5.23 -> 5.3 : real KiCad name is 5.3x握5.3
    ('fp="Package_SO:SOIC-8_5.23x5.23mm_P1.27mm"',
     'fp="Package_SO:SOIC-8_5.3x5.3mm_P1.27mm"',
     "SOIC-8 5.23 -> 5.3 (KiCad spelling)"),

    # BM03B = Vertical ; SM03B = Horizontal . J4 is the horizontal one.
    ('fp="Connector_JST:JST_SH_BM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal"',
     'fp="Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal"',
     "JST SH BM03B->SM03B (Horizontal variant)"),
]

for old, new, note in FIXES:
    n = src.count(old)
    if n == 0:
        log.append(f"SKIP ({note}): anchor not found")
        continue
    src = src.replace(old, new)
    log.append(f"OK   x{n}  {note}")

if src != orig:
    open(NET, "w", encoding="utf-8", newline="\n").write(src)
    log.append("wrote sgc_netlist.py")
else:
    log.append("sgc_netlist.py UNCHANGED")

# ============ 3. verify no stale names remain ============
for bad in ("MountingHole:MountingHole_1.6mm",
            "SOIC-8_5.23x5.23mm",
            "JST_SH_BM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal"):
    log.append(f"remaining '{bad}': {src.count(bad)}")

print("\n".join(log))
