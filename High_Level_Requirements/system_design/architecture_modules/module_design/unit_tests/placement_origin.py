"""Where did the 18 divergent positions come from?

Question: are the positions in the live sheets (a) JP's GUI edits, or (b) just
what gen_schematic.py wrote before? sgc_netlist.py has C1 at (152,42) but the
live sheet says (199.64,57.66). Someone moved it.

Candidates:
  - JP dragged it in the KiCad GUI
  - an older version of sgc_netlist.py had different coords and the table was
    edited back / diverged
  - the generator applied an offset

Check the sheet position across git history: if the position was ALREADY
(199.64,57.66) in the last commit that gen_schematic.py wrote, then it came from
the generator (table changed since). If it only appeared after JP's repair
commit 6a3918f6, it is a KiCad-side change.
"""
import io
import os
import re
import subprocess
import sys

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
REL = "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_power.kicad_sch"
REFS = ["C1", "C6", "R1", "U3"]


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def pos_of(text, ref):
    """Symbol's own (at) for the symbol whose Reference property == ref."""
    # locate the property, then walk back to the enclosing (symbol
    idx = text.find('(property "Reference" "%s"' % ref)
    if idx < 0:
        return None
    start = text.rfind("(symbol", 0, idx)
    block = text[start:idx]
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', block)
    return (float(m.group(1)), float(m.group(2))) if m else None


def show(rev, label):
    r = subprocess.run(["git", "-C", REPO, "show", "%s:%s" % (rev, REL)],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if r.returncode != 0:
        print("  %-28s <not present>" % label)
        return {}
    t = r.stdout
    got = {}
    for ref in REFS:
        p = pos_of(t, ref)
        if p:
            got[ref] = p
    print("  %-28s %s" % (label, "  ".join("%s(%g,%g)" % (k, v[0], v[1])
                                           for k, v in sorted(got.items()))))
    return got


def main():
    log = subprocess.run(["git", "-C", REPO, "log", "--format=%h %s", "-8",
                          "--", REL], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    print("%s - recent history:\n" % os.path.basename(REL))
    for ln in log.stdout.strip().splitlines():
        print("  " + ln)
    print()

    revs = [ln.split()[0] for ln in log.stdout.strip().splitlines() if ln.strip()]
    if not revs:
        print("no history")
        return 2

    print("positions of %s across revisions:\n" % ", ".join(REFS))
    prev = None
    for rev in revs[:6]:
        subj = subprocess.run(["git", "-C", REPO, "log", "-1", "--format=%s",
                               rev], capture_output=True, text=True,
                              encoding="utf-8", errors="replace").stdout.strip()
        cur = show(rev, "%s %s" % (rev, subj[:22]))
        if prev is not None and cur and prev:
            diffs = [k for k in cur if k in prev and cur[k] != prev[k]]
            if diffs:
                print("      ^ changed vs next-newer: %s" % ", ".join(diffs))
        prev = cur or prev
    return 0


if __name__ == "__main__":
    sys.exit(main())
