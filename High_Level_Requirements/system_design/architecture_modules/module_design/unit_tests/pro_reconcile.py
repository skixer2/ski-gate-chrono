"""Reconcile every UUID in sgc_p1.kicad_pro against the actual sheet files.

A project file that references a sheet UUID which no longer exists cannot bind
the hierarchy on load. kicad-cli never validates the .kicad_pro, so this class
of defect is invisible to every export/ERC/netlist check.

Reports:
  - top_level_sheets uuid vs the root .kicad_sch root uuid
  - every uuid mentioned in the pro, and whether any sheet file owns it
"""
import glob
import io
import json
import os
import re
import sys

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
PRO = os.path.join(D, "sgc_p1.kicad_pro")

UUID_RE = re.compile(r'\(uuid\s+"?([0-9a-fA-F-]{36})"?\)')


def sheet_uuids():
    """Every uuid declared in every sheet file, with its owner file."""
    owner = {}
    for p in sorted(glob.glob(os.path.join(D, "*.kicad_sch"))):
        raw = io.open(p, "rb").read()
        try:
            t = raw.decode("utf-8")
        except UnicodeDecodeError:
            t = raw.decode("cp1252", "replace")
        for u in UUID_RE.findall(t):
            owner.setdefault(u.lower(), []).append(os.path.basename(p))
    return owner


def main():
    if not os.path.isfile(PRO):
        print("no .kicad_pro at %s" % PRO)
        print("SKIP: schematic not checked out on this host")
        return 0
    raw = io.open(PRO, "rb").read()
    pro_text = raw.decode("utf-8-sig")
    j = json.loads(pro_text)
    owner = sheet_uuids()

    # the true root uuid = first uuid in the root sheet
    root_path = os.path.join(D, "sgc_p1.kicad_sch")
    rt = io.open(root_path, "rb").read().decode("utf-8", "replace")
    m = UUID_RE.search(rt)
    true_root = m.group(1).lower() if m else None

    print("true root uuid (from sgc_p1.kicad_sch) : %s" % true_root)
    print()

    tls = j.get("schematic", {}).get("top_level_sheets", [])
    bad = 0
    print("=== top_level_sheets ===")
    for e in tls:
        u = str(e.get("uuid", "")).lower()
        ok = (u == true_root)
        print("  filename=%s name=%s" % (e.get("filename"), e.get("name")))
        print("  uuid=%s  %s" % (u, "OK" if ok else "MISMATCH <- breaks the bind"))
        if not ok:
            bad += 1
    print()

    print("=== every uuid referenced in the .kicad_pro ===")
    pro_uuids = sorted(set(x.lower() for x in UUID_RE.findall(pro_text)))
    if not pro_uuids:
        print("  (none found by s-expr pattern; scanning JSON strings)")
        pro_uuids = sorted(set(
            x.lower() for x in re.findall(
                r'"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-'
                r'[0-9a-fA-F]{4}-[0-9a-fA-F]{12})"', pro_text)))
    for u in pro_uuids:
        holders = owner.get(u)
        if holders:
            print("  %s  OK      owned by %s" % (u, ",".join(sorted(set(holders)))))
        else:
            print("  %s  ORPHAN  no sheet file declares this")
            bad += 1

    print()
    # also: the 'sheets' array must agree with the actual hierarchy
    arr = j.get("sheets", [])
    print("=== sheets array (%d entries) ===" % len(arr))
    for entry in arr:
        if isinstance(entry, list) and len(entry) == 2:
            u = str(entry[0]).lower()
            holders = owner.get(u)
            mark = "OK" if holders else "ORPHAN"
            print("  %s  %-9s %s" % (u, mark, entry[1]))
            if not holders:
                bad += 1

    print()
    if bad:
        print("RECONCILE NEEDED: %d inconsistency(ies)" % bad)
        return 1
    print("PROJECT FILE CONSISTENT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
