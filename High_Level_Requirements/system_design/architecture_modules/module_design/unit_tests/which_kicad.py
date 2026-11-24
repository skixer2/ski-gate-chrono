"""Which KiCad is actually installed, and what opens .kicad_sch?

I have been testing with C:\\Program Files\\KiCad\\10.0\\bin\\kicad-cli.exe and
assuming JP uses the same build. If he has another install (9.x, or a second
10.x), every "clean" result I produced describes a binary he never runs.
"""
import glob
import os
import re
import winreg


def reg_query(root, path):
    try:
        k = winreg.OpenKey(root, path)
        items = []
        i = 0
        while True:
            try:
                items.append(winreg.EnumValue(k, i))
                i += 1
            except OSError:
                break
        return items
    except OSError:
        return []


def reg_subkeys(root, path):
    try:
        k = winreg.OpenKey(root, path)
        out = []
        i = 0
        while True:
            try:
                out.append(winreg.EnumKey(k, i))
                i += 1
            except OSError:
                break
        return out
    except OSError:
        return []


def main():
    print("=== KiCad install directories ===")
    for base in (r"C:\Program Files", r"C:\Program Files (x86)",
                 r"C:\Users\v17ni\AppData\Local\Programs"):
        p = os.path.join(base, "KiCad")
        if os.path.isdir(p):
            for v in sorted(os.listdir(p)):
                full = os.path.join(p, v, "bin")
                exes = glob.glob(os.path.join(full, "kicad*.exe"))
                print("  %s  (%d exe)" % (os.path.join(p, v), len(exes)))
                for e in exes:
                    print("      %s" % os.path.basename(e))
        else:
            print("  (none) %s" % p)

    print()
    print("=== every kicad_cli / kicad exe on disk (Program Files) ===")
    hits = []
    for pat in ("kicad.exe", "kicad-cli.exe"):
        hits += glob.glob(r"C:\Program Files\**\\" + pat, recursive=True)
        hits += glob.glob(r"C:\Program Files (x86)\**\\" + pat, recursive=True)
    for h in sorted(set(hits)):
        print("  " + h)

    print()
    print("=== registry: KiCad product entries ===")
    for root, label in ((winreg.HKEY_LOCAL_MACHINE, "HKLM"),
                        (winreg.HKEY_CURRENT_USER, "HKCU")):
        for path in (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
                     r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"):
            for sub in reg_subkeys(root, path):
                if "kicad" in sub.lower():
                    vals = dict((n, v) for n, v, _ in
                                reg_query(root, path + "\\" + sub))
                    print("  [%s] %s" % (label, sub))
                    for k in ("DisplayName", "DisplayVersion", "InstallLocation"):
                        if k in vals:
                            print("      %s = %s" % (k, vals[k]))

    print()
    print("=== file association .kicad_sch / .kicad_pro ===")
    for ext in (".kicad_sch", ".kicad_pro"):
        vals = dict((n, v) for n, v, _ in reg_query(
            winreg.HKEY_CLASSES_ROOT, ext))
        if vals:
            print("  %s -> %s" % (ext, vals))
        else:
            print("  %s -> (no association; opens via app picker)" % ext)

    print()
    print("=== KiCad user config (which version last ran) ===")
    cfg = r"C:\Users\v17ni\AppData\Roaming\kicad"
    if os.path.isdir(cfg):
        for v in sorted(os.listdir(cfg)):
            print("  config dir: %s" % v)
        # the common settings file records the version
        for f in glob.glob(os.path.join(cfg, "*", "kicad_common.json")):
            try:
                import json
                j = json.load(open(f, encoding="utf-8"))
                print("  %s -> %s" % (os.path.relpath(f, cfg),
                                      json.dumps(j.get("meta", {}))[:200]))
            except Exception as e:
                print("  %s unreadable: %s" % (f, e))
    else:
        print("  no AppData\\Roaming\\kicad")


if __name__ == "__main__":
    main()
