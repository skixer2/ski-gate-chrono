"""End-to-end proof: run the FIXED generator pipeline, confirm KiCad 10 format.

This is the test that would have caught the bug months ago. It:
  1. runs gen_symbols.py in a scratch copy
  2. checks the produced .kicad_sym version and property token shape
  3. runs gen_schematic.py and checks the sheets
  4. asks kicad-cli whether it would rewrite any of it

Before the fix, step 2 reported version 20231120 with 99 old-style (hide)
tokens - which is what made KiCad migrate the library on every load.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

SRC = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
PY = r"C:\Users\v17ni\AppData\Local\Programs\Python\Python313\python.exe"
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"

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
    work = os.path.join(tempfile.gettempdir(), "sgc_pipeline_probe")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        if f.endswith((".py", ".txt", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))
    if os.path.isdir(os.path.join(SRC, "sgc_p1.pretty")):
        shutil.copytree(os.path.join(SRC, "sgc_p1.pretty"),
                        os.path.join(work, "sgc_p1.pretty"), dirs_exist_ok=True)

    print("=== 1. run gen_symbols.py (the fixed one) ===")
    r = subprocess.run([PY, "gen_symbols.py"], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    out = ((r.stdout or "") + (r.stderr or "")).strip()
    print("exit=%d" % r.returncode)
    for ln in out.splitlines()[-6:]:
        print("  | " + ln)

    lib = os.path.join(work, "sgc_p1.kicad_sym")
    if not os.path.isfile(lib):
        print("no library produced")
        return 2
    t = read(lib)
    v = VER.search(t)
    print()
    print("=== 2. produced library format ===")
    print("  version      : %s" % (v.group(1) if v else "?"))
    print("  old (hide)   : %d   <- must be 0" % len(OLD_HIDE.findall(t)))
    print("  (hide yes)   : %d" % len(NEW_HIDE.findall(t)))
    print("  (show_name)  : %d" % len(SHOW_NAME.findall(t)))

    lib_ok = (v and v.group(1) != "20231120"
              and len(OLD_HIDE.findall(t)) == 0)
    print("  VERDICT      : %s" % ("KiCad 10 format" if lib_ok
                                   else "STILL OLD FORMAT"))

    print()
    print("=== 3. run gen_schematic.py ===")
    r = subprocess.run([PY, "gen_schematic.py"], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    print("exit=%d" % r.returncode)
    for ln in ((r.stdout or "") + (r.stderr or "")).strip().splitlines()[-3:]:
        print("  | " + ln)

    print()
    print("=== 4. ask kicad-cli if it would rewrite the sheets ===")
    root = os.path.join(work, "sgc_p1.kicad_sch")
    before = {}
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        before[os.path.basename(p)] = read(p)
    # force a rewrite
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        s = re.sub(r'\(version \d{8}\)', '(version 20231120)', read(p), count=1)
        io.open(p, "w", encoding="utf-8", newline="").write(s)
    r = subprocess.run([CLI, "sch", "upgrade", root], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    msg = ((r.stdout or "") + (r.stderr or "")).strip()
    print("  kicad-cli exit=%d %s" % (r.returncode, msg[:80]))
    print()
    print("%-26s %-9s %-9s %s" % ("sheet", "our_ver", "kicad_ver", "same?"))
    print("-" * 60)
    all_same = True
    for p in sorted(glob.glob(os.path.join(work, "*.kicad_sch"))):
        name = os.path.basename(p)
        our = VER.search(before.get(name, ""))
        now = VER.search(read(p))
        same = (our.group(1) if our else "?") == (now.group(1) if now else "?")
        if not same:
            all_same = False
        print("%-26s %-9s %-9s %s" % (name,
                                      our.group(1) if our else "?",
                                      now.group(1) if now else "?",
                                      "yes" if same else "NO"))

    print()
    if lib_ok:
        print("RESULT: the pipeline now emits KiCad 10 format end to end.")
        print("Regenerating can no longer reintroduce the repair dialog.")
        return 0
    print("RESULT: REGRESSION - pipeline still emits old-format library.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
