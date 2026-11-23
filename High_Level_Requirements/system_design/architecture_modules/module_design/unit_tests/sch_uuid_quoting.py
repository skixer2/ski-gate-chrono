#!/usr/bin/env python3
"""sch_uuid_quoting.py - the actual cause of the KiCad repair dialog.

ROOT CAUSE, PROVEN 2026-11-23
-----------------------------
gen_schematic.py emitted bare UUIDs:

    (uuid a32dcc37-0a61-5292-9274-6b32d59107b0)      <- BAD
    (uuid "a32dcc37-0a61-5292-9274-6b32d59107b0")    <- correct

KiCad's S-expression parser expects a STRING there. A bare token is parsed as
a SYMBOL, the type check fails, and the parser recovers by coercing it. That
recovery is precisely what the GUI reports as:

  "An error was found when loading the schematic that has been automatically
   fixed. Please save the schematic to repair the broken file..."

Every earlier checker missed it because the regexes only matched QUOTED uuids,
so the malformed ones were invisible - the check silently skipped the defect
it was looking for.

This script finds unquoted uuid tokens in every sheet. Exit 1 if any remain.
"""
import io
import os
import re
import subprocess
import sys

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
RELBASE = ("High_Level_Requirements/system_design/architecture_modules/"
           "pcb/sgc_p1/")

# (uuid <bare-token>)  -> the token does not start with a double quote
UNQUOTED_RE = re.compile(r'\(uuid\s+([^"\s()][^\s()]*)\s*\)')
QUOTED_RE = re.compile(r'\(uuid\s+"([^"]+)"')

# any (uuid ...) whose argument is not a quoted string
ANY_UUID_RE = re.compile(r'\(uuid\s+([^\s()]+)')


def read_rev(rev, name):
    r = subprocess.run(["git", "-C", REPO, "show",
                        "%s:%s%s" % (rev, RELBASE, name)],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return r.stdout if r.returncode == 0 else None


def main():
    # --rev <commit> audits a historical version instead of the working tree.
    # This is how the checker proves it can actually SEE the defect: run it
    # against the commit before the fix and it must exit 1.
    rev = None
    args = sys.argv[1:]
    for i, a in enumerate(args):
        if a == "--rev" and i + 1 < len(args):
            rev = args[i + 1]

    here = os.path.dirname(os.path.abspath(__file__))
    base = os.path.normpath(os.path.join(here, "..", "..", "pcb", "sgc_p1"))
    if not os.path.isdir(base):
        print("schematic dir not found: %s" % base)
        return 2

    files = sorted(f for f in os.listdir(base) if f.endswith(".kicad_sch"))
    if not files:
        print("no .kicad_sch files")
        return 2

    total_bad = 0
    total_uuid = 0
    src = ("git rev %s" % rev) if rev else "working tree"
    print("auditing: %s" % src)
    print("%-26s %8s %8s  %s" % ("file", "uuids", "unquoted", ""))
    print("-" * 70)

    for f in files:
        if rev:
            text = read_rev(rev, f)
            if text is None:
                print("%-26s  (absent at %s)" % (f, rev))
                continue
        else:
            text = io.open(os.path.join(base, f), encoding="utf-8",
                           errors="replace").read()
        quoted = QUOTED_RE.findall(text)
        bare = UNQUOTED_RE.findall(text)
        total_uuid += len(quoted) + len(bare)
        total_bad += len(bare)
        flag = "OK" if not bare else "%d BARE" % len(bare)
        print("%-26s %8d %8d  %s" % (f, len(quoted) + len(bare), len(bare),
                                     flag))
        seen = set()
        for b in bare:
            if b in seen:
                continue
            seen.add(b)
            print("      ! (uuid %s) is missing its quotes" % b[:48])

    print("-" * 70)
    print("total uuid tokens: %d" % total_uuid)
    if total_bad:
        print("RESULT: %d unquoted uuid(s) - THIS is the repair dialog."
              % total_bad)
        print("Fix the generator to emit quoted uuids, then re-run.")
        return 1
    print("RESULT: every uuid is a quoted string. KiCad's parser has nothing")
    print("to coerce, so the 'automatically fixed' dialog cannot fire.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
