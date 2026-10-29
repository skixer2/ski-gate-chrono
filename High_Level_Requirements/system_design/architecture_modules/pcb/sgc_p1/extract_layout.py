#!/usr/bin/env python3
"""extract_layout.py — read JP's hand-edited .kicad_sch positions back into
the generator data-truth (stdlib only; run on the PC where the edited file lives).

Writes layout_snapshot.json: per-symbol refdes -> (x, y, angle), plus
text_box and label anchor positions. Parent absorbs this into sgc_netlist.py
so regenerations preserve JP's decluttering."""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.join(HERE, "sgc_p1.kicad_sch")

src = open(SCH, encoding="utf-8").read()
out = {"symbols": {}, "text_boxes": [], "labels": {}}

# placed symbol instances: (symbol (lib_id "...") (at X Y A) ...
for m in re.finditer(r'\(symbol \(lib_id "([^"]+)"\)[^()]{0,80}?\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', src):
    lib_id, x, y = m.group(1), float(m.group(2)), float(m.group(3))
    a = float(m.group(4)) if m.group(4) else 0.0
    # find the Reference property value following this instance (search forward, bounded)
    tail = src[m.end():m.end() + 2000]
    rm = re.search(r'\(property "Reference" "([^"]+)"', tail)
    ref = rm.group(1) if rm else lib_id
    out["symbols"][ref] = {"lib_id": lib_id, "x": x, "y": y, "angle": a}

# text boxes: (text_box "..." (at X Y A)
for m in re.finditer(r'\(text_box "([^"]{0,40})[\s\S]{0,600}?\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', src):
    out["text_boxes"].append({"text_head": m.group(1)[:40], "x": float(m.group(2)),
                              "y": float(m.group(3)), "angle": float(m.group(4))})

# global labels: (global_label "NET" ... (at X Y A)
for m in re.finditer(r'\(global_label "([^"]+)"[^()]*\(shape \w+\)\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', src):
    net = m.group(1)
    out["labels"].setdefault(net, []).append(
        {"x": float(m.group(2)), "y": float(m.group(3)), "angle": float(m.group(4))})

dst = os.path.join(HERE, "layout_snapshot.json")
json.dump(out, open(dst, "w"), indent=1)
print("symbols:", len(out["symbols"]), "| text_boxes:", len(out["text_boxes"]),
      "| label nets:", len(out["labels"]), "->", dst)
