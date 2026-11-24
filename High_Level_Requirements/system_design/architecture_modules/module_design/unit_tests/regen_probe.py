"""Regenerate into a scratch dir with the FIXED generator, then ask KiCad 10
whether it would change anything. If KiCad's own rewrite is a no-op, the
generator output is storable in current format and needs no repair.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import tempfile

SRC = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
PY = r"C:\Users\v17ni\AppData\Local\Programs\Python\Python313\python.exe"

VOL = re.compile(r'^\s*\((version|date|generator|generator_version)\b.*$')


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def norm(t):
    return [l.strip() for l in t.splitlines()
            if l.strip() and not VOL.match(l)]


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_regen_probe")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    # copy the generator + its inputs so it can run standalone
    for f in os.listdir(SRC):
        if f.endswith((".py", ".kicad_sym", ".txt", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))
    if os.path.isdir(os.path.join(SRC, "sgc_p1.pretty")):
        shutil.copytree(os.path.join(SRC, "sgc_p1.pretty"),
                        os.path.join(work, "sgc_p1.pretty"), dirs_exist_ok=True)

    print("running the FIXED generator in %s\n" % work)
    r = subprocess.run([PY, "gen_schematic.py"], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    out = ((r.stdout or "") + (r.stderr or "")).strip()
    print("exit=%d" % r.returncode)
    for ln in out.splitlines()[-8:]:
        print("  | " + ln)
    if r.returncode != 0:
        print("\ngenerator failed - cannot verify")
        return 2

    gen = {}
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        gen[os.path.basename(p)] = read(p)
    print("\ngenerated %d sheets\n" % len(gen))

    # what did it write for sheet_instances?
    m = re.search(r'\(sheet_instances(.*?)\n\t\)', gen["sgc_p1.kicad_sch"], re.S)
    n = len(re.findall(r'\(path ', m.group(1))) if m else 0
    print("generated root sheet_instances entries: %d" % n)

    # now force KiCad to parse+rewrite everything and diff
    for p in glob.glob(os.path.join(work, "*.kicad_sch")):
        t = read(p)
        t = re.sub(r'\(version \d{8}\)', '(version 20231120)', t, count=1)
        io.open(p, "w", encoding="utf-8", newline="").write(t)

    root = os.path.join(work, "sgc_p1.kicad_sch")
    r = subprocess.run([CLI, "sch", "upgrade", root], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    msg = ((r.stdout or "") + (r.stderr or "")).strip()
    print("kicad-cli rewrite: exit=%d %s" % (r.returncode, msg[:100]))
    if "not updated" in msg.lower():
        print("inconclusive: no rewrite")
        return 2

    print()
    bad = 0
    for name, before in sorted(gen.items()):
        after = read(os.path.join(work, name))
        a, b = norm(before), norm(after)
        if a == b:
            print("  %-28s KiCad changed nothing" % name)
            continue
        bad += 1
        sa, sb = set(a), set(b)
        only_a = [x for x in a if x not in sb]
        only_b = [x for x in b if x not in sa]
        print("  %-28s CHANGED (%d -> %d)" % (name, len(a), len(b)))
        for x in only_a[:5]:
            print("      generator : " + x[:120])
        for x in only_b[:5]:
            print("      kicad     : " + x[:120])

    print()
    if bad:
        print("%d sheet(s) still differ -> generator still emits something KiCad"
              % bad)
        print("rewrites. The lines above are what to change in the generator.")
        return 1
    print("GENERATOR OUTPUT IS STABLE: KiCad 10 rewrites none of it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
