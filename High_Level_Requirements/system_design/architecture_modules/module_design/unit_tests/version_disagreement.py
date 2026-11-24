"""Where do the versions differ: JP-PC live tree vs the gateway checkout?

DS reported the .kicad_sym at 20231120 with old-style (effects ... hide).
My measurement on JP-PC said 20251024. Both can be true if DS read the
gateway repo copy, which I upgraded only on JP-PC and may never have committed.

This prints, for each KiCad file: version stamp, old-style-hide count,
new-style (hide yes) count, and mtime - so the disagreement is resolvable.
"""
import glob
import io
import os
import re
import sys

PATHS = {
    "JP-PC live": r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1",
}

VER = re.compile(r'\(version (\d+)\)')
OLD_HIDE = re.compile(r'\(effects[^()]*\)\s*hide\s*\)')
NEW_HIDE = re.compile(r'\(hide yes\)')
SHOW_NAME = re.compile(r'\(show_name ')
IN_POS = re.compile(r'\(in_pos_files ')
EXCL_SIM = re.compile(r'\(exclude_from_sim ')


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def report(label, base):
    print("=" * 70)
    print(label)
    print("  %s" % base)
    if not os.path.isdir(base):
        print("  NOT PRESENT")
        return
    files = []
    for pat in ("*.kicad_sym", "sgc_p1.kicad_sch", "sch_mcu.kicad_sch"):
        files += glob.glob(os.path.join(base, pat))
    for p in files:
        t = read(p)
        v = VER.search(t)
        print("  %-22s ver=%-9s mtime=%s" % (
            os.path.basename(p),
            v.group(1) if v else "?",
            __import__("datetime").datetime.fromtimestamp(
                os.path.getmtime(p)).strftime("%m-%d %H:%M")))
        print("      old-hide=%-4d new-hide=%-4d show_name=%-4d in_pos=%-3d "
              "excl_sim=%d" % (
                  len(OLD_HIDE.findall(t)), len(NEW_HIDE.findall(t)),
                  len(SHOW_NAME.findall(t)), len(IN_POS.findall(t)),
                  len(EXCL_SIM.findall(t))))


def main():
    for label, base in PATHS.items():
        report(label, base)

    print()
    print("=" * 70)
    print("NOTE: the gateway checkout is a DIFFERENT working copy.")
    print("If DS read that one, its version numbers describe a stale tree.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
