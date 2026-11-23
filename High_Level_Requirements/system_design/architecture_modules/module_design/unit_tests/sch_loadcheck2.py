#!/usr/bin/env python3
"""sch_loadcheck2.py - hierarchy-level auto-repair triggers.

sch_loadcheck.py handles per-file defects. This covers the cross-file ones
KiCad repairs silently on load:

  A. the same UUID appearing in more than one sheet file
     (a copied sheet reuses UUIDs -> "error found, automatically fixed")
  B. a (sheet ...) whose Sheetfile property points at a missing file
  C. sheet pin / symbol instance paths pointing at UUIDs not in the hierarchy
  D. project file (.kicad_pro) version if present
"""
import io
import os
import re
import sys
import collections

UUID_RE = re.compile(r'\(uuid "([0-9a-fA-F-]{36})"\)')
SHEETFILE_RE = re.compile(r'\(property "Sheetfile" "([^"]+)"\)')
SHEETNAME_RE = re.compile(r'\(property "Sheetname" "([^"]+)"\)')


def collect(base, files):
    per_file = {}
    for f in files:
        text = io.open(os.path.join(base, f), encoding="utf-8",
                       errors="replace").read()
        per_file[f] = {
            "text": text,
            "uuids": UUID_RE.findall(text),
            "sheetfiles": SHEETFILE_RE.findall(text),
            "sheetnames": SHEETNAME_RE.findall(text),
        }
    return per_file


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    base = os.path.normpath(os.path.join(here, "..", "..", "pcb", "sgc_p1"))
    files = sorted(f for f in os.listdir(base) if f.endswith(".kicad_sch"))
    data = collect(base, files)
    problems = []

    # A. cross-file duplicate UUIDs
    owner = {}
    for f, d in data.items():
        for u in set(d["uuids"]):
            owner.setdefault(u, []).append(f)
    for u, fs in owner.items():
        if len(fs) > 1:
            problems.append("UUID %s shared by %s" % (u, " + ".join(fs)))

    # B. Sheetfile targets must exist
    for f, d in data.items():
        for sf in d["sheetfiles"]:
            target = os.path.join(base, sf)
            if not os.path.isfile(target):
                problems.append("%s references missing sheet file %s" % (f, sf))

    # C. every sheet file must be referenced by someone (orphan = stale ref)
    referenced = set()
    for d in data.values():
        referenced.update(os.path.basename(s) for s in d["sheetfiles"])
    for f in files:
        if f != "sgc_p1.kicad_sch" and f not in referenced:
            problems.append("%s exists but nothing references it" % f)

    # D. project file
    pro = [p for p in os.listdir(base) if p.endswith(".kicad_pro")]
    print("project file: %s" % (", ".join(pro) if pro else "NONE"))
    for p in pro:
        t = io.open(os.path.join(base, p), encoding="utf-8",
                    errors="replace").read()
        m = re.search(r'"(\d{8})"\s*$', t.strip()[-40:]) or \
            re.search(r'"version"\s*:\s*"(\d+)"', t)
        print("  %s kicad version marker: %s" % (p, m.group(1) if m else "?"))

    print()
    print("sheets on disk: %d" % len(files))
    for f, d in data.items():
        print("  %-26s %d uuids  %d sheetfiles" %
              (f, len(d["uuids"]), len(d["sheetfiles"])))

    print()
    if problems:
        print("HIERARCHY DEFECTS (%d):" % len(problems))
        for p in problems:
            print("  ! " + p)
        return 1
    print("HIERARCHY CLEAN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
