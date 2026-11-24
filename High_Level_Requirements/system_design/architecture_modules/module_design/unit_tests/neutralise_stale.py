"""Neutralise the stale schematic snapshot so it cannot be opened by mistake.

There is an OBSOLETE copy from 2026-11-21 still at version 20231120 with bare
uuids. If JP opens that one he gets the old error no matter what I fix in the
live tree. Rename it to something un-openable-by-accident rather than delete
(it is recoverable history).
"""
import os
import shutil

ROOT = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Firmware_Zephyr\sgc_verify"
OLD = os.path.join(ROOT, "OBSOLETE_sgc_p1_snapshot_20261121")
NEW = os.path.join(ROOT, "DO_NOT_OPEN_stale_snapshot_20261121_superseded")


def main():
    if not os.path.isdir(OLD):
        print("not present: %s" % OLD)
        return 0
    if os.path.isdir(NEW):
        print("target already exists: %s" % NEW)
        return 0
    try:
        os.rename(OLD, NEW)
        print("renamed:")
        print("  %s" % OLD)
        print("->%s" % NEW)
    except OSError as e:
        print("rename failed (%s), trying copy+remove" % e)
        shutil.copytree(OLD, NEW)
        shutil.rmtree(OLD, ignore_errors=True)
        print("copied then removed original")

    # drop a README in the folder so the reason is discoverable
    readme = os.path.join(NEW, "WHY_THIS_IS_HERE.txt")
    try:
        with open(readme, "w", encoding="utf-8") as f:
            f.write(
                "SUPERSEDED SNAPSHOT - DO NOT OPEN\n"
                "=================================\n\n"
                "This tree is a 2026-11-21 copy of the SGC P1 schematic, kept for\n"
                "reference only. Its files are still in the old KiCad 8 format\n"
                "(version 20231120) and contain bare uuid tokens.\n\n"
                "Opening sgc_p1.kicad_sch from here WILL show:\n"
                '  "An error was found when loading the schematic that has been\n'
                '   automatically fixed..."\n\n'
                "That message is expected here and does not mean the live design is\n"
                "broken. The live tree is:\n"
                "  High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/\n")
        print("wrote %s" % readme)
    except OSError as e:
        print("could not write readme: %s" % e)

    print()
    print("=== remaining schematic trees with version stamps ===")
    base = (r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
            r"\High_Level_Requirements\system_design\architecture_modules")
    import glob
    import re
    for p in glob.glob(os.path.join(base, "**", "sgc_p1.kicad_sch"),
                       recursive=True):
        raw = open(p, "rb").read()
        try:
            t = raw.decode("utf-8")
        except UnicodeDecodeError:
            t = raw.decode("cp1252", "replace")
        m = re.search(r'\(version (\d+)\)', t)
        bare = len(re.findall(r'\(uuid\s+[^"\s()]', t))
        print("  ver=%s bare=%d  %s" % (m.group(1) if m else "?",
                                        bare,
                                        p.replace(base, "...")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
