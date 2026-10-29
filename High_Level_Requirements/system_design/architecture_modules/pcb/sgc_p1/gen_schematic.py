#!/usr/bin/env python3
"""
gen_schematic.py — builds sgc_p1.kicad_sch (KiCad 8, A3 single sheet) + sgc_p1.kicad_pro
from sgc_netlist.py. Run AFTER gen_symbols.py.

Regeneration source (run from this dir):  python3 gen_schematic.py

Style: every pin gets either
  - a short wire stub + global label (signal nets), or
  - an elbow wire + power symbol (#PWR, rail/GND nets), or
  - a no-connect flag ('NC').
Grouped by subsystem with text-box notes; all connectivity is via named global labels
and power symbols (no sprawling point-to-point wiring).
"""
import json
import os
import sys
import uuid

from kiutils.symbol import SymbolLib
from kiutils.schematic import Schematic
from kiutils.items.common import (Position, Property, Effects, Font, Justify, Stroke, Fill,
                                  TitleBlock, PageSettings)
from kiutils.items.schitems import (SchematicSymbol, GlobalLabel, NoConnect, Connection,
                                    TextBox, SymbolProjectInstance, SymbolProjectPath,
                                    HierarchicalSheetInstance)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sgc_netlist import SYMBOL_PINS, PARTS, NOTES, POWER_SYMBOLS, POWER_ANCHORS, PASSIVE_LEN

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = "sgc_p1"
LIB_NICK = "sgc_p1"
STUB = 5.08   # signal stub length (horizontal only -> no cross-pin collisions)
OFF = 2.54    # T/B-pin offset before vertical segment
F = lambda s=1.27: Font(height=s, width=s)


def prop(name, val, at, hide=False, just=None):
    ef = Effects(font=F(), justify=Justify(horizontally=just) if just else Justify())
    if hide:
        ef.hide = True
    return Property(name, val, position=Position(X=at[0], Y=at[1], angle=0), effects=ef)


def body_extents(sym_name):
    """(hw, top, bot) in symbol coords — mirrors gen_symbols.build_symbol layout."""
    spec = SYMBOL_PINS[sym_name]
    hw = spec["hw"]
    sides = [p for p in spec["pins"] if p[3] in "LR"]
    tb = [p for p in spec["pins"] if p[3] in "TB"]
    if tb:
        ln = PASSIVE_LEN.get(sym_name, 3.81)
        return hw, ln, -ln
    h2 = (max(sum(1 for p in sides if p[3] == "L"), sum(1 for p in sides if p[3] == "R")) - 1) / 2.0 * 2.54
    return hw, h2 + 2.54, -(h2 + 2.54)


def pin_sch_pos(part, pin_num, libsyms):
    """Schematic-space position of a pin's connection point (rotation 0 placement,
    KiCad lib->schematic Y-flip: y_sch = Y - y_lib)."""
    sym = libsyms[part["sym"]]
    unit = next(u for u in sym.units if u.unitId == 1)
    pin = next(p for p in unit.pins if p.number == str(pin_num))
    return Position(X=round(part["x"] + pin.position.X, 4),
                    Y=round(part["y"] - pin.position.Y, 4))


