"""The test I have never run: does the PROJECT load cleanly, not just the sheets?

All my previous checks exported individual .kicad_sch files with kicad-cli.
KiCad's GUI opens the .kicad_pro first, then loads the hierarchy THROUGH the
project (resolving ${KIPRJMOD}, lib tables, instance data). That path is
different and I have never exercised it.

This does a full project-level round trip in a scratch copy and diffs every
file, so any repair KiCad performs at PROJECT level becomes visible.
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
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"

VER = re.compile(r'^\s*\((version|date|generator|generator_version)\b.*$')


def norm(text):
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or VER.match(ln):
            continue
        out.append(s)
    return out


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def copy_tree(dst):
    """Copy ONLY the files a project needs, plus lib tables."""
    os.makedirs(dst, exist_ok=True)
    pats = ["*.kicad_sch", "*.kicad_pro", "*.kicad_sym",
            "sym-lib-table", "fp-lib-table", "*.kicad_wks"]
    n = 0
    for pat in pats:
        for f in glob.glob(os.path.join(SRC, pat)):
            shutil.copy2(f, os.path.join(dst, os.path.basename(f)))
            n += 1
    pretty_src = os.path.join(SRC, "sgc_p1.pretty")
    if os.path.isdir(pretty_src):
        shutil.copytree(pretty_src, os.path.join(dst, "sgc_p1.pretty"),
                        dirs_exist_ok=True)
    return n


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_proj_probe")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    n = copy_tree(work)
    print("copied %d project files to %s\n" % (n, work))

    # baseline
    before = {}
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        before[os.path.basename(p)] = read(p)

    # 1. full project ERC (loads the whole hierarchy through the project)
    pro = os.path.join(work, "sgc_p1.kicad_pro")
    sch = os.path.join(work, "sgc_p1.kicad_sch")
    print("=== 1. sch erc --severity-all (project context) ===")
    r = subprocess.run([CLI, "sch", "erc", "--severity-all",
                        "-o", os.path.join(work, "out.rpt"), sch],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    msg = ((r.stdout or "") + (r.stderr or "")).strip()
    print("exit=%d" % r.returncode)
    for ln in msg.splitlines()[:12]:
        print("  | " + ln)

    # look for the words the GUI dialog uses
    print()
    print("=== 2. grep kicad-cli output for repair/warning language ===")
    lowered = msg.lower()
    for needle in ("automatically fixed", "error was found", "repair",
                   "warning", "migrat", "rescue", "recovered"):
        if needle in lowered:
            for ln in msg.splitlines():
                if needle in ln.lower():
                    print("  HIT [%s] %s" % (needle, ln.strip()[:150]))
        else:
            print("  no hit: %s" % needle)

    # 3. did ERC/load MODIFY any sheet? that is the repair happening.
    print()
    print("=== 3. did the load modify any sheet? (this IS the repair) ===")
    changed = 0
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        name = os.path.basename(p)
        now = read(p)
        if name in before and norm(before[name]) != norm(now):
            changed += 1
            a, b = norm(before[name]), norm(now)
            print("  %s CHANGED (%d -> %d lines)" % (name, len(a), len(b)))
            sa, sb = set(a), set(b)
            for x in [y for y in a if y not in sb][:5]:
                print("      - " + x[:130])
            for x in [y for y in b if y not in sa][:5]:
                print("      + " + x[:130])
        else:
            print("  %-28s unchanged" % name)

    print()
    if changed:
        print("PROJECT LOAD REPAIRED %d sheet(s)" % changed)
        return 1
    print("PROJECT LOAD IS CLEAN - no sheet was rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
