"""Gate: the symbol library must be in CURRENT KiCad format, not KiCad 8.

WHY THIS EXISTS
---------------
On 2026-11-24 the dialog "An error was found when loading the schematic that has
been automatically fixed" turned out to live in sgc_p1.kicad_sym, not the sheets.
sym-lib-table loads that library on project open, and:

  * kiutils (KiCad 8 era) stamps it `(version 20231120)`
  * it also writes the old property shape `(effects (font ...) hide)`
  * KiCad 10 migrates both IN MEMORY on every load, marks the design modified,
    and reports the repair dialog

The library was upgraded and committed, then a regeneration silently put it back.
Every sheet-level check stayed green throughout, because the defect was never in
a sheet. This gate closes that hole.

Detects: version stamp, old-style hide tokens, missing (hide yes) / (show_name).
"""
import glob
import io
import os
import re
import sys

KNOWN_OLD = {"20231120", "20211014", "2022", "2023"}

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))

VER = re.compile(r'\(version (\d+)\)')
OLD_HIDE = re.compile(r'\(effects[^\n]*\)\s*hide\s*\)')
NEW_HIDE = re.compile(r'\(hide yes\)')
SHOW_NAME = re.compile(r'\(show_name ')


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    libs = sorted(glob.glob(os.path.join(BASE, "*.kicad_sym")))
    if not libs:
        print("no .kicad_sym in %s" % BASE)
        print("SKIP: library not present on this host")
        return 0

    problems = []
    for p in libs:
        name = os.path.basename(p)
        t = read(p)
        m = VER.search(t)
        v = m.group(1) if m else "?"
        old_hide = len(OLD_HIDE.findall(t))
        new_hide = len(NEW_HIDE.findall(t))
        show_name = len(SHOW_NAME.findall(t))

        print("%-24s version=%-9s old_hide=%-4d hide_yes=%-4d show_name=%d"
              % (name, v, old_hide, new_hide, show_name))

        if v in KNOWN_OLD:
            problems.append("%s is OLD format (%s)" % (name, v))
        if old_hide:
            problems.append("%s has %d old-style (effects ... hide) tokens"
                            % (name, old_hide))
        if new_hide == 0 and old_hide == 0:
            problems.append("%s has no (hide yes) tokens at all" % name)

    print()
    if problems:
        print("LIBRARY FORMAT DEFECTS (%d):" % len(problems))
        for p in problems:
            print("  ! " + p)
        print()
        print("KiCad 10 migrates this in memory on every load = the repair")
        print("dialog. Regenerate with gen_symbols.py (it now self-upgrades).")
        return 1
    print("LIBRARY FORMAT OK - current KiCad format, no migration needed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
