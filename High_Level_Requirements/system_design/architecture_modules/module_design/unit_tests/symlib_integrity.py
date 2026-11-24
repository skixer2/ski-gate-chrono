"""Did `kicad-cli sym upgrade` preserve every symbol?

I ran it on 2026-11-24 and took the "Saving symbol library in updated format"
message as success. If it dropped or mangled symbols, the schematic would fail
to resolve them on load and KiCad would rescue/repair - which is exactly the
dialog JP sees.

Compares the backup against the upgraded file: symbol count, names, pin counts.
"""
import io
import os
import re

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
BACKUP = r"C:\Users\v17ni\zio_claw\sgc_k10_backup_20261124\sgc_p1.kicad_sym"
LIVE = os.path.join(D, "sgc_p1.kicad_sym")

SYM = re.compile(r'^\s*\(symbol "([^"]+)"', re.M)
PIN = re.compile(r'\(pin\s')


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def stats(p):
    t = read(p)
    names = SYM.findall(t)
    # top-level symbols only (not the unit children like NAME_0_1)
    top = sorted(set(n for n in names if not re.match(r'.+_\d+_\d+$', n)))
    return {
        "bytes": os.path.getsize(p),
        "symbols_total": len(names),
        "symbols_top": len(top),
        "names": top,
        "pins": len(PIN.findall(t)),
        "version": (re.search(r'\(version (\d+)\)', t) or [None, "?"])[1],
    }


def main():
    if not os.path.isfile(BACKUP):
        print("no backup at %s" % BACKUP)
        return 2
    a = stats(BACKUP)
    b = stats(LIVE)

    print("%-22s %-18s %-18s" % ("metric", "BEFORE (bk)", "AFTER (live)"))
    print("-" * 60)
    for k in ("version", "bytes", "symbols_total", "symbols_top", "pins"):
        print("%-22s %-18s %-18s" % (k, a[k], b[k]))

    print()
    lost = [n for n in a["names"] if n not in b["names"]]
    added = [n for n in b["names"] if n not in a["names"]]
    print("symbols before: %d" % len(a["names"]))
    print("symbols after : %d" % len(b["names"]))
    if lost:
        print()
        print("LOST %d SYMBOL(S):" % len(lost))
        for n in lost:
            print("  - " + n)
    if added:
        print()
        print("ADDED %d SYMBOL(S):" % len(added))
        for n in added:
            print("  + " + n)

    print()
    problems = []
    if lost:
        problems.append("%d symbol(s) lost in upgrade" % len(lost))
    if b["pins"] < a["pins"]:
        problems.append("pin count dropped %d -> %d" % (a["pins"], b["pins"]))
    if b["symbols_top"] == 0:
        problems.append("no symbols at all in the live file")

    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  ! " + p)
        return 1
    print("symbol library intact - upgrade preserved every symbol and pin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
