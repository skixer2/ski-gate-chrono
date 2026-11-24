"""Root sheet_instances must contain ONLY the root page in KiCad 10.

HISTORY / WHY THIS CHECK WAS REWRITTEN TWICE
--------------------------------------------
2026-10-xx commit d8932503 "root sheet_instances must enumerate every child
page" added 7 entries because a loader complained. That was correct for the
KiCad 8 format.

On 2026-11-24 I verified the opposite for KiCad 10: feed a 7-entry block to
`kicad-cli sch upgrade` and it collapses to exactly one entry,

    (sheet_instances
      (path "/"
        (page "1")
      )
    )

Child page numbers are NOT stored here in the current format. So a 7-entry
block is a legacy shape that KiCad normalises on load - i.e. something to
repair. This check now enforces the KiCad 10 shape.

I had the wrong rule in the file for a day because I trusted an old commit
message over a fresh measurement. Measure, do not remember.
"""
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
    root = read(os.path.join(D, "sgc_p1.kicad_sch"))
    m = re.search(r'\(sheet_instances(.*?)\n\t\)', root, re.S)
    if not m:
        print("FAIL: no sheet_instances block")
        return 1
    body = m.group(1)
    paths = re.findall(r'\(path "([^"]*)"', body)
    pages = re.findall(r'\(page "([^"]*)"', body)

    boxes = re.findall(r'\(sheet\b.*?\(uuid "([0-9a-f-]{36})"', root, re.S)

    print("child sheets: %d" % len(boxes))
    print("sheet_instances: %d path(s) %s  page(s) %s" % (len(paths), paths,
                                                          pages))
    print()

    if paths == ["/"] and pages == ["1"]:
        print("OK: KiCad 10 shape - single root page, nothing to normalise")
        return 0

    print("FAIL: expected exactly (path \"/\" (page \"1\"))")
    if len(paths) > 1:
        print("  %d child page paths present - this is the LEGACY shape." %
              (len(paths) - 1))
        print("  KiCad 10 strips them on load. Emit only the root entry.")
    print("  actual block:")
    for ln in body.rstrip().splitlines():
        print("    " + ln.strip())
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
