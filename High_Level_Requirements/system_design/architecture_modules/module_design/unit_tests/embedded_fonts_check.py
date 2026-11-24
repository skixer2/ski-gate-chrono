"""Where does embedded_fonts live, and does KiCad 10 require it?

KiCad 9+ writes (embedded_fonts no) into every schematic it saves. If our ROOT
sheet lacks it while the children have it, KiCad must add it on load - exactly
the "automatically fixed" situation.

Checks the position of the token in each file and in KiCad's own rewrite.
"""
import glob
import io
import os
import re

LIVE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
KICAD_ROOT = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124\sgc_force_probe\sgc_p1.kicad_sch"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def report(path, label):
    t = read(path)
    lines = t.splitlines()
    idx = [i + 1 for i, ln in enumerate(lines) if "embedded_fonts" in ln]
    print("%-34s lines=%-5d embedded_fonts_at=%s" %
          (label, len(lines), idx if idx else "ABSENT"))
    if idx:
        for i in idx:
            print("        %d: %r" % (i, lines[i - 1].strip()))
    return bool(idx)


def main():
    print("=== LIVE sheets ===")
    present = {}
    for p in sorted(glob.glob(os.path.join(LIVE, "*.kicad_sch"))):
        present[os.path.basename(p)] = report(p, os.path.basename(p))

    print()
    print("=== KiCad 10's own rewrite of the root ===")
    if os.path.isfile(KICAD_ROOT):
        report(KICAD_ROOT, "kicad_rewrite/sgc_p1.kicad_sch")
    else:
        print("  (missing)")

    print()
    missing = [n for n, ok in present.items() if not ok]
    if missing:
        print("SHEETS WITHOUT embedded_fonts (%d):" % len(missing))
        for n in missing:
            print("  " + n)
        print()
        print("KiCad 9/10 writes this token into every schematic it saves.")
        print("A file lacking it must be completed on load = repair.")
    else:
        print("every sheet carries embedded_fonts")


if __name__ == "__main__":
    main()
