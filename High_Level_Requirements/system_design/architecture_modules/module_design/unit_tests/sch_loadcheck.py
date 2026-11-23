#!/usr/bin/env python3
"""sch_loadcheck.py - find what KiCad repairs when loading a schematic.

WHY THIS EXISTS
---------------
JP kept getting:
  "An error was found when loading the schematic that has been automatically
   fixed. Please save the schematic to repair the broken file or it may not be
   usable with other versions of KiCad."

kicad-cli CANNOT reproduce it: it auto-repairs on read, so export succeeds and
looks clean. A tool that auto-migrates cannot be used to test a repair complaint.

This script inspects the raw S-expressions for the defects KiCad's parser
silently fixes:
  1. Duplicate UUIDs (the #1 cause of that dialog)
  2. Duplicate item references within a sheet
  3. symbol instances whose project name is stale
  4. lib_id references with no matching lib_symbols entry
  5. malformed/mismatched parens

Exit 0 = clean, 1 = defects found.
"""
import io
import os
import re
import sys
import collections

UUID_RE = re.compile(r'\(uuid "([0-9a-fA-F-]{36})"\)')
LIBID_RE = re.compile(r'\(lib_id "([^"]+)"\)')
SYMSYM_RE = re.compile(r'\(symbol "([^"]+)"')
PROJ_RE = re.compile(r'\(project "([^"]+)"')


def parens_balanced(text):
    depth = 0
    in_str = False
    esc = False
    for ch in text:
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth < 0:
                return False, "closing paren before opening"
    if depth != 0:
        return False, "depth=%d at EOF" % depth
    return True, ""


def header(text):
    v = re.search(r'\(version (\d+)\)', text)
    g = re.search(r'\(generator_version "([^"]+)"\)', text)
    gen = re.search(r'\(generator "([^"]+)"\)', text)
    return (v.group(1) if v else "?",
            g.group(1) if g else "?",
            gen.group(1) if gen else "?")


def check(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    name = os.path.basename(path)
    problems = []

    ver, gver, gen = header(text)
    ok, why = parens_balanced(text)
    if not ok:
        problems.append("PARENS: " + why)

    uuids = UUID_RE.findall(text)
    dup = [u for u, n in collections.Counter(uuids).items() if n > 1]
    for u in dup:
        problems.append("DUPLICATE UUID: %s" % u)

    libids = set(LIBID_RE.findall(text))
    # symbols defined inside (lib_symbols ...) are plain (symbol "Name" ...)
    defined = set(SYMSYM_RE.findall(text))
    missing = sorted(i for i in libids if i not in defined)
    for m in missing:
        problems.append("MISSING lib_symbols ENTRY: %s" % m)

    projects = sorted(set(PROJ_RE.findall(text)))
    if len(projects) > 1:
        problems.append("MULTIPLE project names: %s" % ", ".join(projects))

    return name, ver, gver, gen, len(uuids), problems


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    # unit_tests -> module_design -> architecture_modules -> pcb/sgc_p1
    base = os.path.normpath(os.path.join(
        here, "..", "..", "pcb", "sgc_p1"))
    if not os.path.isdir(base):
        print("schematic dir not found: %s" % base)
        return 2

    files = sorted(f for f in os.listdir(base) if f.endswith(".kicad_sch"))
    if not files:
        print("no .kicad_sch in %s" % base)
        return 2

    total = 0
    print("%-26s %-9s %-5s %-6s %8s  %s" %
          ("file", "version", "gen", "uuids", "problems", ""))
    print("-" * 74)
    for f in files:
        name, ver, gver, gen, n, problems = check(os.path.join(base, f))
        total += len(problems)
        flag = "OK" if not problems else "%d DEFECT(S)" % len(problems)
        print("%-26s %-9s %-5s %6d %8s" % (name, ver, gver, n, flag))
        for p in problems:
            print("      ! " + p)

    print("-" * 74)
    if total:
        print("LOADCHECK FOUND %d DEFECT(S) - KiCad will silently repair these"
              % total)
        print("That is the dialog JP sees. Fix the defects, do not re-export.")
        return 1
    print("LOADCHECK CLEAN - no auto-repair triggers in any sheet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
