"""Byte-level header comparison: what WE write vs what KICAD 10 writes.

KiCad's own writer emits a specific header shape. ours comes from kiutils and
may differ in ways that force a repair. Diff the first lines exactly.
"""
import difflib
import glob
import io
import os

LIVE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1\sgc_p1.kicad_sch"
# a file KiCad 10 itself rewrote (from the forced-reparse probe)
KICAD_WROTE = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124\sgc_force_probe\sgc_p1.kicad_sch"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    print("=== OUR LIVE ROOT SHEET: first 14 lines ===")
    a = read(LIVE).splitlines()
    for i, ln in enumerate(a[:14], 1):
        print("%3d| %r" % (i, ln))

    if os.path.isfile(KICAD_WROTE):
        print()
        print("=== KICAD 10's OWN REWRITE of the same file: first 14 lines ===")
        b = read(KICAD_WROTE).splitlines()
        for i, ln in enumerate(b[:14], 1):
            print("%3d| %r" % (i, ln))

        print()
        print("=== header diff (first 20 lines) ===")
        for ln in difflib.unified_diff(a[:20], b[:20], "ours", "kicad",
                                       lineterm="", n=2):
            print(ln[:150])

    print()
    print("=== specific token checks on the LIVE file ===")
    t = read(LIVE)
    for tok, want in (
        ("(generator sgc_gen)", "unquoted generator name"),
        ('(generator "sgc_gen")', "quoted generator name"),
        ("generator_version", "generator_version present"),
        ('(paper "A3")', "paper"),
        ("(lib_symbols)", "empty or populated lib_symbols"),
        ("(sheet_instances", "sheet_instances"),
        ("(embedded_fonts", "embedded_fonts (KiCad 9+ field)"),
    ):
        print("  %-34s %s   %s" % (tok, "YES" if tok in t else "no ", want))

    print()
    print("=== does EVERY sheet carry generator_version? ===")
    for p in sorted(glob.glob(os.path.join(os.path.dirname(LIVE),
                                           "*.kicad_sch"))):
        s = read(p)
        has = "generator_version" in s
        print("  %-28s generator_version=%s" % (os.path.basename(p), has))


if __name__ == "__main__":
    main()
