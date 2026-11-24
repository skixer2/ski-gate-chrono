"""What actually changed in sch_power.kicad_sch?

git status shows it modified (380+/390-) yet HEAD blob has C1 at (152,42) and the
working tree has C1 at (199.64,57.66). Categorise the diff: are these symbol
position moves, net/label changes, or pure formatting?
"""
import io
import os
import re
import subprocess
import sys

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
REL = "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_power.kicad_sch"


def main():
    d = subprocess.run(["git", "-C", REPO, "diff", "-U0", "HEAD", "--", REL],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace").stdout
    rem = [l[1:] for l in d.splitlines() if l.startswith("-") and not l.startswith("---")]
    add = [l[1:] for l in d.splitlines() if l.startswith("+") and not l.startswith("+++")]
    print("removed %d line(s), added %d line(s)\n" % (len(rem), len(add)))

    def cls(lines):
        c = {}
        for l in lines:
            s = l.strip()
            if not s:
                key = "<blank>"
            elif re.match(r'^\(at [-\d.]+ [-\d.]+', s):
                key = "(at ...)"
            elif s.startswith("(uuid"):
                key = "(uuid)"
            elif "Reference" in s:
                key = "Reference prop"
            elif "Value" in s:
                key = "Value prop"
            elif s.startswith(("(pin", "(stroke", "(fill", "(effects", "(font",
                             "(justify", "(size", "(width", "(type", "(color")):
                key = "geometry/effects"
            elif s.startswith(("(label", "(global_label", "(hierarchical_label",
                             "(wire", "(junction", "(no_connect", "(text")):
                key = "connectivity"
            else:
                key = s.split()[0] if s.split() else "?"
            c[key] = c.get(key, 0) + 1
        return c

    ra, aa = cls(rem), cls(add)
    keys = sorted(set(ra) | set(aa))
    print("%-24s %8s %8s" % ("category", "removed", "added"))
    for k in keys:
        print("%-24s %8d %8d" % (k, ra.get(k, 0), aa.get(k, 0)))

    # the money question: do (at ...) coordinates change?
    def ats(lines):
        out = []
        for l in lines:
            m = re.match(r'^\s*\(at ([-\d.]+) ([-\d.]+)', l)
            if m:
                out.append((float(m.group(1)), float(m.group(2))))
        return out
    r_at, a_at = ats(rem), ats(add)
    print("\n(at ...) tokens: %d removed, %d added" % (len(r_at), len(a_at)))
    if r_at and a_at and len(r_at) == len(a_at):
        deltas = {}
        for (x1, y1), (x2, y2) in zip(r_at, a_at):
            k = (round(x2 - x1, 2), round(y2 - y1, 2))
            deltas[k] = deltas.get(k, 0) + 1
        print("coordinate deltas (new - old):")
        for (dx, dy), n in sorted(deltas.items(), key=lambda kv: -kv[1])[:10]:
            print("   dx=%+8.2f dy=%+8.2f   x%d" % (dx, dy, n))
    else:
        for i in range(min(8, len(r_at), len(a_at))):
            print("   %s -> %s" % (r_at[i], a_at[i]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
