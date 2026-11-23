#!/usr/bin/env python3
"""sch_repair_repro.py - prove whether KiCad still repairs the schematic on load.

THE PROBLEM
-----------
JP gets a GUI dialog:
  "An error was found when loading the schematic that has been automatically
   fixed. Please save the schematic to repair the broken file or it may not be
   usable with other versions of KiCad."

kicad-cli repair the file IN MEMORY and says nothing, so an export looks clean
whether or not the file is broken. Every "it's fine" verdict from an export was
therefore worthless. This is why JP had to tell me three times.

THE TEST
--------
Copy the whole schematic dir somewhere private, run `kicad-cli sch upgrade` on
the COPY (load + save through KiCad's own parser), then diff copy vs original
ignoring the (date ...) line.

  identical  -> KiCad found nothing to repair. The file is clean.
  different  -> KiCad rewrote something on load = the dialog is real, and the
                diff shows exactly WHAT it fixed.

This is the only scriptable way to reproduce a GUI-only repair warning.
"""
import io
import os
import re
import shutil
import subprocess
import sys

KICAD_CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"

DATE_RE = re.compile(r'^\s*\(date\s+"[^"]*"\)\s*$')
GEN_RE = re.compile(r'^\s*\((generator|generator_version)\s+"?[^")]*"?\)\s*$')


def strip_volatile(text):
    out = []
    for line in text.splitlines():
        if DATE_RE.match(line):
            continue
        if GEN_RE.match(line):
            continue
        out.append(line.rstrip())
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.normpath(os.path.join(here, "..", "..", "pcb", "sgc_p1"))
    if not os.path.isdir(src):
        print("source dir missing: %s" % src)
        return 2

    work = r"C:\sgc\sch_roundtrip"
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(src, work)

    root = os.path.join(work, "sgc_p1.kicad_sch")
    if not os.path.isfile(root):
        print("root sheet missing in copy")
        return 2

    print("round-tripping %s" % work)
    r = subprocess.run([KICAD_CLI, "sch", "upgrade", root],
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    print("  kicad-cli exit=%d" % r.returncode)
    for ln in ((r.stdout or "") + (r.stderr or "")).strip().splitlines():
        print("  | " + ln)

    names = sorted(f for f in os.listdir(src) if f.endswith(".kicad_sch"))
    changed = 0
    for f in names:
        a_path = os.path.join(src, f)
        b_path = os.path.join(work, f)
        if not os.path.isfile(b_path):
            print("  %-26s MISSING after round-trip" % f)
            changed += 1
            continue
        a = strip_volatile(io.open(a_path, encoding="utf-8",
                                   errors="replace").read())
        b = strip_volatile(io.open(b_path, encoding="utf-8",
                                   errors="replace").read())
        if a == b:
            print("  %-26s IDENTICAL  (KiCad repaired nothing)" % f)
            continue

        changed += 1
        print("  %-26s REPAIRED   (%d vs %d lines)" % (f, len(a), len(b)))
        # show the first few differing lines so the fix is visible
        shown = 0
        for i in range(max(len(a), len(b))):
            la = a[i] if i < len(a) else "<eof>"
            lb = b[i] if i < len(b) else "<eof>"
            if la != lb:
                print("      line %d" % (i + 1))
                print("        before: " + la.strip()[:150])
                print("        after : " + lb.strip()[:150])
                shown += 1
                if shown >= 5:
                    print("      ... (further differences suppressed)")
                    break

    print()
    if changed:
        print("RESULT: %d sheet(s) still repaired on load -> the dialog is real"
              % changed)
        print("The 'after' lines above are exactly what KiCad wants written.")
        return 1
    print("RESULT: KiCad 10 loads and re-saves every sheet unchanged.")
    print("No auto-repair happens any more -> the dialog should be gone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
