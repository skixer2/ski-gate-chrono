"""Audit EVERY KiCad file in the board dir for old-format stamps.

I upgraded the .kicad_sch files on 2026-11-23 and declared the job done. The
.kicad_sym symbol library was never touched and is still KiCad 8 format, which
is what KiCad migrates on load -> "An error was found when loading ... and has
been automatically fixed".

This lists every KiCad file with its version stamp so nothing is missed again.
"""
import glob
import io
import os
import re
import sys

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
CURRENT = "20260306"

VER = re.compile(r'\(version (\d+)\)')
GENV = re.compile(r'\(generator_version "([^"]*)"\)')

EXTS = (".kicad_sch", ".kicad_sym", ".kicad_pcb", ".kicad_pro",
        ".kicad_mod", ".kicad_wks", ".kicad_dru")


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    old = []
    files = []
    for ext in EXTS:
        files += glob.glob(os.path.join(D, "**", "*" + ext), recursive=True)
    files = sorted(set(files))

    print("%-52s %-10s %s" % ("file", "version", "state"))
    print("-" * 78)
    for p in files:
        rel = os.path.relpath(p, D)
        t = read(p)
        if p.endswith(".kicad_pro"):
            print("%-52s %-10s %s" % (rel[:52], "json", "n/a"))
            continue
        m = VER.search(t)
        v = m.group(1) if m else "?"
        if v == CURRENT:
            state = "OK"
        elif v == "?":
            state = "no stamp"
        else:
            state = "OLD -> migrated on load"
            old.append((rel, v))
        print("%-52s %-10s %s" % (rel[:52], v, state))

    print()
    if old:
        print("OLD-FORMAT FILES (%d):" % len(old))
        for rel, v in old:
            print("  %-50s %s" % (rel, v))
        return 1
    print("ALL KiCad FILES AT %s" % CURRENT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
