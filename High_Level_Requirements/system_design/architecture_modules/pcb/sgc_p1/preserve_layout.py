#!/usr/bin/env python3
# preserve_layout.py - keep JP's manual symbol placements across generator rebuilds.
#
# Workflow (see README):
#   python3 preserve_layout.py preserve   # BEFORE regen: harvest current positions -> layout_positions.json
#   python3 gen_symbols.py
#   python3 gen_schematic.py
#   python3 preserve_layout.py apply      # AFTER regen: re-apply saved positions onto fresh files
#   python3 validate_sgc_p1.py
#
# What is preserved per (sheet, ref): position (x, y), angle, mirror flags.
# What is preserved at root level: sheet box (at + size) per Sheetname.
# New refs added in the netlist keep the generator's default position.
# Refs removed from the netlist keep their saved entry (harmless, ignored).
# PCB placement is NOT affected by any of this - it comes from PLACEMENT_PLAN.md / placement_check.py.
import json, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
JSON_PATH = os.path.join(HERE, "layout_positions.json")
SHEETS = ["sch_power.kicad_sch", "sch_mcu.kicad_sch", "sch_sensors.kicad_sch",
          "sch_storage.kicad_sch", "sch_boost_led.kicad_sch", "sch_connectors.kicad_sch"]
ROOT = "sgc_p1.kicad_sch"

def first_field(s, tag):
    i = s.find("(" + tag + " ")
    if i < 0:
        return None
    return s[i + len(tag) + 2:s.find(")", i)].strip()

def harvest():
    data = {"symbols": {}, "sheets": {}}
    for fname in SHEETS:
        path = os.path.join(HERE, fname)
        if not os.path.exists(path):
            continue
        sheet = fname.replace("sch_", "").replace(".kicad_sch", "")
        t = open(path, encoding="utf-8").read()
        # skip lib_symbols definitions: instances all come after the last (lib_symbols) close;
        # robust discriminator: an instance chunk contains a Reference property whose value has a digit
        for chunk in t.split("(symbol")[1:]:
            head = chunk[:(chunk.find("(property") if chunk.find("(property") > 0 else 400)]
            at = first_field(head, "at")
            if at is None:
                continue
            ri = chunk.find('"Reference" "')
            if ri < 0:
                continue
            ref = chunk[ri + 13:chunk.find(chr(34), ri + 13)]
            if not any(ch.isdigit() for ch in ref):
                continue  # lib definition placeholder (e.g. REF**), skip
            mx = "(mirror x" in head
            my = "(mirror y" in head
            data["symbols"][sheet + ":" + ref] = {"at": at, "mx": mx, "my": my}
    rt = open(os.path.join(HERE, ROOT), encoding="utf-8").read()
    for chunk in rt.split("(sheet")[1:]:
        at = first_field(chunk, "at")
        size = first_field(chunk, "size")
        ni = chunk.find('"Sheetname" "')
        if at and size and ni >= 0:
            nm = chunk[ni + 13:chunk.find(chr(34), ni + 13)]
            data["sheets"][nm] = {"at": at, "size": size}
    json.dump(data, open(JSON_PATH, "w", encoding="utf-8"), indent=1, sort_keys=True)
    print("preserved:", len(data["symbols"]), "symbols,", len(data["sheets"]), "sheet boxes ->", os.path.basename(JSON_PATH))

def apply():
    data = json.load(open(JSON_PATH, encoding="utf-8"))
    fixed = 0
    for fname in SHEETS:
        path = os.path.join(HERE, fname)
        if not os.path.exists(path):
            continue
        sheet = fname.replace("sch_", "").replace(".kicad_sch", "")
        t = open(path, encoding="utf-8").read()
        parts = t.split("(symbol")
        changed = False
        for i in range(1, len(parts)):
            c = parts[i]
            head_end = c.find("(property")
            head = c[:head_end if head_end > 0 else 400]
            ri = c.find('"Reference" "')
            if ri < 0:
                continue
            ref = c[ri + 13:c.find(chr(34), ri + 13)]
            if not any(ch.isdigit() for ch in ref):
                continue
            saved = data["symbols"].get(sheet + ":" + ref)
            if not saved:
                continue
            at = first_field(head, "at")
            if at is None or at == saved["at"]:
                continue
            ai = head.find("(at ")
            end = head.find(")", ai)
            parts[i] = head[:ai] + "(at " + saved["at"] + ")" + head[end + 1:]
            changed = True
            fixed += 1
        if changed:
            open(path, "w", encoding="utf-8").write("(symbol".join(parts))
    rt = open(os.path.join(HERE, ROOT), encoding="utf-8").read()
    parts = rt.split("(sheet")
    changed = False
    for i in range(1, len(parts)):
        c = parts[i]
        ni = c.find('"Sheetname" "')
        if ni < 0:
            continue
        nm = c[ni + 13:c.find(chr(34), ni + 13)]
        saved = data["sheets"].get(nm)
        if not saved:
            continue
        at = first_field(c, "at")
        size = first_field(c, "size")
        if (at != saved["at"]) or (size != saved["size"]):
            ai = c.find("(at ")
            end = c.find(")", ai)
            c2 = c[:ai] + "(at " + saved["at"] + ")" + c[end + 1:]
            si = c2.find("(size ")
            if si >= 0:
                end2 = c2.find(")", si)
                c2 = c2[:si] + "(size " + saved["size"] + ")" + c2[end2 + 1:]
            parts[i] = c2
            changed = True
            fixed += 1
    if changed:
        open(os.path.join(HERE, ROOT), "w", encoding="utf-8").write("(sheet".join(parts))
    print("applied:", fixed, "restored placements")

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "preserve":
        harvest()
    elif mode == "apply":
        apply()
    else:
        print("usage: preserve_layout.py preserve|apply")
        sys.exit(2)
