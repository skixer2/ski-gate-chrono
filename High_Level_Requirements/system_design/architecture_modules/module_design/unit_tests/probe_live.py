"""Prove whether KiCad 10 still repairs the LIVE schematic on load.

Runs entirely in Python: copy the sheet set to a scratch dir, force the
version marker back to 20231120, let kicad-cli rewrite, then diff
semantically. Also reports the exact dialog-relevant tokens.
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
OLD = "20231120"

VOL = re.compile(r'^\s*\((version|date|generator|generator_version)\b.*$')
UUID_BARE = re.compile(r'\(uuid\s+([^"\s()][^\s()]*)')
UUID_OK = re.compile(r'\(uuid\s+"([^"]+)"')


def norm(text):
    return [l.strip() for l in text.splitlines()
            if l.strip() and not VOL.match(l)]


def main():
    names = sorted(os.path.basename(p)
                   for p in glob.glob(os.path.join(SRC, "*.kicad_sch")))
    if not names:
        print("no sheets found in %s" % SRC)
        return 2
    print("source: %s" % SRC)
    print("sheets: %d\n" % len(names))

    work = os.path.join(tempfile.gettempdir(), "sgc_force_probe")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    originals = {}
    for n in names:
        p = os.path.join(SRC, n)
        raw = io.open(p, "rb").read()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("cp1252", "replace")
        originals[n] = text
        forced = re.sub(r'\(version \d{8}\)', '(version %s)' % OLD, text, count=1)
        io.open(os.path.join(work, n), "w", encoding="utf-8",
                newline="").write(forced)

    # report the raw defect counts on the LIVE files first
    print("%-28s %8s %8s" % ("sheet", "quoted", "BARE"))
    print("-" * 48)
    tq = tb = 0
    for n in names:
        q = len(UUID_OK.findall(originals[n]))
        b = len(UUID_BARE.findall(originals[n]))
        tq += q
        tb += b
        print("%-28s %8d %8d%s" % (n, q, b, "   <-- BARE!" if b else ""))
    print("-" * 48)
    print("%-28s %8d %8d" % ("TOTAL", tq, tb))
    print()

    root = os.path.join(work, "sgc_p1.kicad_sch")
    cp = subprocess.run([CLI, "sch", "upgrade", root], capture_output=True,
                        text=True, encoding="utf-8", errors="replace")
    msg = ((cp.stdout or "") + (cp.stderr or "")).strip()
    print("kicad-cli exit=%d" % cp.returncode)
    for ln in msg.splitlines():
        print("  | " + ln)
    if "not updated" in msg.lower():
        print("\nINCONCLUSIVE: no rewrite happened.")
        return 2

    print()
    changed = 0
    for n in names:
        a = norm(originals[n])
        b = norm(io.open(os.path.join(work, n), encoding="utf-8",
                         errors="replace").read())
        if a == b:
            print("%-28s unchanged by KiCad" % n)
            continue
        changed += 1
        print("%-28s CHANGED (%d -> %d lines)" % (n, len(a), len(b)))
        # set difference is far more informative than index alignment
        sa, sb = set(a), set(b)
        only_a = [x for x in a if x not in sb]
        only_b = [x for x in b if x not in sa]
        for x in only_a[:6]:
            print("    live only : " + x[:130])
        for x in only_b[:6]:
            print("    kicad out : " + x[:130])
        if len(only_a) > 6:
            print("    ... %d more live-only lines" % (len(only_a) - 6))
        if len(only_b) > 6:
            print("    ... %d more kicad-only lines" % (len(only_b) - 6))

    print()
    if changed:
        print("VERDICT: %d sheet(s) still repaired on load." % changed)
        return 1
    print("VERDICT: KiCad 10 rewrites nothing - the live sheets are clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
