"""Does regeneration PRESERVE the .kicad_pro, or clobber user settings?

The generator rewrote top_level_sheets to its own new root uuid, which proves it
writes .kicad_pro. The question that matters now: does it keep unrelated
settings (net classes, design rules, ERC/DRC config) that JP may have tuned in
the GUI?

If it clobbers them, that is a data-loss trap: edit the schematic in KiCad, tune
the project, then regenerate -> settings gone.
"""
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "pcb", "sgc_p1"))
GEN = "gen_schematic.py"
PRO = "sgc_p1.kicad_pro"


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def flat(obj, pre=""):
    """Flatten nested json to {path: value} for comparison."""
    out = {}
    if isinstance(obj, dict):
        for k, v in obj.items():
            out.update(flat(v, pre + "/" + k))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            out.update(flat(v, pre + "[%d]" % i))
    else:
        out[pre] = obj
    return out


def main():
    work = os.path.join(tempfile.gettempdir(), "sgc_prokeep")
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for f in os.listdir(SRC):
        if f.endswith((".py", ".kicad_sym", ".kicad_pro")) or \
           f.startswith(("sym-lib", "fp-lib")) or f.endswith(".txt"):
            shutil.copy2(os.path.join(SRC, f), os.path.join(work, f))

    before_p = os.path.join(work, PRO)
    before = json.loads(read(before_p))
    before_keys = set(before.keys())
    before_flat = flat(before)
    print("original .kicad_pro: %d top-level keys, %d leaves"
          % (len(before_keys), len(before_flat)))
    print("  keys: %s\n" % ", ".join(sorted(before_keys)))

    r = subprocess.run([sys.executable, GEN], cwd=work, capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    print("generator exit=%d" % r.returncode)
    if r.returncode != 0:
        return 1

    after = json.loads(read(before_p))
    after_flat = flat(after)
    after_keys = set(after.keys())
    print("regenerated .kicad_pro: %d top-level keys, %d leaves\n"
          % (len(after_keys), len(after_flat)))

    lost_keys = before_keys - after_keys
    gained = after_keys - before_keys
    if lost_keys:
        print("LOST top-level sections: %s" % ", ".join(sorted(lost_keys)))
    if gained:
        print("NEW top-level sections:  %s" % ", ".join(sorted(gained)))

    changed = []
    for k in before_flat:
        if k not in after_flat:
            changed.append((k, before_flat[k], "<deleted>"))
        elif after_flat[k] != before_flat[k]:
            changed.append((k, before_flat[k], after_flat[k]))
    print("\n%d leaf value(s) changed:\n" % len(changed))
    for k, a, b in changed[:25]:
        sa, sb = str(a)[:44], str(b)[:44]
        print("  %s" % k)
        print("      before: %s" % sa)
        print("      after : %s" % sb)
    if len(changed) > 25:
        print("  ... and %d more" % (len(changed) - 25))

    preserved = len(changed) == 0 and not lost_keys
    print()
    if preserved:
        print("PRESERVED: .kicad_pro untouched by regeneration.")
        return 0
    print("CLOBBER RISK: regeneration modifies .kicad_pro.")
    print("If JP tunes net classes / design rules / ERC settings in the GUI and")
    print("then runs gen_schematic.py, those edits are overwritten.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
