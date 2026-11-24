"""Upgrade footprints and the PCB to KiCad 10 format.

Only upgrades what is genuinely old. Asks kicad-cli what it actually wants to
migrate rather than assuming a version number, because each KiCad file family
has its OWN version sequence:
    .kicad_sch / .kicad_pcb  20260306 / 20260206
    .kicad_sym               20251024
    .kicad_mod               20260206
Comparing every family against one constant is what made my earlier audit
report false positives.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys

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


def run(args):
    r = subprocess.run([CLI] + args, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return r.returncode, ((r.stdout or "") + (r.stderr or "")).strip()


def main():
    os.makedirs(BACKUP, exist_ok=True)

    print("=== FOOTPRINTS ===")
    mods = sorted(glob.glob(os.path.join(D, "sgc_p1.pretty", "*.kicad_mod")))
    for p in mods:
        before = ver_of(p)
        dst = os.path.join(BACKUP, os.path.basename(p))
        if not os.path.exists(dst):
            shutil.copy2(p, dst)
        rc, msg = run(["fp", "upgrade", p])
        after = ver_of(p)
        print("  %-42s %s -> %s  exit=%d %s"
              % (os.path.basename(p), before, after, rc, msg[:60]))

    print()
    print("=== PCB ===")
    pcb = os.path.join(D, "sgc_p1.kicad_pcb")
    if os.path.isfile(pcb):
        before = ver_of(pcb)
        dst = os.path.join(BACKUP, os.path.basename(pcb))
        if not os.path.exists(dst):
            shutil.copy2(pcb, dst)
        rc, msg = run(["pcb", "upgrade", pcb])
        after = ver_of(pcb)
        print("  sgc_p1.kicad_pcb  %s -> %s  exit=%d %s"
              % (before, after, rc, msg[:70]))

    print()
    print("=== FINAL STATE (per family) ===")
    fam = {
        ".kicad_sch": sorted(glob.glob(os.path.join(D, "*.kicad_sch"))),
        ".kicad_sym": [os.path.join(D, "sgc_p1.kicad_sym")],
        ".kicad_mod": mods,
        ".kicad_pcb": [pcb] if os.path.isfile(pcb) else [],
    }
    for ext, files in fam.items():
        seen = {}
        for f in files:
            if os.path.isfile(f):
                seen.setdefault(ver_of(f), []).append(os.path.basename(f))
        for v, names in sorted(seen.items()):
            print("  %-12s %-10s %d file(s)" % (ext, v, len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
