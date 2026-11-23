#!/usr/bin/env python3
"""sch_root_diff.py - semantic diff of the root sheet, old vs current.

Line-index alignment in sch_repair_old.py was misleading: KiCad reformatted
the whole file, so index N in old is not index N in new. Use a real LCS diff
and report only ADDED/REMOVED content, ignoring pure reflow.

usage: sch_root_diff.py <old-rev>
"""
import io
import os
import re
import subprocess
import sys
import difflib

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
RELBASE = ("High_Level_Requirements/system_design/architecture_modules/"
           "pcb/sgc_p1/")
NAME = "sgc_p1.kicad_sch"

VOLATILE_RE = re.compile(
    r'^\s*\((version|date|generator|generator_version)\b.*$')


def norm(text):
    """Token-level normalisation: strip formatting, keep semantics."""
    out = []
    for ln in text.splitlines():
        s = ln.strip()
        if not s or VOLATILE_RE.match(s):
            continue
        # collapse internal whitespace
        s = re.sub(r'\s+', ' ', s)
        out.append(s)
    return out


def git_show(rev, path):
    r = subprocess.run(["git", "-C", REPO, "show", "%s:%s" % (rev, path)],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return r.stdout if r.returncode == 0 else None


def main():
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD~1"
    old = git_show(rev, RELBASE + NAME)
    if old is None:
        print("cannot read %s at %s" % (NAME, rev))
        return 2
    cur_path = os.path.join(REPO.replace("/", os.sep),
                            RELBASE.replace("/", os.sep) + NAME)
    cur = io.open(cur_path, encoding="utf-8", errors="replace").read()

    a, b = norm(old), norm(cur)
    print("old %s: %d semantic lines" % (rev, len(a)))
    print("current : %d semantic lines" % len(b))
    print()

    diff = list(difflib.unified_diff(a, b, lineterm="", n=2,
                                     fromfile="old", tofile="current"))
    if not diff:
        print("NO SEMANTIC DIFFERENCE - the files are equivalent.")
        return 0

    removed = [l for l in diff if l.startswith("-") and not l.startswith("---")]
    added = [l for l in diff if l.startswith("+") and not l.startswith("+++")]
    print("removed %d lines, added %d lines" % (len(removed), len(added)))
    print()
    for l in diff:
        print(l[:170])
    print()
    print("If every removed line has an equivalent added line, this is pure")
    print("reformatting. Anything else is a real repair KiCad performed.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
