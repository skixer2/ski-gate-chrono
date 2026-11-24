"""Does KiCad 10 keep child page paths in sheet_instances, or strip them?

Two data points disagree:
  - gen_schematic writes 7 entries (root + 6 children)
  - the live file on disk has only 1 entry ("/")

If KiCad 10 itself writes only "/", then a 7-entry block is the OLD format and
my generator is emitting something KiCad must repair. If KiCad 10 keeps 7, then
something stripped them and I need to find out what.

This asks KiCad directly: feed it the 7-entry version, have it rewrite, and see
what it keeps.
"""
import io
import os
import re
import shutil
import subprocess
import tempfile

SRC = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def block(text):
    m = re.search(r'\(sheet_instances(.*?)\n\t\)', text, re.S)
    return m.group(1) if m else "(none)"


def main():
    live = read(os.path.join(SRC, "sgc_p1.kicad_sch"))
    print("=== LIVE root sheet_instances (%d entries) ===" %
          len(re.findall(r'\(path ', block(live))))
    print(block(live).rstrip())
    print()

    boxes = re.findall(r'\(sheet\b.*?\(uuid "([0-9a-f-]{36})"', live, re.S)
    print("child boxes: %d" % len(boxes))

    # build a 7-entry version and let KiCad rewrite it
    work = os.path.join(tempfile.gettempdir(), "sgc_si_probe")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        if f.endswith((".kicad_sch", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))

    entries = ['\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)']
    for i, b in enumerate(boxes):
        entries.append('\n\t\t(path "/%s"\n\t\t\t(page "%d")\n\t\t)' % (b, i + 2))
    new_block = "(sheet_instances" + "".join(entries) + "\n\t)"
    patched = re.sub(r'\(sheet_instances.*?\n\t\)', new_block, live, count=1,
                     flags=re.S)
    root = os.path.join(work, "sgc_p1.kicad_sch")
    io.open(root, "w", encoding="utf-8", newline="").write(patched)
    print("wrote a 7-entry sheet_instances into the copy")
    print("  entries before rewrite: %d" %
          len(re.findall(r'\(path ', block(patched))))
    print()

    # force a rewrite by downgrading the version stamp
    t = read(root)
    t = re.sub(r'\(version \d{8}\)', '(version 20231120)', t, count=1)
    io.open(root, "w", encoding="utf-8", newline="").write(t)

    r = subprocess.run([CLI, "sch", "upgrade", root], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    msg = ((r.stdout or "") + (r.stderr or "")).strip()
    print("kicad-cli exit=%d  %s" % (r.returncode, msg[:120]))
    print()

    after = read(root)
    ab = block(after)
    n = len(re.findall(r'\(path ', ab))
    print("=== AFTER KiCad 10 rewrite: %d entries ===" % n)
    print(ab.rstrip())
    print()
    if n == 1:
        print("VERDICT: KiCad 10 keeps ONLY '/' - child page paths are not")
        print("stored in sheet_instances. A 7-entry block is legacy shape and")
        print("is a likely repair trigger. Generator should emit 1 entry.")
        return 1
    if n == 7:
        print("VERDICT: KiCad 10 preserves all 7 - the live file lost them")
        print("somehow and the generator's 7-entry form is correct.")
        return 0
    print("VERDICT: KiCad kept %d entries - inspect above." % n)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
