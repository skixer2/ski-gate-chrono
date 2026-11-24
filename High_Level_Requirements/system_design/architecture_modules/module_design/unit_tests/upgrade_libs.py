"""Upgrade the symbol library (and footprints) to KiCad 10 format.

THE BUG THIS FIXES
------------------
On 2026-11-23 I upgraded the seven .kicad_sch sheets to 20260306 and declared
the schematic fixed. I never touched sgc_p1.kicad_sym, which sym-lib-table
loads by name. KiCad opens the schematic, resolves the library, finds format
20231120 (KiCad 8), and migrates it on load - which is precisely
"An error was found when loading the schematic that has been automatically
fixed. Please save the schematic to repair the broken file."

kicad-cli never complains because it migrates silently. The user sees the
dialog. Three rounds of sheet-level checks could not see this.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
BACKUP = r"C:\Users\v17ni\zio_claw\sgc_k10_backup_20261124"

VER = re.compile(r'\(version (\d+)\)')


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def ver_of(p):
    m = VER.search(read(p))
    return m.group(1) if m else "?"


def main():
    os.makedirs(BACKUP, exist_ok=True)

    targets = [os.path.join(D, "sgc_p1.kicad_sym")]
    targets += sorted(glob.glob(os.path.join(D, "sgc_p1.pretty", "*.kicad_mod")))

    print("=== BEFORE ===")
    for p in targets:
        print("  %-48s %s" % (os.path.basename(p), ver_of(p)))
    print()

    # back up first (recoverable beats gone)
    for p in targets:
        dst = os.path.join(BACKUP, os.path.basename(p))
        if not os.path.exists(dst):
            shutil.copy2(p, dst)
    print("backed up %d file(s) to %s\n" % (len(targets), BACKUP))

    # how does kicad-cli migrate a symbol library?
    for sub in (["sym", "upgrade"], ["create-project"]):
        r = subprocess.run([CLI] + sub + ["--help"], capture_output=True,
                           text=True, encoding="utf-8", errors="replace")
        head = ((r.stdout or "") + (r.stderr or "")).strip().splitlines()
        print("kicad-cli %s --help -> exit %d" % (" ".join(sub), r.returncode))
        for ln in head[:6]:
            print("   | " + ln)
        print()

    # try the documented route for the symbol lib
    lib = os.path.join(D, "sgc_p1.kicad_sym")
    for args in (["sym", "upgrade", lib],
                 ["create-project", "--force", os.path.join(D, "sgc_p1.kicad_pro")]):
        r = subprocess.run([CLI] + args, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        msg = ((r.stdout or "") + (r.stderr or "")).strip()
        print("RUN %s" % " ".join(args[:2]))
        print("    exit=%d  %s" % (r.returncode, msg[:200]))
        print("    lib version now: %s" % ver_of(lib))
        print()

    print("=== AFTER ===")
    bad = 0
    for p in targets:
        v = ver_of(p)
        print("  %-48s %s" % (os.path.basename(p), v))
        if v == "20231120" or v.startswith("2022"):
            bad += 1
    print()
    if bad:
        print("%d file(s) STILL old format - CLI cannot migrate them." % bad)
        return 1
    print("all targets at current format")
    return 0


if __name__ == "__main__":
    sys.exit(main())
