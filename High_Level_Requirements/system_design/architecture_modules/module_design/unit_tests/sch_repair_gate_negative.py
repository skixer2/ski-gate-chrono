"""NEGATIVE TEST for sch_repair_gate.py -- prove the gate can fail.

Two defects injected independently, because there are three repair classes:
  A. strip the root uuid from child instance paths   (the original dialog cause)
  B. remove the sheet-box (instances ...) blocks
  C. remove (embedded_fonts ...) from the root

A gate that passes any of these is not a gate.
"""
import glob
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
GATE = os.path.join(HERE, "sch_repair_gate.py")
PY = sys.executable


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def write(p, t):
    io.open(p, "w", encoding="utf-8", newline="").write(t)


def stage(tag):
    d = os.path.join(tempfile.gettempdir(), "sgc_gate_" + tag)
    if os.path.isdir(d):
        shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for f in os.listdir(SRC):
        if f.endswith((".kicad_sch", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")):
            shutil.copy2(os.path.join(SRC, f), os.path.join(d, f))
    return d


def run_gate(d):
    r = subprocess.run([PY, GATE, d], capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    return r.returncode, ((r.stdout or "") + (r.stderr or ""))


def drop_instances(text):
    """Remove every (instances ...) block from a sheet, paren-balanced.

    The old regex version removed nothing (the file is CRLF + tabs) and the
    negative test silently reported a false 'gate blind'. Do it by counting
    parentheses so nesting cannot defeat it.
    """
    out = []
    i = 0
    removed = 0
    while True:
        j = text.find("(instances", i)
        if j < 0:
            out.append(text[i:])
            break
        out.append(text[i:j])
        depth = 0
        k = j
        while k < len(text):
            if text[k] == "(":
                depth += 1
            elif text[k] == ")":
                depth -= 1
                if depth == 0:
                    k += 1
                    break
            k += 1
        removed += 1
        # swallow trailing whitespace/CRLF so we do not leave blank lines
        while k < len(text) and text[k] in " \t\r\n":
            k += 1
        i = k
    return "".join(out), removed


def main():
    failures = 0

    # ---- baseline: the real tree must PASS ----
    rc, out = run_gate(SRC)
    print("=" * 60)
    print("BASELINE (real tree) -> exit %d" % rc)
    print("=" * 60)
    print(out.strip())
    if rc != 0:
        print("!! baseline FAILS - the tree really is broken, or gate is wrong")
        failures += 1
    print()

    # ---- A: strip root uuid from child instance paths ----
    d = stage("a")
    root = read(os.path.join(d, "sgc_p1.kicad_sch"))
    root_uuid = re.search(r'\(uuid "([0-9a-f-]{36})"', root).group(1)
    n = 0
    for p in glob.glob(os.path.join(d, "sch_*.kicad_sch")):
        t = read(p)
        n += t.count('"/%s/' % root_uuid)
        write(p, t.replace('"/%s/' % root_uuid, '"/'))
    rc, out = run_gate(d)
    print("=" * 60)
    print("DEFECT A: stripped root uuid from %d child paths -> exit %d" % (n, rc))
    print("=" * 60)
    print(out.strip())
    if rc == 0:
        print("!! GATE BLIND to defect A")
        failures += 1
    print()

    # ---- B: remove sheet-box instances blocks ----
    d = stage("b")
    root = read(os.path.join(d, "sgc_p1.kicad_sch"))
    new, nrem = drop_instances(root)
    write(os.path.join(d, "sgc_p1.kicad_sch"), new)
    rc, out = run_gate(d)
    print("=" * 60)
    print("DEFECT B: removed %d sheet-box instances block(s) -> exit %d"
          % (nrem, rc))
    print("=" * 60)
    print(out.strip())
    if nrem == 0:
        print("!! INJECTION FAILED: nothing removed, test proves nothing")
        failures += 1
    elif rc == 0:
        print("!! GATE BLIND to defect B")
        failures += 1
    print()

    # ---- C: remove embedded_fonts ----
    d = stage("c")
    root = read(os.path.join(d, "sgc_p1.kicad_sch"))
    new = re.sub(r'\n?\s*\(embedded_fonts \w+\)', '', root)
    write(os.path.join(d, "sgc_p1.kicad_sch"), new)
    rc, out = run_gate(d)
    print("=" * 60)
    print("DEFECT C: removed embedded_fonts -> exit %d" % rc)
    print("=" * 60)
    print(out.strip())
    if rc == 0:
        print("!! GATE BLIND to defect C")
        failures += 1
    print()

    print("=" * 60)
    if failures:
        print("NEGATIVE TEST FAILED: %d case(s) not caught" % failures)
        return 1
    print("NEGATIVE TEST PASSED: gate catches all three defect classes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
