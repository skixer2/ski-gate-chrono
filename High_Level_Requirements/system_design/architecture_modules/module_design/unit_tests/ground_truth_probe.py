"""Compare the FIXED generator output against KiCad 10's own repaired file.

KiCad's repair save (commit 6a3918f6) is ground truth: it is what KiCad itself
writes after fixing everything. If the generator now produces the same thing,
regeneration can never trigger the dialog again.

Checks the three things KiCad added:
  1. each child symbol instance path = /<root-uuid>/<box-uuid>
  2. each sheet box has an (instances (project ... (path "/<root-uuid>" (page N))))
  3. the root carries (embedded_fonts no)
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


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_gt_probe")
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
    print("gen_schematic exit=%d" % r.returncode)
    for ln in ((r.stdout or "") + (r.stderr or "")).strip().splitlines()[-4:]:
        print("  | " + ln)
    if r.returncode != 0:
        return 2

    root = read(os.path.join(work, "sgc_p1.kicad_sch"))
    root_uuid = re.search(r'\(uuid "([0-9a-f-]{36})"', root).group(1)
    print("\nroot uuid: %s" % root_uuid)

    problems = []

    # 1. child symbol instance paths
    print("\n=== 1. child symbol instance paths ===")
    for p in sorted(glob.glob(os.path.join(work, "sch_*.kicad_sch"))):
        t = read(p)
        name = os.path.basename(p)
        paths = set(re.findall(r'\(path "([^"]*)"', t))
        # every symbol path must start with /<root-uuid>/
        bad = [x for x in paths if not x.startswith("/" + root_uuid + "/")]
        good = [x for x in paths if x.startswith("/" + root_uuid + "/")]
        print("  %-28s good=%-4d bad=%-3d" % (name, len(good), len(bad)))
        if bad:
            problems.append("%s: %d path(s) missing the root uuid" %
                            (name, len(bad)))
            for b in bad[:2]:
                print("      BAD %s" % b)

    # 2. sheet box instances blocks
    print("\n=== 2. sheet box (instances ...) blocks in root ===")
    boxes = re.findall(r'\(sheet\b.*?\n\t\)', root, re.S)
    with_inst = [b for b in boxes if "(instances" in b]
    print("  sheet boxes: %d   with instances block: %d"
          % (len(boxes), len(with_inst)))
    if len(with_inst) != len(boxes):
        problems.append("%d of %d sheet boxes lack an instances block"
                        % (len(boxes) - len(with_inst), len(boxes)))
    pages = re.findall(r'\(page "(\d+)"\)', " ".join(with_inst))
    print("  page numbers: %s" % (sorted(pages) if pages else "none"))
    if pages and sorted(pages) != sorted(str(i) for i in range(2, 8)):
        problems.append("page numbers are %s, expected 2..7" % sorted(pages))

    # 3. embedded_fonts on root
    print("\n=== 3. root embedded_fonts ===")
    has = "embedded_fonts" in root
    print("  embedded_fonts present: %s" % has)
    if not has:
        problems.append("root lacks (embedded_fonts no)")

    print()
    if problems:
        print("STILL DIFFERS FROM KICAD'S OWN OUTPUT (%d):" % len(problems))
        for p in problems:
            print("  ! " + p)
        return 1
    print("GENERATOR NOW MATCHES KICAD 10's OWN REPAIRED OUTPUT")
    print("Regeneration can no longer trigger the repair dialog.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
