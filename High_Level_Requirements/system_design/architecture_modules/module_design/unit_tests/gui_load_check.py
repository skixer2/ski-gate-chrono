"""What the GUI does on open that kicad-cli does not.

kicad-cli parses the sheet and is happy. The GUI additionally:
  1. searches UPWARD for a .kicad_pro and adopts the first one it finds
  2. resolves ${KIPRJMOD} from that project's directory
  3. loads sym-lib-table / fp-lib-table from the PROJECT dir
  4. refuses if a lock file exists

If a stray .kicad_pro sits in a parent directory, opening the schematic
standalone attaches it to the WRONG project, ${KIPRJMOD} points elsewhere,
the symbol library does not resolve, and KiCad raises a load error even
though every sheet file is valid.
"""
import glob
import io
import json
import os
import re

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"

print("=== 1. .kicad_pro files on the path from drive root down to the board ===")
parts = D.split(os.sep)
for i in range(2, len(parts) + 1):
    d = os.sep.join(parts[:i])
    hits = glob.glob(os.path.join(d, "*.kicad_pro"))
    if hits:
        for h in hits:
            print("  %s" % h)
    else:
        print("  (none)  %s" % d)
print()

print("=== 2. lock files ===")
locks = glob.glob(os.path.join(D, "*.lck")) + glob.glob(os.path.join(D, "~*"))
print("  %s" % (locks if locks else "none"))
print()

print("=== 3. top_level_sheets / annotation in the real project ===")
j = json.loads(io.open(os.path.join(D, "sgc_p1.kicad_pro"),
                       encoding="utf-8-sig").read())
s = j.get("schematic", {})
print("  top_level_sheets: %s" % json.dumps(s.get("top_level_sheets")))
print("  annotate_start_num: %s" % s.get("annotate_start_num"))
print("  legacy_lib_dir: %r" % s.get("legacy_lib_dir"))
print()

print("=== 4. lib tables present in the board dir ===")
for f in ("sym-lib-table", "fp-lib-table"):
    p = os.path.join(D, f)
    print("  %-14s exists=%s" % (f, os.path.isfile(p)))

print()
print("=== 5. symbol library the project needs ===")
need = "sgc_p1.kicad_sym"
p = os.path.join(D, need)
print("  %s exists=%s size=%s" % (need, os.path.isfile(p),
                                  os.path.getsize(p) if os.path.isfile(p) else "-"))

print()
print("=== 6. .history backup copies (KiCad may offer/restore these) ===")
h = os.path.join(D, ".history")
if os.path.isdir(h):
    files = sorted(glob.glob(os.path.join(h, "**", "*"), recursive=True))
    sch = [f for f in files if f.endswith(".kicad_sch")]
    print("  %d entries, %d sheet backups" % (len(files), len(sch)))
    for f in sch[:10]:
        print("    %s  %s" % (os.path.getmtime(f), f.replace(D, ".")))
else:
    print("  no .history")
