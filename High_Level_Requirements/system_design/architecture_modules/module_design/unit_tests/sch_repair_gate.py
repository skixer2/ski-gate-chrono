"""Direct assertion gate for the schematic repair (replaces the blind fixpoint test).

WHY THIS EXISTS
---------------
kicad_fixpoint.py was proven BLIND by fixpoint_negative_test.py: strip the root
uuid from all 101 child instance paths and the "fixed point" test still says ALL
STABLE. kicad-cli normalises instance paths silently during parse, so any
parse-rewrite comparison is structurally incapable of seeing this defect class.

The dialog is caused by three concrete things KiCad adds. So assert them
directly, by reading the files:

  1. every child symbol instance path starts with /<root-uuid>/
  2. each sheet box carries an (instances (project "..." (path "/<root>" (page N))))
  3. the root sheet has (embedded_fonts ...)

This is the gate that can actually fail. Proven by--repair-negative on a copy.
"""
import glob
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def check(root_dir):
    """Return (ok, [(severity, message)], stats)."""
    problems = []
    stats = {}

    rootp = os.path.join(root_dir, "sgc_p1.kicad_sch")
    if not os.path.isfile(rootp):
        return False, [("FAIL", "sgc_p1.kicad_sch missing")], {}
    root = read(rootp)

    m = re.search(r'\(uuid "([0-9a-f-]{36})"', root)
    if not m:
        return False, [("FAIL", "root sheet has no uuid")], {}
    root_uuid = m.group(1)
    stats["root_uuid"] = root_uuid

    # --- 1. child symbol instance paths ---
    total_good = 0
    total_bad = 0
    for p in sorted(glob.glob(os.path.join(root_dir, "sch_*.kicad_sch"))):
        t = read(p)
        name = os.path.basename(p)
        paths = re.findall(r'\(path "([^"]*)"', t)
        for x in paths:
            if x == "/":
                continue          # root sheet_instances entry, legitimate
            if x.startswith("/" + root_uuid + "/"):
                total_good += 1
            else:
                total_bad += 1
                if total_bad <= 5:
                    problems.append(
                        ("FAIL", "%s: instance path missing root uuid: %s"
                         % (name, x)))
    stats["paths_good"] = total_good
    stats["paths_bad"] = total_bad
    if total_good == 0 and total_bad == 0:
        problems.append(("FAIL", "no child symbol instance paths found at all"))

    # --- 2. sheet box instances blocks ---
    # NOTE: the file is CRLF + tab-indented, so match on "(sheet" followed by
    # WHITESPACE. That excludes (sheet_instances, which has "_" after "sheet".
    # Counting with "(sheet\n" scored 0 and silently disabled the check below.
    n_boxes = len(re.findall(r'\(sheet\s', root))
    n_proj = root.count('(project "')
    pages = re.findall(r'\(page "([^"]*)"\)', root)
    stats["sheet_boxes"] = n_boxes
    stats["instances_projects"] = n_proj
    stats["pages"] = sorted(set(pages), key=lambda x: int(x) if x.isdigit() else 999)
    if n_boxes == 0:
        problems.append(("FAIL", "root sheet has no (sheet) boxes at all"))
    elif n_proj < n_boxes:
        problems.append(
            ("FAIL", "only %d sheet-box instances block(s) for %d box(es)"
             % (n_proj, n_boxes)))
    if len(pages) < n_boxes + 1:
        problems.append(
            ("FAIL", "%d page number(s) for %d box(es) + root"
             % (len(pages), n_boxes)))
    elif len(set(pages)) != len(pages):
        problems.append(("FAIL", "duplicate page numbers: %s" % pages))

    # --- 3. embedded_fonts on root ---
    has_ef = "embedded_fonts" in root
    stats["embedded_fonts"] = has_ef
    if not has_ef:
        problems.append(("FAIL", "root sheet has no embedded_fonts token"))

    ok = not any(s == "FAIL" for s, _ in problems)
    return ok, problems, stats


def main():
    root_dir = (sys.argv[1] if len(sys.argv) > 1
                else os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1")))
    ok, problems, stats = check(root_dir)
    print("schematic repair gate: %s" % root_dir)
    print("  root uuid:          %s" % stats.get("root_uuid", "?"))
    print("  instance paths:     %d good, %d bad"
          % (stats.get("paths_good", 0), stats.get("paths_bad", 0)))
    print("  sheet boxes:        %s" % stats.get("sheet_boxes", "?"))
    print("  instances blocks:   %s" % stats.get("instances_projects", "?"))
    print("  pages:              %s" % (stats.get("pages") or "none"))
    print("  embedded_fonts:     %s" % stats.get("embedded_fonts", "?"))
    print()
    if ok:
        print("SCHEMATIC REPAIR OK - all three KiCad fixes present.")
        return 0
    for sev, msg in problems:
        print("  ! %s" % msg)
    print("\nSCHEMATIC NEEDS REPAIR - KiCad will show the dialog.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
