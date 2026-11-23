"""Compare two KiCad netlists, ignoring the timestamp header.

Run on JP-PC:
    python tools/netdiff_check.py before.net after.net
Exit 0 = identical content (component/net lists match exactly).
"""
import sys
import re
import difflib


def load(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    # strip the generator/timestamp header: everything before the components
    # block, plus any line carrying a date stamp.
    out = []
    for ln in lines:
        if re.search(r"\((date|source|generator|tool)\b", ln):
            continue
        out.append(ln.rstrip())
    return out


def main():
    if len(sys.argv) != 3:
        print("usage: netdiff_check.py BEFORE AFTER")
        return 2
    a = load(sys.argv[1])
    b = load(sys.argv[2])
    print("before lines:", len(a))
    print("after  lines:", len(b))
    if a == b:
        print("IDENTICAL (excluding date/generator header)")
        return 0
    diff = list(difflib.unified_diff(a, b, "before", "after", n=1, lineterm=""))
    print("DIFFERENCES:", len(diff), "diff lines")
    for ln in diff[:60]:
        print("  " + ln)
    return 1


if __name__ == "__main__":
    sys.exit(main())
