"""Inspect the accidental live edit to sgc_netlist.py before deciding anything.

My harvest_roundtrip.py invoked harvest_placement.py from a sandbox, but that
tool resolves paths from __file__ (the live repo), so it wrote 18 x=/y= changes
into the LIVE sgc_netlist.py. A timestamped backup was made first.

Show exactly what changed so we can decide: keep or restore.
"""
import io
import os
import re
import subprocess

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
REL = "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sgc_netlist.py"
BAK = REL + ".bak_20261124_221042"


def show(revargs, label):
    r = subprocess.run(["git", "-C", REPO, "diff"] + revargs, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    print("=" * 70)
    print(label)
    print("=" * 70)
    print(r.stdout[:2500])
    print()


def main():
    show(["-U0", "--", REL], "DIFF HEAD -> working tree (live file)")

    # compare backup vs current to see what the tool changed
    p_bak = os.path.join(REPO, BAK.replace("/", "\\"))
    p_cur = os.path.join(REPO, REL.replace("/", "\\"))
    if not os.path.isfile(p_bak):
        print("no backup found at %s" % p_bak)
        return 1
    a = io.open(p_bak, encoding="utf-8", errors="replace").read().splitlines()
    b = io.open(p_cur, encoding="utf-8", errors="replace").read().splitlines()
    import difflib
    d = list(difflib.unified_diff(a, b, "backup", "current", lineterm="", n=0))
    print("=" * 70)
    print("BACKUP vs CURRENT  (%d diff lines)" % len(d))
    print("=" * 70)
    for ln in d[:60]:
        print(ln)
    nchg = sum(1 for ln in d if ln.startswith(("+", "-")) and not ln.startswith(("+++", "---")))
    print("\n%d changed line(s) total" % nchg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
