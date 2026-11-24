"""Confirm the LIVE files contain KiCad's three repairs.

KiCad's repair save (commit 6a3918f6) added:
  1. child symbol instance paths  /<root-uuid>/<box-uuid>
  2. (instances (project ...)) on each sheet box
  3. (embedded_fonts no) on the root

If the working tree has all three, the file JP opens is already repaired and no
dialog should appear. If any are missing, a regeneration clobbered them.
"""
import glob
import io
import os
import re

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    root = read(os.path.join(D, "sgc_p1.kicad_sch"))
    m = re.search(r'\(uuid "([0-9a-f-]{36})"', root)
    root_uuid = m.group(1) if m else "?"
    print("root uuid: %s\n" % root_uuid)

    print("=== 1. child symbol instance paths ===")
    bad_total = 0
    for p in sorted(glob.glob(os.path.join(D, "sch_*.kicad_sch"))):
        t = read(p)
        name = os.path.basename(p)
        paths = re.findall(r'\(path "([^"]*)"', t)
        good = [x for x in paths if x.startswith("/" + root_uuid + "/")]
        bad = [x for x in paths if x != "/" and
               not x.startswith("/" + root_uuid + "/")]
        bad_total += len(bad)
        print("  %-28s good=%-4d bad=%-3d" % (name, len(good), len(bad)))
        for b in bad[:3]:
            print("      BAD %s" % b)

    print("\n=== 2. sheet box instances blocks ===")
    # count sheet boxes and how many carry an instances block
    boxes = re.findall(r'\(sheet\s*\n', root)
    inst = re.findall(r'\(sheet\s*\n(?:.|\n)*?\(instances\s*\n', root)
    n_inst = root.count("(instances")
    n_proj = root.count('(project "sgc_p1"')
    print("  sheet boxes: %d" % len(boxes))
    print("  (instances occurrences in root: %d" % n_inst)
    print("  (project \"sgc_p1\") occurrences: %d" % n_proj)
    pages = re.findall(r'\(page "(\d+)"\)', root)
    print("  page numbers found: %s" % sorted(set(pages)))

    print("\n=== 3. root embedded_fonts ===")
    has = "embedded_fonts" in root
    print("  present: %s" % has)

    print()
    ok = (bad_total == 0 and n_proj >= 6 and has)
    if ok:
        print("LIVE TREE IS REPAIRED: all three KiCad fixes present.")
        return 0
    if bad_total:
        print("MISSING: %d child instance path(s) lack the root uuid" % bad_total)
    if n_proj < 6:
        print("MISSING: only %d sheet-box instances blocks (need 6)" % n_proj)
    if not has:
        print("MISSING: root has no embedded_fonts")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
