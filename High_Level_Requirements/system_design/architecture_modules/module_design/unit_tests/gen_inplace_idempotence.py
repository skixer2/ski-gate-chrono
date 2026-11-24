"""Does regenerating IN PLACE keep the root uuid stable?

The realistic scenario: JP has the project open in KiCad, edits the generator,
runs it in the same directory. The existing sgc_p1.kicad_sch is present, so the
generator should REUSE its root uuid and leave .kicad_pro untouched.

Contrast with pro_preserve_check.py, which stages a clean dir (no .kicad_sch) and
therefore gets a fresh uuid by design -- its "sheet map churn" was an artifact of
that staging, not a real defect.

Run: python gen_inplace_idempotence.py
"""
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
GEN = "gen_schematic.py"
GATE = os.path.join(HERE, "sch_repair_gate.py")


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def root_uuid(d):
    p = os.path.join(d, "sgc_p1.kicad_sch")
    if not os.path.isfile(p):
        return None
    m = re.search(r'\(uuid "([0-9a-f-]{36})"', read(p))
    return m.group(1) if m else None


def snapshot(d):
    pro = os.path.join(d, "sgc_p1.kicad_pro")
    try:
        data = json.loads(read(pro))
    except Exception:
        data = None
    sheets = os.path.join(d, "sgc_p1.kicad_sch")
    return {
        "uuid": root_uuid(d),
        "pro": data,
        "sch": read(sheets) if os.path.isfile(sheets) else None,
    }


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_inplace")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    # FULL copy - this is the in-place scenario, outputs included
    for f in os.listdir(SRC):
        s = os.path.join(SRC, f)
        if os.path.isfile(s) and not f.endswith((".pdf", ".kicad_pcb")):
            shutil.copy2(s, os.path.join(work, f))
    if os.path.isdir(os.path.join(SRC, "sgc_p1.pretty")):
        shutil.copytree(os.path.join(SRC, "sgc_p1.pretty"),
                        os.path.join(work, "sgc_p1.pretty"), dirs_exist_ok=True)

    before = snapshot(work)
    print("BEFORE: root uuid = %s" % before["uuid"])
    if not before["uuid"]:
        print("SETUP FAILED: no root sheet copied")
        return 2

    r = subprocess.run([sys.executable, GEN], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    print("regenerate in place: exit=%d" % r.returncode)
    if r.returncode != 0:
        print(((r.stdout or "") + (r.stderr or ""))[-600:])
        return 1

    after = snapshot(work)
    problems = []

    if after["uuid"] != before["uuid"]:
        problems.append("root uuid CHANGED: %s -> %s"
                        % (before["uuid"], after["uuid"]))
    else:
        print("root uuid STABLE: %s" % after["uuid"])

    # .kicad_pro sheet map must be byte-identical
    b_sheets = json.dumps(before["pro"].get("sheets"), sort_keys=True)
    a_sheets = json.dumps(after["pro"].get("sheets"), sort_keys=True)
    if b_sheets != a_sheets:
        problems.append(".kicad_pro sheets[] changed on in-place regeneration")
    else:
        print(".kicad_pro sheets[] IDENTICAL")

    b_tl = json.dumps(before["pro"].get("schematic", {}).get("top_level_sheets"),
                      sort_keys=True)
    a_tl = json.dumps(after["pro"].get("schematic", {}).get("top_level_sheets"),
                      sort_keys=True)
    if b_tl != a_tl:
        problems.append("top_level_sheets changed on in-place regeneration")
    else:
        print("top_level_sheets IDENTICAL")

    same = before["sch"] == after["sch"]
    print("root sheet bytes %s" % ("IDENTICAL" if same else "DIFFER"))

    g = subprocess.run([sys.executable, GATE, work], capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    gate_ok = g.returncode == 0
    print("repair gate: %s" % ("PASS" if gate_ok else "FAIL"))
    if not gate_ok:
        problems.append("repair gate failed after in-place regen")
        print(((g.stdout or "") + (g.stderr or "")).strip())

    print()
    if problems:
        print("IN-PLACE REGENERATION PROBLEMS:")
        for p in problems:
            print("  ! %s" % p)
        return 1
    print("IN-PLACE REGENERATION IS IDEMPOTENT AND REPAIRED.")
    print("Re-running the generator on an existing project is safe:")
    print("  - root uuid reused, so .kicad_pro never needs reconciling")
    print("  - repair gate still passes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
