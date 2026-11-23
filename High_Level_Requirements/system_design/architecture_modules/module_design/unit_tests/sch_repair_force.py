#!/usr/bin/env python3
"""sch_repair_force.py - force KiCad to fully parse+rewrite, then diff.

WHY THE PREVIOUS TEST WAS NOT ENOUGH
------------------------------------
sch_repair_repro.py ran `kicad-cli sch upgrade` and got
    "Schematic file was not updated"
That means KiCad saw version 20260306 already matched and SKIPPED the rewrite.
A skip proves nothing: the repair path never ran. Same class of mistake as
trusting an export that auto-repairs silently.

THE FORCED TEST
---------------
1. copy the sheet set
2. rewrite the version marker back to 20231120 (the old value)
3. run `kicad-cli sch upgrade` -> it MUST now parse and rewrite every sheet
4. strip the version/date/generator lines and diff against the on-disk file

If the only difference is the version stamp, then KiCad's parser had nothing
structural to repair and JP's dialog was about the format, now fixed.
If anything else differs, that difference IS what KiCad was fixing, and it
gets printed.
"""
import io
import os
import re
import shutil
import subprocess
import sys

KICAD_CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
OLD_VERSION = "20231120"

VOLATILE_RE = re.compile(
    r'^\s*\((version|date|generator|generator_version)\b.*$')


def strip_volatile(text):
    return [ln.rstrip() for ln in text.splitlines()
            if not VOLATILE_RE.match(ln)]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.normpath(os.path.join(here, "..", "..", "pcb", "sgc_p1"))
    if not os.path.isdir(src):
        print("source dir missing: %s" % src)
        return 2

    work = r"C:\sgc\sch_force"
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(src, work)

    names = sorted(f for f in os.listdir(src) if f.endswith(".kicad_sch"))

    # 2. downgrade the version marker in every copied sheet
    for f in names:
        p = os.path.join(work, f)
        t = io.open(p, encoding="utf-8", errors="replace").read()
        t2 = re.sub(r'\(version \d{8}\)', '(version %s)' % OLD_VERSION, t, 1)
        if t2 == t:
            print("  warn: no version marker rewritten in %s" % f)
        io.open(p, "w", encoding="utf-8", newline="").write(t2)

    print("forced %d sheets to version %s in the copy" % (len(names),
                                                          OLD_VERSION))
    root = os.path.join(work, "sgc_p1.kicad_sch")
    r = subprocess.run([KICAD_CLI, "sch", "upgrade", root],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    print("  kicad-cli exit=%d" % r.returncode)
    msg = ((r.stdout or "") + (r.stderr or "")).strip()
    for ln in msg.splitlines():
        print("  | " + ln)
    if "not updated" in msg.lower():
        print()
        print("INCONCLUSIVE: KiCad still refused to rewrite. The forced test")
        print("did not exercise the parser. Do not claim the file is clean.")
        return 2

    print()
    bad = 0
    for f in names:
        a_path = os.path.join(src, f)
        b_path = os.path.join(work, f)
        if not os.path.isfile(b_path):
            print("  %-26s MISSING after rewrite" % f)
            bad += 1
            continue
        a = strip_volatile(io.open(a_path, encoding="utf-8",
                                   errors="replace").read())
        b = strip_volatile(io.open(b_path, encoding="utf-8",
                                   errors="replace").read())
        if a == b:
            print("  %-26s IDENTICAL after full parse+rewrite" % f)
            continue
        bad += 1
        print("  %-26s DIFFERS  (%d -> %d lines)" % (f, len(a), len(b)))
        shown = 0
        for i in range(max(len(a), len(b))):
            la = a[i] if i < len(a) else "<eof>"
            lb = b[i] if i < len(b) else "<eof>"
            if la != lb:
                print("      line %d" % (i + 1))
                print("        on disk : " + la.strip()[:140])
                print("        reprocessed: " + lb.strip()[:140])
                shown += 1
                if shown >= 6:
                    print("      ... further differences suppressed")
                    break

    print()
    if bad:
        print("RESULT: %d sheet(s) change under a forced KiCad 10 rewrite." % bad)
        print("Those differences are the repairs KiCad performs on load.")
        return 1
    print("RESULT: every sheet survives a forced KiCad 10 parse+rewrite with")
    print("only the version stamp touched. No structural repair occurs.")
    print("The dialog JP saw was the old-format notice, now resolved.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
