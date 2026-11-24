"""Verify the regenerated tree against KiCad 10 by asking KiCad to rewrite it.

Ground truth = KiCad's own repair save (commit 6a3918f6). The most reliable
question is: if KiCad parses our fresh output, does it change anything?

Approach: generate into a scratch dir, then let `kicad-cli sch upgrade` parse
each sheet (forcing a rewrite via a downgraded version stamp), and diff
semantically. Any remaining difference is something KiCad still normalises.
"""
import difflib
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

VOL = re.compile(r'^\s*\((version|date|generator|generator_version)\b')


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def norm(t):
    return [l.strip() for l in t.splitlines() if l.strip() and not VOL.match(l)]


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_gt2")
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

    r = subprocess.run([PY, "gen_schematic.py"], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    print("generator exit=%d" % r.returncode)
    if r.returncode != 0:
        print(((r.stdout or "") + (r.stderr or ""))[-800:])
        return 2

    before = {os.path.basename(p): read(p)
              for p in glob.glob(os.path.join(work, "*.kicad_sch"))}

    # force every sheet through a real KiCad parse+rewrite
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        t = re.sub(r'\(version \d{8}\)', '(version 20231120)', read(p), count=1)
        io.open(p, "w", encoding="utf-8", newline="").write(t)

    # upgrade EACH sheet (not just the root - the root does not cascade)
    for p in sorted(glob.glob(os.path.join(work, "*.kicad_sch"))):
        cp = subprocess.run([CLI, "sch", "upgrade", p], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
        msg = ((cp.stdout or "") + (cp.stderr or "")).strip()
        print("  %-28s exit=%d %s" % (os.path.basename(p), cp.returncode,
                                      msg[:50]))

    print()
    total = 0
    for name in sorted(before):
        a = norm(before[name])
        b = norm(read(os.path.join(work, name)))
        if a == b:
            print("%-28s IDENTICAL after KiCad parse" % name)
            continue
        sa, sb = set(a), set(b)
        only_a = [x for x in a if x not in sb]
        only_b = [x for x in b if x not in sa]
        total += len(only_a) + len(only_b)
        print("%-28s DIFFERS: %d ours-only, %d kicad-only"
              % (name, len(only_a), len(only_b)))
        for x in only_a[:4]:
            print("    OURS  " + x[:120])
        for x in only_b[:4]:
            print("    KICAD " + x[:120])

    print()
    if total == 0:
        print("MATCH: KiCad parses our generated tree without changing anything.")
        print("That is the same test KiCad's own repair save implies, and it passes.")
        return 0
    print("KiCad still normalises %d line(s) - see above." % total)
    return 1


if __name__ == "__main__":
    sys.exit(main())
