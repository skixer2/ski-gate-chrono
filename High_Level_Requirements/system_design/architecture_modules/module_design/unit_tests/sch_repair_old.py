#!/usr/bin/env python3
"""sch_repair_old.py - what did KiCad repair in the PRE-UPGRADE schematic?

The forced test (sch_repair_force.py) showed the CURRENT sheets are clean.
But JP's dialog came from a file that had a real defect. To find it, extract
the pre-upgrade sheets from git into a scratch dir, force KiCad to parse them
by downgrading the version marker, and diff what comes back.

Any difference = the defect KiCad was auto-fixing in the dialog.
No difference = the dialog was only the old-format notice.

usage: sch_repair_old.py <git-rev-of-pre-upgrade-commit>
"""
import io
import os
import re
import shutil
import subprocess
import sys

KICAD_CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
RELBASE = ("High_Level_Requirements/system_design/architecture_modules/"
           "pcb/sgc_p1/")
OLD_VERSION = "20231120"

VOLATILE_RE = re.compile(
    r'^\s*\((version|date|generator|generator_version)\b.*$')


def strip_volatile(text):
    return [ln.rstrip() for ln in text.splitlines()
            if not VOLATILE_RE.match(ln)]


def git_show(rev, path):
    r = subprocess.run(["git", "-C", REPO, "show", "%s:%s" % (rev, path)],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        return None
    return r.stdout


def main():
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD~5"

    listing = subprocess.run(
        ["git", "-C", REPO, "ls-tree", "-r", "--name-only", rev, RELBASE],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    if listing.returncode != 0:
        print("cannot list rev %s" % rev)
        return 2

    names = [ln.split("/")[-1] for ln in listing.stdout.splitlines()
             if ln.endswith(".kicad_sch")]
    names.sort()
    if not names:
        print("no .kicad_sch at rev %s" % rev)
        return 2

    work = r"C:\sgc\sch_old"
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    originals = {}
    for n in names:
        text = git_show(rev, RELBASE + n)
        if text is None:
            print("  skip %s (not in rev)" % n)
            continue
        originals[n] = text
        t2 = re.sub(r'\(version \d{8}\)', '(version %s)' % OLD_VERSION,
                    text, count=1)
        io.open(os.path.join(work, n), "w", encoding="utf-8",
                newline="").write(t2)

    print("extracted %d sheets from %s" % (len(originals), rev))
    for n in names:
        if n in originals:
            v = re.search(r'\(version (\d+)\)', originals[n])
            print("  %-26s was version %s" % (n, v.group(1) if v else "?"))

    root = os.path.join(work, "sgc_p1.kicad_sch")
    if not os.path.isfile(root):
        print("no root sheet at rev")
        return 2

    r = subprocess.run([KICAD_CLI, "sch", "upgrade", root],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    print("\n  kicad-cli exit=%d" % r.returncode)
    for ln in (((r.stdout or "") + (r.stderr or "")).strip()).splitlines():
        print("  | " + ln)

    print()
    bad = 0
    for n in names:
        if n not in originals:
            continue
        before = strip_volatile(originals[n])
        after_path = os.path.join(work, n)
        after = strip_volatile(io.open(after_path, encoding="utf-8",
                                       errors="replace").read())
        if before == after:
            print("  %-26s no structural change" % n)
            continue
        bad += 1
        print("  %-26s CHANGED (%d -> %d lines)" % (n, len(before), len(after)))
        shown = 0
        for i in range(max(len(before), len(after))):
            a = before[i] if i < len(before) else "<eof>"
            b = after[i] if i < len(after) else "<eof>"
            if a != b:
                print("      line %d" % (i + 1))
                print("        broken : " + a.strip()[:140])
                print("        fixed  : " + b.strip()[:140])
                shown += 1
                if shown >= 8:
                    print("      ... further differences suppressed")
                    break

    print()
    if bad:
        print("RESULT: %d sheet(s) had real structural defects." % bad)
        print("THESE are what the dialog was repairing.")
        return 1
    print("RESULT: the pre-upgrade sheets differ ONLY in the version stamp.")
    print("There was no structural defect - the dialog was the format notice.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
