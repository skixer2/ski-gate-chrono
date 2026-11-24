"""SUPERSEDED - DO NOT WIRE INTO SMOKE. THIS TEST IS PROVEN BLIND.

fixpoint_negative_test.py stripped the root uuid from all 101 child instance
paths (the exact defect that caused the "automatically fixed" dialog) and this
script still printed ALL SHEETS ARE FIXED POINTS. kicad-cli normalises instance
paths silently during parse, so a parse-rewrite comparison cannot observe this
defect class. It answers a real question; it is simply not THE question.

Use sch_repair_gate.py instead: direct assertions on the three things KiCad adds
(child instance paths, sheet-box instances blocks, root embedded_fonts), proven
able to fail on all three by sch_repair_gate_negative.py.

Kept only as the historical record of why the fixed-point idea fails.

--- original docstring ---

Fixed-point test: is the schematic stable under KiCad 10?

The only question that matters. Take a sheet, force KiCad to parse and rewrite
it twice, and compare pass 1 with pass 2.

  pass1 == pass2  -> the file is a FIXED POINT. KiCad has nothing left to
                     repair, so no "automatically fixed" dialog.
  pass1 != pass2  -> still settling; KiCad keeps changing something.

Line diffs are useless here because KiCad reformats wholesale. This compares
the two KiCad-generated outputs to each other, which removes formatting as a
variable entirely.

Usage: kicad_fixpoint.py [dir]
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
DEFAULT = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def norm(t):
    """Whitespace-insensitive: KiCad's formatting is not the question."""
    return re.sub(r'\s+', ' ', t).strip()


def force_upgrade(path):
    """Make the version stamp old so KiCad MUST parse and rewrite."""
    t = re.sub(r'\(version \d{8}\)', '(version 20231120)', read(path), count=1)
    io.open(path, "w", encoding="utf-8", newline="").write(t)
    r = subprocess.run([CLI, "sch", "upgrade", path], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    return ((r.stdout or "") + (r.stderr or "")).strip()


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    names = sorted(os.path.basename(p)
                   for p in glob.glob(os.path.join(src, "*.kicad_sch")))
    if not names:
        print("no sheets in %s" % src)
        return 2

    work = os.path.join(tempfile.gettempdir(), "sgc_fixpoint")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    # need the whole project context for hierarchy resolution
    for f in os.listdir(src):
        if f.endswith((".kicad_sch", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(src, f), os.path.join(work, f))

    print("source: %s" % src)
    print("sheets: %d\n" % len(names))

    unstable = 0
    for n in names:
        p = os.path.join(work, n)
        force_upgrade(p)          # pass 1
        first = norm(read(p))
        force_upgrade(p)          # pass 2
        second = norm(read(p))
        if first == second:
            print("%-28s STABLE (fixed point)" % n)
        else:
            unstable += 1
            print("%-28s UNSTABLE - KiCad changes it again" % n)
            # show the first differing chunk
            import difflib
            sm = difflib.SequenceMatcher(None, first, second)
            for tag, i1, i2, j1, j2 in sm.get_opcodes():
                if tag != "equal":
                    print("    pass1: %r" % first[i1:min(i2, i1 + 100)])
                    print("    pass2: %r" % second[j1:min(j2, j1 + 100)])
                    break

    print()
    if unstable:
        print("%d sheet(s) NOT at a fixed point - KiCad still repairs them."
              % unstable)
        return 1
    print("ALL SHEETS ARE FIXED POINTS.")
    print("KiCad has nothing left to repair -> no 'automatically fixed' dialog.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
