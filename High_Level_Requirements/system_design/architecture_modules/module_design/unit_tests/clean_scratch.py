"""Remove MY scratch copies of the schematic so they cannot be opened by mistake.

Only touches dirs I created during debugging on 2026-11-23/24. Refuses to
delete anything that is not clearly a scratch copy. Moves to a trash folder
rather than deleting, so it stays recoverable.
"""
import glob
import os
import shutil

TRASH = r"C:\sgc\sch_trash_20261124"

SCRATCH = [
    r"C:\sgc\gentest",
    r"C:\sgc\sch_old",
    r"C:\sgc\sch_roundtrip",
    r"C:\sgc\sf2",
    os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "sgc_force_probe"),
    r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements"
    r"\system_design\architecture_modules\module_design\implementation"
    r"\Firmware_Zephyr\gentest",
    r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements"
    r"\system_design\architecture_modules\module_design\implementation"
    r"\Firmware_Zephyr\sch_old",
    r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements"
    r"\system_design\architecture_modules\module_design\implementation"
    r"\Firmware_Zephyr\sch_roundtrip",
]

LIVE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"


def main():
    os.makedirs(TRASH, exist_ok=True)
    moved = kept = 0
    for d in SCRATCH:
        if not os.path.isdir(d):
            continue
        # never touch the live design tree
        if os.path.abspath(d).lower() == os.path.abspath(LIVE).lower():
            print("REFUSE live tree: %s" % d)
            kept += 1
            continue
        # only move dirs that look like scratch: contain .kicad_sch but are not the live tree
        schs = glob.glob(os.path.join(d, "*.kicad_sch"))
        if not schs:
            print("skip (no sheets): %s" % d)
            continue
        name = os.path.basename(d.rstrip("\\")) or "root"
        dest = os.path.join(TRASH, name)
        i = 1
        while os.path.exists(dest):
            dest = os.path.join(TRASH, "%s_%d" % (name, i))
            i += 1
        try:
            shutil.move(d, dest)
            print("MOVED  %s" % d)
            print("    -> %s  (%d sheets)" % (dest, len(schs)))
            moved += 1
        except Exception as e:
            print("FAILED %s : %s" % (d, e))
            kept += 1

    print()
    print("moved %d scratch tree(s), left %d" % (moved, kept))
    print("trash: %s" % TRASH)

    print()
    print("=== remaining schematic trees under the repo ===")
    for p in glob.glob(r"C:\Users\v17ni\Documents\**\sgc_p1.kicad_sch",
                       recursive=True):
        print("  " + p)


if __name__ == "__main__":
    main()
