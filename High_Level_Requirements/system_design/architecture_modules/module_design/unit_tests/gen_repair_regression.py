"""GENERATOR REGRESSION TEST: does gen_schematic.py still emit a repaired tree?

The on-disk schematic is repaired right now, but that file was saved by KiCad.
If gen_schematic.py is run again (which happens whenever the design changes),
does it reproduce all three repairs -- or quietly reintroduce the dialog?

This is the difference between "we fixed it once" and "it stays fixed".

Run: python gen_repair_regression.py
"""
import io
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
GATE = os.path.join(HERE, "sch_repair_gate.py")
GEN = "gen_schematic.py"


def main():
    if not os.path.isfile(os.path.join(SRC, GEN)):
        print("SKIP: %s not found in %s" % (GEN, SRC))
        return 0

    work = os.path.join(tempfile.gettempdir(), "sgc_genreg")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    # copy only the inputs the generator needs; NOT the .kicad_sch outputs,
    # so anything found afterwards was produced by this run.
    copied = 0
    for f in os.listdir(SRC):
        if f.endswith((".py", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")) or f.endswith(".txt"):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))
            copied += 1
    if os.path.isdir(os.path.join(SRC, "sgc_p1.pretty")):
        shutil.copytree(os.path.join(SRC, "sgc_p1.pretty"),
                        os.path.join(work, "sgc_p1.pretty"), dirs_exist_ok=True)

    print("staged %d input file(s) into %s" % (copied, work))
    r = subprocess.run([sys.executable, GEN], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    tail = ((r.stdout or "") + (r.stderr or "")).strip().splitlines()
    print("generator exit=%d" % r.returncode)
    for ln in tail[-4:]:
        print("   " + ln)
    if r.returncode != 0:
        print("\nGENERATOR FAILED - cannot assess repairs.")
        return 1

    made = sorted(f for f in os.listdir(work) if f.endswith(".kicad_sch"))
    print("generated %d sheet(s): %s\n" % (len(made), ", ".join(made)))
    if len(made) < 7:
        print("!! expected 7 sheets, got %d" % len(made))
        return 1

    g = subprocess.run([sys.executable, GATE, work], capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    print(((g.stdout or "") + (g.stderr or "")).strip())
    print()
    if g.returncode == 0:
        print("GENERATOR REGRESSION PASS: fresh output carries all three repairs.")
        print("Regenerating the schematic will not bring the dialog back.")
        return 0
    print("GENERATOR REGRESSION FAIL: fresh output is NOT repaired.")
    print("Running gen_schematic.py would reintroduce the 'automatically fixed'")
    print("dialog. Fix the generator before regenerating anything.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
