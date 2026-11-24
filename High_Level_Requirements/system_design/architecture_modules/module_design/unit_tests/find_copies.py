"""Find every copy of the sgc_p1 schematic tree on this machine.

If JP opens a different copy than the one I fixed, the error persists and my
"it's clean" proof is about a file he never looks at. Search wide, report
paths + mtimes + version stamps.
"""
import glob
import io
import os
import re

ROOTS = [
    r"C:\Users\v17ni\Documents",
    r"C:\Users\v17ni\zio_claw",
    r"C:\sgc",
    r"C:\Users\v17ni\Desktop",
    r"C:\Users\v17ni\Downloads",
]

VER = re.compile(r'\(version (\d{8})\)')
BARE = re.compile(r'\(uuid\s+([^"\s()][^\s()]*)')


def mtime(p):
    import datetime
    return datetime.datetime.fromtimestamp(os.path.getmtime(p)).strftime(
        "%Y-%m-%d %H:%M")


def main():
    seen = {}
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for pat in ("sgc_p1.kicad_sch", "sgc_p1.kicad_pro", "*.kicad_sch"):
            for p in glob.glob(os.path.join(root, "**", pat), recursive=True):
                if "\\.history" in p or "\\build_trash" in p:
                    continue
                seen[os.path.abspath(p)] = True

    print("found %d schematic-related files\n" % len(seen))
    roots = {}
    for p in sorted(seen):
        d = os.path.dirname(p)
        roots.setdefault(d, []).append(p)

    for d, files in sorted(roots.items()):
        schs = [f for f in files if f.endswith(".kicad_sch")]
        pro = [f for f in files if f.endswith(".kicad_pro")]
        print("DIR  %s" % d)
        print("     %d sch, %d pro" % (len(schs), len(pro)))
        for f in pro:
            print("     PRO  %s  mtime=%s" % (os.path.basename(f), mtime(f)))
        # version + bare uuid state of the ROOT sheet in this dir
        root_sch = os.path.join(d, "sgc_p1.kicad_sch")
        if os.path.isfile(root_sch):
            raw = io.open(root_sch, "rb").read()
            try:
                t = raw.decode("utf-8")
            except UnicodeDecodeError:
                t = raw.decode("cp1252", "replace")
            m = VER.search(t)
            nb = len(BARE.findall(t))
            print("     ROOT %s  ver=%s  bare_uuids=%d  mtime=%s" % (
                os.path.basename(root_sch),
                m.group(1) if m else "?",
                nb, mtime(root_sch)))
        print()

    # flag any dir that has a pro older than its sheets
    print("=" * 60)
    for d in sorted(roots):
        schs = sorted(glob.glob(os.path.join(d, "*.kicad_sch")))
        pros = sorted(glob.glob(os.path.join(d, "*.kicad_pro")))
        if not schs or not pros:
            continue
        newest_sch = max(os.path.getmtime(s) for s in schs)
        oldest_pro = min(os.path.getmtime(p) for p in pros)
        if oldest_pro < newest_sch - 60:
            print("MISMATCH %s" % d)
            print("   .kicad_pro is OLDER than the newest sheet")
            print("   pro : %s" % mtime(pros[0]))
            print("   sch : %s" % mtime(max(schs, key=os.path.getmtime)))


if __name__ == "__main__":
    main()
