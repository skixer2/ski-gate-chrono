"""Check the root sheet_instances block against the real page count.

A 7-sheet hierarchy must enumerate every page path in the root's
(sheet_instances ...) block:

    (sheet_instances
      (path "/" (page "1"))
      (path "/<box-uuid-1>" (page "2"))
      ... 7 total
    )

If only "/" is present, the child pages have no page numbers and KiCad has to
assign/repair them on load. An earlier commit (d8932503) fixed exactly this and
it may have regressed when the sheets were regenerated.
"""
import glob
import io
import os
import re

D = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def main():
    root_p = os.path.join(D, "sgc_p1.kicad_sch")
    root = read(root_p)

    # all child sheet boxes declared in the root
    boxes = re.findall(r'\(sheet\b.*?\(uuid "([0-9a-f-]{36})"', root, re.S)
    print("child sheet boxes in root: %d" % len(boxes))
    for b in boxes:
        print("  %s" % b)

    # the sheet_instances block
    m = re.search(r'\(sheet_instances(.*?)\n\t\)', root, re.S)
    print()
    if not m:
        print("NO sheet_instances BLOCK  <-- KiCad must rebuild page numbers")
        return 1
    body = m.group(1)
    print("=== sheet_instances body ===")
    print(body.rstrip())
    print()

    paths = re.findall(r'\(path "([^"]*)"', body)
    pages = re.findall(r'\(page "([^"]*)"', body)
    print("entries: %d   paths: %d   pages: %d" % (len(paths), len(paths),
                                                   len(pages)))
    print()

    expected = len(boxes) + 1   # children + root
    problems = []
    if len(paths) < expected:
        problems.append("expected %d page paths (root + %d children), found %d"
                        % (expected, len(boxes), len(paths)))
    missing = [b for b in boxes
               if ('"/%s"' % b) not in body and ("/%s" % b) not in body]
    if missing:
        problems.append("%d child box(es) missing from sheet_instances: %s"
                        % (len(missing), ", ".join(m[:8] for m in missing)))
    if pages != sorted(pages) and pages:
        pass

    if problems:
        print("DEFECTS:")
        for p in problems:
            print("  ! " + p)
        print()
        print("A child page with no (path \"/<uuid>\" (page \"N\")) entry has no")
        print("page number. KiCad assigns one on load and asks to save = repair.")
        return 1

    print("sheet_instances enumerates every page - OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
