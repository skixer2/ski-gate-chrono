"""NEGATIVE TEST for the fixed-point gate.

A gate that cannot fail is not a gate. This recreates the ORIGINAL defect --
child symbol instance paths missing the root uuid -- and checks whether
kicad_fixpoint.py actually flags it.

If the fixpoint gate PASSES on a deliberately broken file, then it does not
detect the real dialog and I must not trust it. This script exists to find that
out before the user does.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
FIXPOINT = os.path.join(HERE, "kicad_fixpoint.py")
PY = sys.executable


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def write(p, t):
    io.open(p, "w", encoding="utf-8", newline="").write(t)


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_negtest")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        if f.endswith((".kicad_sch", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))

    root = read(os.path.join(work, "sgc_p1.kicad_sch"))
    m = re.search(r'\(uuid "([0-9a-f-]{36})"', root)
    root_uuid = m.group(1)
    print("root uuid: %s" % root_uuid)

    # --- inject the ORIGINAL defect: strip the root uuid from child paths ---
    touched = 0
    for p in glob.glob(os.path.join(work, "sch_*.kicad_sch")):
        t = read(p)
        new = t.replace('"/%s/' % root_uuid, '"/')
        if new != t:
            n = t.count('"/%s/' % root_uuid)
            touched += n
            write(p, new)
    print("stripped root uuid from %d instance path(s)\n" % touched)

    if touched == 0:
        print("SETUP FAILED: no paths to strip - is the tree already broken?")
        return 2

    # --- ask the gate: do you catch this? ---
    r = subprocess.run([PY, FIXPOINT, work], capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    out = ((r.stdout or "") + (r.stderr or "")).strip()
    print(out)
    print("\n--- verdict ---")
    if r.returncode != 0:
        print("GATE CAUGHT THE DEFECT (exit=%d) - trustworthy." % r.returncode)
        return 0
    print("*** GATE IS BLIND: it passed a file with the original defect. ***")
    print("kicad-cli normalises the paths silently, so a parse-rewrite")
    print("comparison cannot see this class of bug. Needs a direct assertion")
    print("on instance paths instead (repair_presence.py does that).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
