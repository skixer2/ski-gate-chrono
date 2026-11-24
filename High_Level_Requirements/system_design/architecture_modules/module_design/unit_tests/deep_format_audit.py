"""Deep-format audit: what does KiCad 10 object to that kicad-cli ignores?

Checks:
  1. version stamp of sgc_p1.kicad_sym  (a KiCad 8 library is a real defect)
  2. symbol instance PATHS in every child sheet vs the real root/box uuids
  3. project name consistency in (project "...") blocks
  4. sheet_instances in the root
  5. any token kicad-cli might tolerate but the GUI repairs
"""
import glob
import io
import os
import re
import sys

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"

VER = re.compile(r'\(version (\d+)\)')
GENV = re.compile(r'\(generator_version "([^"]*)"\)')
PROJ = re.compile(r'\(project "([^"]*)"')
INST_PATH = re.compile(r'\(path "([^"]*)"')
SHEET_INST = re.compile(r'\(sheet_instances(.*?)\n\t\)', re.S)


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    print("=== 1. SYMBOL LIBRARY version ===")
    lib = os.path.join(D, "sgc_p1.kicad_sym")
    if os.path.isfile(lib):
        t = read(lib)
        v = VER.search(t)
        g = GENV.search(t)
        print("  sgc_p1.kicad_sym  version=%s  generator_version=%s"
              % (v.group(1) if v else "?", g.group(1) if g else "ABSENT"))
        if v and v.group(1) != "20260306":
            print("  ^^ KiCad 8/9 FORMAT - KiCad 10 must migrate this on open")
    else:
        print("  MISSING")

    print()
    print("=== 2. ROOT sheet_instances + root uuid ===")
    root = read(os.path.join(D, "sgc_p1.kicad_sch"))
    root_uuid = re.search(r'\(uuid "?([0-9a-f-]{36})"?\)', root)
    root_uuid = root_uuid.group(1) if root_uuid else "?"
    print("  root uuid: %s" % root_uuid)
    m = SHEET_INST.search(root)
    if m:
        body = m.group(1)
        paths = re.findall(r'\(path "([^"]*)"\s*\n?\s*\(page "([^"]*)"', body)
        if not paths:
            paths = re.findall(r'\(path "([^"]*)"', body)
        print("  sheet_instances: %d entries" % len(paths))
        for p in paths[:10]:
            print("    %s" % (p,))
    else:
        print("  NO sheet_instances block")

    print()
    print("=== 3. per-sheet instance paths ===")
    for p in sorted(glob.glob(os.path.join(D, "sch_*.kicad_sch"))):
        t = read(p)
        name = os.path.basename(p)
        projs = sorted(set(PROJ.findall(t)))
        paths = sorted(set(INST_PATH.findall(t)))
        v = VER.search(t)
        print("  %s" % name)
        print("    version=%s  projects=%s" % (v.group(1) if v else "?", projs))
        print("    distinct instance paths: %d" % len(paths))
        for x in paths[:4]:
            print("      %r" % x)
        # a child path should be "/<box-uuid>"; a bare "/" is the root's own
        bad = [x for x in paths if x.count("/") < 1 or
               (x != "/" and not re.match(r'^/[0-9a-f-]{36}$', x))]
        if bad:
            print("    SUSPECT paths: %s" % bad[:5])

    return 0


if __name__ == "__main__":
    sys.exit(main())