def main():
    lib = SymbolLib.from_file(os.path.join(HERE, "sgc_p1.kicad_sym"))
    libsyms = {s.entryName: s for s in lib.symbols if s.unitId is None}

    sch = Schematic(version="20231120", generator="sgc_gen", paper=PageSettings(paperSize="A3"))
    sch.uuid = str(uuid.uuid4())
    root_path = f"/{sch.uuid}"
    sch.titleBlock = TitleBlock(
        title="SGC P1 - ANNA-B402 carrier, 22x55mm 4L",
        date="2026-10-29", revision="A-WIP", company="Ski Gate Chrono",
        comments={1: "from sgc_schematic_spec.md v0.1 (92b352e)", 2: "generated - not ERC-reviewed"})
    sch.libSymbols = list(lib.symbols)

    wires, labels, ncs, symbols, boxes = [], [], [], [], []

    def wire(p1, p2):
        wires.append(Connection(points=[p1, p2], stroke=Stroke(width=0.0, type="default"),
                                uuid=str(uuid.uuid4())))

    def glabel(net, at, facing):  # facing: 'left' (wire arrives from right) / 'right'
        labels.append(GlobalLabel(
            text=net, shape="input", fieldsAutoplaced=True,
            position=Position(X=at[0], Y=at[1], angle=180 if facing == "left" else 0),
            effects=Effects(font=F(), justify=Justify(horizontally="right" if facing == "left" else "left")),
            uuid=str(uuid.uuid4())))

    pwr_n = 0
    def power_sym(net, at):
        nonlocal pwr_n
        pwr_n += 1
        ref = f"#PWR{pwr_n:02d}"
        s = SchematicSymbol(libraryNickname=LIB_NICK, entryName=net, position=Position(X=at[0], Y=at[1], angle=0),
                            unit=1, inBom=True, onBoard=True, dnp=False, uuid=str(uuid.uuid4()))
        s.properties = [prop("Reference", ref, (at[0], at[1]), hide=True),
                        prop("Value", net, (at[0], at[1] - 3.81)),
                        prop("Footprint", "", (at[0], at[1]), hide=True),
                        prop("Datasheet", "", (at[0], at[1]), hide=True),
                        prop("Description", "", (at[0], at[1]), hide=True)]
        s.instances = [SymbolProjectInstance(name=PROJECT,
                       paths=[SymbolProjectPath(sheetInstancePath=root_path, reference=ref, unit=1)])]
        symbols.append(s)

    # ---- component instances + per-pin routing ----
    # Style: EVERY pin gets a stub + global label (power nets included).
    #  - L/R pins: pure horizontal stub (no verticals -> geometrically collision-free)
    #  - T pins:   2.54 right, 2.54 up, label facing right
    #  - B pins:   2.54 left, 2.54 down, label facing left
    for part in PARTS:
        spec = SYMBOL_PINS[part["sym"]]
        hw, top, bot = body_extents(part["sym"])
        x, y = part["x"], part["y"]
        s = SchematicSymbol(libraryNickname=LIB_NICK, entryName=part["sym"], position=Position(X=x, Y=y, angle=0),
                            unit=1, inBom=True, onBoard=True, dnp=part.get("dnp", False),
                            uuid=str(uuid.uuid4()))
        s.properties = [
            prop("Reference", part["ref"], (x - hw - 2.0, y - top - 1.5)),
            prop("Value", part["value"], (x - hw, y - bot + 2.6)),
            prop("Footprint", part.get("fp", ""), (x, y), hide=True),
            prop("Datasheet", spec.get("ds", ""), (x, y), hide=True),
            prop("Description", spec.get("desc", ""), (x, y), hide=True),
        ]
        s.instances = [SymbolProjectInstance(name=PROJECT,
                       paths=[SymbolProjectPath(sheetInstancePath=root_path, reference=part["ref"], unit=1)])]
        symbols.append(s)

        side_of = {p[0]: p[3] for p in spec["pins"]}
        for pin_num, net in part["nets"].items():
            sd = side_of[pin_num]
            p = pin_sch_pos(part, pin_num, libsyms)
            if net == "NC":
                ncs.append(NoConnect(position=p, uuid=str(uuid.uuid4())))
                continue
            if sd == "L":
                wire(p, Position(p.X - STUB, p.Y)); glabel(net, (p.X - STUB, p.Y), "left")
            elif sd == "R":
                wire(p, Position(p.X + STUB, p.Y)); glabel(net, (p.X + STUB, p.Y), "right")
            elif sd == "T":
                wire(p, Position(p.X + OFF, p.Y))
                wire(Position(p.X + OFF, p.Y), Position(p.X + OFF, p.Y - OFF))
                glabel(net, (p.X + OFF, p.Y - OFF), "right")
            else:  # B
                wire(p, Position(p.X - OFF, p.Y))
                wire(Position(p.X - OFF, p.Y), Position(p.X - OFF, p.Y + OFF))
                glabel(net, (p.X - OFF, p.Y + OFF), "left")

    # ---- power-symbol zone anchors (symbol + 1 wire + matching label) ----
    for (net, ax, ay) in POWER_ANCHORS:
        power_sym(net, (ax, ay))
        if net == "GND":
            wire(Position(ax, ay), Position(ax, ay - OFF))
            glabel(net, (ax, ay - OFF), "left")
        else:
            wire(Position(ax, ay), Position(ax, ay + OFF))
            glabel(net, (ax, ay + OFF), "left")

    # ---- text boxes ----
    for (nx, ny, nw, nh, text) in NOTES:
        boxes.append(TextBox(text=text, position=Position(X=nx, Y=ny, angle=0),
                             size=Position(X=nw, Y=nh, angle=0),
                             stroke=Stroke(width=0.0, type="dash"), fill=Fill(type="none"),
                             effects=Effects(font=F(1.27), justify=Justify(horizontally="left", vertically="top")),
                             uuid=str(uuid.uuid4())))

    sch.schematicSymbols = symbols
    sch.graphicalItems = wires
    sch.globalLabels = labels
    sch.noConnects = ncs
    sch.textBoxes = boxes
    sch.sheetInstances = [HierarchicalSheetInstance(instancePath="/", page="1")]

    sch.to_file(os.path.join(HERE, "sgc_p1.kicad_sch"))

    # ---- minimal project file ----
    pro = {
        "board": {"design_settings": {}, "layer_presets": [], "viewports": []},
        "boards": [],
        "cvpcb": {"equivalence_files": []},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": "sgc_p1.kicad_pro", "version": 1},
        "net_settings": {"classes": [{"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25,
                                      "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
                                      "line_style": 0, "microvia_diameter": 0.3,
                                      "microvia_drill": 0.1, "name": "Default",
                                      "pcb_color": "rgba(0, 0, 0, 0.000)",
                                      "schematic_color": "rgba(0, 0, 0, 0.000)",
                                      "track_width": 0.2, "via_diameter": 0.6, "via_drill": 0.3,
                                      "wire_width": 6}],
                         "meta": {"version": 3}},
        "pcbnew": {"last_paths": {"gencad": "", "idf": "", "netlist": "", "plot": "",
                                  "pos_files": "", "specctra_dsn": "", "step": "", "svg": "",
                                  "vrml": ""},
                   "page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[sch.uuid, "Root"]],
        "text_variables": {},
    }
    with open(os.path.join(HERE, "sgc_p1.kicad_pro"), "w") as f:
        json.dump(pro, f, indent=2)

    print(f"wrote sgc_p1.kicad_sch: {len(symbols)} symbol instances "
          f"({len([p for p in PARTS])} components + {pwr_n} power), "
          f"{len(wires)} wires, {len(labels)} global labels, {len(ncs)} no-connects, "
          f"{len(boxes)} text boxes")


if __name__ == "__main__":
    main()
