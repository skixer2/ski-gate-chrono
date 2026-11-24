"""Compare the TAIL of our sheets with KiCad 10's own output.

KiCad 9+ appends fields at the END of a .kicad_sch, notably
  (embedded_fonts no)
A file that lacks the fields KiCad 10 expects at the end must be completed on
load - which is a repair. My earlier header comparison only looked at line 1-20
and would never have seen this.
"""
import glob
import io
import os

LIVE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
KICAD = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124\sgc_force_probe"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def tail(p, n=12):
    return read(p).splitlines()[-n:]


def main():
    print("=== LIVE root sheet, last 12 lines ===")
    for ln in tail(os.path.join(LIVE, "sgc_p1.kicad_sch")):
        print("  %r" % ln)

    k = os.path.join(KICAD, "sgc_p1.kicad_sch")
    if os.path.isfile(k):
        print()
        print("=== KiCad 10's own rewrite, last 12 lines ===")
        for ln in tail(k):
            print("  %r" % ln)

    print()
    print("=== which sheets carry (embedded_fonts ...) ===")
    for p in sorted(glob.glob(os.path.join(LIVE, "*.kicad_sch"))):
        t = read(p)
        print("  %-28s embedded_fonts=%s" % (os.path.basename(p),
                                             "embedded_fonts" in t))

    print()
    print("=== which sheets carry a trailing (sheet_instances ...) ===")
    for p in sorted(glob.glob(os.path.join(LIVE, "*.kicad_sch"))):
        t = read(p)
        print("  %-28s sheet_instances=%s" % (os.path.basename(p),
                                              "sheet_instances" in t))


if __name__ == "__main__":
    main()
