"""DS claims the embedded lib_symbols lack KiCad 10 tokens.

Count, per sheet, the tokens KiCad 10 writes into embedded lib_symbols:
  (show_name ...)  (do_not_autoplace ...)  (in_pos_files ...)
  (exclude_from_sim ...)  (duplicate_pin_numbers_are_jumpers ...)
  old-style (effects ... hide)   vs   (hide yes)

If these are 0 in our sheets but KiCad's own output has hundreds, then every
sheet DOES get rewritten on load and my "unchanged by KiCad" result needs
re-examining.
"""
import glob
import io
import os
import re
import sys

LIVE = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1"
KICAD_WROTE = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124\sgc_force_probe"

TOKENS = {
    "show_name": re.compile(r'\(show_name '),
    "do_not_autoplace": re.compile(r'\(do_not_autoplace '),
    "in_pos_files": re.compile(r'\(in_pos_files '),
    "exclude_from_sim": re.compile(r'\(exclude_from_sim '),
    "dup_pin_jumpers": re.compile(r'\(duplicate_pin_numbers_are_jumpers '),
    "hide_yes": re.compile(r'\(hide yes\)'),
    "effects_hide_OLD": re.compile(r'\(effects[^\n]*\)\s*hide\s*\)'),
    "lib_symbols": re.compile(r'\(lib_symbols'),
}


def read(p):
    raw = io.open(p, "rb").read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def counts(p):
    t = read(p)
    return {k: len(rx.findall(t)) for k, rx in TOKENS.items()}


def main():
    hdr = "%-26s " % "file" + " ".join("%-10s" % k[:10] for k in TOKENS)
    print("=== LIVE JP-PC sheets ===")
    print(hdr)
    print("-" * len(hdr))
    live_tot = {k: 0 for k in TOKENS}
    for p in sorted(glob.glob(os.path.join(LIVE, "*.kicad_sch"))):
        c = counts(p)
        for k in TOKENS:
            live_tot[k] += c[k]
        print("%-26s " % os.path.basename(p)[:26] +
              " ".join("%-10d" % c[k] for k in TOKENS))
    print("-" * len(hdr))
    print("%-26s " % "TOTAL" + " ".join("%-10d" % live_tot[k] for k in TOKENS))

    k = os.path.join(KICAD_WROTE, "sgc_p1.kicad_sch")
    if os.path.isfile(k):
        print()
        print("=== KiCad 10's own rewrite of the ROOT ===")
        print(hdr)
        c = counts(k)
        print("%-26s " % "kicad_rewrite" +
              " ".join("%-10d" % c[x] for x in TOKENS))

    print()
    print("=== INTERPRETATION ===")
    missing = [t for t in ("show_name", "do_not_autoplace", "in_pos_files",
                           "exclude_from_sim", "dup_pin_jumpers")
               if live_tot[t] == 0]
    if missing:
        print("Tokens ABSENT from every live sheet: %s" % ", ".join(missing))
        print("If KiCad 10 adds these on load, that rewrite is the repair.")
    else:
        print("all KiCad 10 tokens present in the live sheets")
    if live_tot["effects_hide_OLD"]:
        print("Old-style (effects ... hide) present: %d occurrences"
              % live_tot["effects_hide_OLD"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
