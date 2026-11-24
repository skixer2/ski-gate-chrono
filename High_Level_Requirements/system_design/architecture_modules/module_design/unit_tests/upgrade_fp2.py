"""Upgrade the two genuinely-old footprints, with --output as the CLI demands.

kicad-cli fp upgrade refuses in-place for non-current files:
  "Output path must be specified to convert legacy and non-KiCad formats"
So write to a temp file, verify, then swap in.
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
PRETTY = os.path.join(D, "sgc_p1.pretty")
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
BACKUP = r"C:\Users\v17ni\zio_claw\sgc_k10_backup_20261124"

VER = re.compile(r'\(version (\d+)\)')
OLD = ("20221018",)


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
    mods = sorted(glob.glob(os.path.join(PRETTY, "*.kicad_mod")))
    fixed = failed = 0

    for p in mods:
        before = ver_of(p)
        if before not in OLD:
            print("%-42s %s  (already current)" % (os.path.basename(p), before))
            continue

        dst_backup = os.path.join(BACKUP, os.path.basename(p))
        if not os.path.exists(dst_backup):
            shutil.copy2(p, dst_backup)

        tmp_out = os.path.join(tempfile.gettempdir(),
                               "k10_" + os.path.basename(p))
        if os.path.exists(tmp_out):
            os.remove(tmp_out)

        r = subprocess.run([CLI, "fp", "upgrade", "--output", tmp_out, p],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        msg = ((r.stdout or "") + (r.stderr or "")).strip()
        print("%-42s %s -> exit=%d %s"
              % (os.path.basename(p), before, r.returncode, msg[:70]))

        if r.returncode != 0 or not os.path.isfile(tmp_out):
            failed += 1
            continue
        after = ver_of(tmp_out)
        if after == before:
            print("    no change (%s) - leaving original alone" % after)
            failed += 1
            continue
        # sanity: still a footprint with pads
        t = read(tmp_out)
        if "(footprint" not in t or "(pad" not in t:
            print("    REFUSING: output is not a footprint")
            failed += 1
            continue
        shutil.move(tmp_out, p)
        print("    upgraded %s -> %s" % (before, ver_of(p)))
        fixed += 1

    print()
    print("fixed %d, failed %d" % (fixed, failed))
    print()
    print("=== final footprint versions ===")
    for p in sorted(glob.glob(os.path.join(PRETTY, "*.kicad_mod"))):
        print("  %-42s %s" % (os.path.basename(p), ver_of(p)))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
