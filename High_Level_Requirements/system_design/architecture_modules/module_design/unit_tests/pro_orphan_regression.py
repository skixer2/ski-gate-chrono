"""Does regenerating the schematic orphan the .kicad_pro again?

gen_schematic.py generates a RANDOM root uuid each run. The .kicad_pro pins
top_level_sheets to a specific uuid. So regenerating the schematic -- while
keeping the existing .kicad_pro -- should leave top_level_sheets pointing at a
uuid that no longer exists in any sheet.

That is the orphan bug fixed by hand earlier (4921ceaf -> 9578d648). If the
generator does not reconcile .kicad_pro, it comes straight back on the next
regeneration.

Run: python pro_orphan_regression.py
"""
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
GEN = "gen_schematic.py"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def collect_uuids(d):
    """Every sheet uuid present in the tree."""
    uu = set()
    for f in os.listdir(d):
        if f.endswith(".kicad_sch"):
            uu |= set(re.findall(r'\(uuid "([0-9a-f-]{36})"',
                                 read(os.path.join(d, f))))
    return uu


def top_level(d):
    p = os.path.join(d, "sgc_p1.kicad_pro")
    if not os.path.isfile(p):
        return None
    try:
        pro = json.loads(read(p))
    except Exception as e:
        return "unparseable: %s" % e
    s = pro.get("sheets")
    return s[0] if isinstance(s, list) and s else None


def main():
    live_uuids = collect_uuids(SRC)
    live_tl = top_level(SRC)
    print("LIVE tree")
    print("  top_level_sheets: %s" % live_tl)
    print("  resolves to a real sheet: %s\n"
          % ("YES" if live_tl in live_uuids else "NO - ORPHAN"))

    work = os.path.join(tempfile.gettempdir(), "sgc_proreg")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        if f.endswith((".py", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")) or f.endswith(".txt"):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))

    r = subprocess.run([sys.executable, GEN], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    print("regenerated tree: gen exit=%d" % r.returncode)
    if r.returncode != 0:
        print("generator failed, cannot assess")
        return 1

    new_tl = top_level(work)
    new_uuids = collect_uuids(work)
    print("  top_level_sheets: %s" % new_tl)
    ok = new_tl in new_uuids
    print("  resolves to a real sheet: %s\n" % ("YES" if ok else "NO - ORPHAN"))

    if ok:
        print("PRO RECONCILE OK: generator keeps .kicad_pro consistent.")
        return 0

    print("ORPHAN REINTRODUCED BY REGENERATION.")
    print("  .kicad_pro points at %s" % new_tl)
    print("  sheets actually contain: %s" % sorted(new_uuids))
    print()
    print("gen_schematic.py must write/reconcile top_level_sheets itself,")
    print("otherwise every regeneration re-breaks project consistency")
    print("(the smoke 'project file consistency' check will fail).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
