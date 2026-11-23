#!/usr/bin/env python3
"""
gen_schematic.py - builds the SGC P1 SEVEN-SHEET hierarchy (JP GO 2026-11-04,
plan in N1_PARITY_PLAN.md section 4): hierarchy-only root sgc_p1.kicad_sch +
six child sheets from SHEETS in sgc_netlist.py. Run AFTER gen_symbols.py.

Regeneration source (run from this dir):  python3 gen_schematic.py

Style: every pin gets either
  - a short wire stub + global label (signal nets), or
  - an elbow wire + power symbol (#PWR, rail/GND nets), or
  - a no-connect flag ('NC').
All connectivity is via named global labels + power symbols, so sheets bridge
by name - no hierarchical pins needed (same recipe the 2-sheet split proved).

UUID policy: child sheet files and their sheet boxes get DETERMINISTIC uuid5
ids (stable across regenerations). The root reuses the uuid of the existing
sgc_p1.kicad_sch (the .kicad_pro sheet map and any external refs stay valid).
"""
import io
import json
import os
import re
import sys
import tempfile
import uuid

from kiutils.symbol import SymbolLib
from kiutils.schematic import Schematic, HierarchicalSheet, HierarchicalSheetInstance, HierarchicalSheetProjectInstance, HierarchicalSheetProjectPath
from kiutils.items.common import (Position, Property, Effects, Font, Justify, Stroke, Fill, ColorRGBA,
                                  TitleBlock, PageSettings)
from kiutils.items.schitems import (SchematicSymbol, GlobalLabel, NoConnect, Connection,
                                    TextBox, SymbolProjectInstance, SymbolProjectPath)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
STD_NICK = {"R": "Device", "C": "Device", "L": "Device", "GND": "power"}
from sgc_netlist import SYMBOL_PINS, PARTS, NOTES, POWER_SYMBOLS, SHEETS, PASSIVE_LEN

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = "sgc_p1"
LIB_NICK = "sgc_p1"
STUB = 5.08   # signal stub length (horizontal only -> no cross-pin collisions)
OFF = 2.54    # T/B-pin offset before vertical segment
F = lambda s=1.27: Font(height=s, width=s)
NS = uuid.uuid5(uuid.NAMESPACE_DNS, "sgc-p1-sheets")   # deterministic sheet ids


def det_uuid(name):
    return str(uuid.uuid5(NS, name))


def esc_text(s):
    """KiCad s-expr string escape: raw newlines inside quotes are illegal
    (KiCad 10: 'Unterminated delimited string'). Escape to literal \\n."""
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")

def prop(name, val, at, hide=False, just=None):
    ef = Effects(font=F(), justify=Justify(horizontally=just) if just else Justify())
    if hide:
        ef.hide = True
    return Property(name, val, position=Position(X=at[0], Y=at[1], angle=0), effects=ef)


def body_extents(sym_name):
    """(hw, top, bot) in symbol coords - mirrors gen_symbols.build_symbol layout."""
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


class SheetCtx:
    """Per-sheet emission context (lists + the per-pin styling rules)."""

    # Power-symbol numbering is PROJECT-wide, not per sheet: KiCad requires every
    # reference to be unique across the whole hierarchy. A per-sheet counter made
    # each of the six sheets restart at #PWR01, giving 21 duplicate references and
    # the "annotation errors" dialog on load.
    _pwr_seq = 0

    def __init__(self):
        self.wires, self.labels, self.ncs, self.symbols, self.boxes = [], [], [], [], []
        self.pwr_n = 0

    def wire(self, p1, p2):
        self.wires.append(Connection(points=[p1, p2], stroke=Stroke(width=0.0, type="default"),
                                     uuid=str(uuid.uuid4())))

    def glabel(self, net, at, facing):
        self.labels.append(GlobalLabel(
            text=net, shape="input", fieldsAutoplaced=True,
            position=Position(X=at[0], Y=at[1], angle=180 if facing == "left" else 0),
            effects=Effects(font=F(), justify=Justify(horizontally="right" if facing == "left" else "left")),
            uuid=str(uuid.uuid4())))

    def power_sym(self, net, at, root_path):
        self.pwr_n += 1
        SheetCtx._pwr_seq += 1
        ref = f"#PWR{SheetCtx._pwr_seq:02d}"
        s = SchematicSymbol(libraryNickname=STD_NICK.get(net, LIB_NICK), entryName=net, position=Position(X=at[0], Y=at[1], angle=0),
                            unit=1, inBom=True, onBoard=True, dnp=False, uuid=str(uuid.uuid4()))
        s.properties = [prop("Reference", ref, (at[0], at[1]), hide=True),
                        prop("Value", net, (at[0], at[1] - 3.81)),
                        prop("Footprint", "", (at[0], at[1]), hide=True),
                        prop("Datasheet", "", (at[0], at[1]), hide=True),
                        prop("Description", "", (at[0], at[1]), hide=True)]
        # Instance path MUST be the root sheet uuid, like every other symbol. A bare "/"
        # makes KiCad report "annotation errors" and silently rewrite the file on load,
        # which is why the schematic kept asking to be repaired after every regeneration.
        s.instances = [SymbolProjectInstance(name=PROJECT,
                       paths=[SymbolProjectPath(sheetInstancePath=root_path, reference=ref, unit=1)])]
        self.symbols.append(s)

    def note(self, nx, ny, nw, nh, text):
        self.boxes.append(TextBox(text=esc_text(text), position=Position(X=nx, Y=ny, angle=0),
                             size=Position(X=nw, Y=nh, angle=0),
                             stroke=Stroke(width=0.0, type="dash"), fill=Fill(type="none"),
                             effects=Effects(font=F(1.27), justify=Justify(horizontally="left", vertically="top")),
                             uuid=str(uuid.uuid4())))

    def emit_part(self, part, libsyms, root_path):
        spec = SYMBOL_PINS[part["sym"]]
        hw, top, bot = body_extents(part["sym"])
        x, y = part["x"], part["y"]
        s = SchematicSymbol(libraryNickname=STD_NICK.get(part["sym"], LIB_NICK), entryName=part["sym"], position=Position(X=x, Y=y, angle=0),
                            unit=1, inBom=True, onBoard=True, dnp=part.get("dnp", False),
                            uuid=str(uuid.uuid4()))
        s.properties = [
            prop("Reference", part["ref"], (x - hw - 2.0, y - top - 1.5)),
            prop("Value", part["value"], (x - hw, y - bot + 2.6)),
            # A part without an explicit fp inherits the symbol spec's footprint;
            # fp=E (empty string) still explicitly blanks it. Previously a part that
            # simply omitted fp also got "", which silently dropped footprints the
            # symbol already declared (TP_TX/TP_RX lost TestPoint_Pad_D1.0mm).
            prop("Footprint", part["fp"] if "fp" in part else spec.get("fp", ""), (x, y), hide=True),
            prop("Datasheet", spec.get("ds", ""), (x, y), hide=True),
            prop("Description", spec.get("desc", ""), (x, y), hide=True),
        ]
        s.instances = [SymbolProjectInstance(name=PROJECT,
                       paths=[SymbolProjectPath(sheetInstancePath=root_path, reference=part["ref"], unit=1)])]
        self.symbols.append(s)

        side_of = {p[0]: p[3] for p in spec["pins"]}
        for pin_num, net in part["nets"].items():
            sd = side_of[pin_num]
            p = pin_sch_pos(part, pin_num, libsyms)
            if net == "NC":
                self.ncs.append(NoConnect(position=p, uuid=str(uuid.uuid4())))
                continue
            if sd == "L":
                self.wire(p, Position(p.X - STUB, p.Y)); self.glabel(net, (p.X - STUB, p.Y), "left")
            elif sd == "R":
                self.wire(p, Position(p.X + STUB, p.Y)); self.glabel(net, (p.X + STUB, p.Y), "right")
            elif sd == "T":
                self.wire(p, Position(p.X + OFF, p.Y))
                self.wire(Position(p.X + OFF, p.Y), Position(p.X + OFF, p.Y - OFF))
                self.glabel(net, (p.X + OFF, p.Y - OFF), "right")
            else:  # B
                self.wire(p, Position(p.X - OFF, p.Y))
                self.wire(Position(p.X - OFF, p.Y), Position(p.X - OFF, p.Y + OFF))
                self.glabel(net, (p.X - OFF, p.Y + OFF), "left")

    def emit_power_anchors(self, nets, root_path, x_off=0.0):
        """One anchor per power net used on this sheet, left margin column."""
        rails = [n for n in nets if n != "GND"]
        y = 25.0
        for n in rails:
            self.power_sym(n, (18.0 + x_off, y), root_path)
            self.wire(Position(18.0 + x_off, y), Position(18.0 + x_off, y + OFF))
            self.glabel(n, (18.0 + x_off, y + OFF), "left")
            y += 10.16
        y = 25.0
        if "GND" in nets:
            self.power_sym("GND", (8.0 + x_off, y), root_path)
            self.wire(Position(8.0 + x_off, y), Position(8.0 + x_off, y - OFF))
            self.glabel("GND", (8.0 + x_off, y - OFF), "left")


def _quote_uuids(path):
    """kiutils emits `(uuid <bare>)`; KiCad 10 wants `(uuid "<quoted>")`.

    A bare token is parsed as a SYMBOL rather than a string, which trips the
    parser's type check and recovers. That recovery is exactly the GUI dialog
    'An error was found when loading the schematic that has been automatically
    fixed'. Until kiutils quotes uuids itself, fix the file after writing.
    """
    with io.open(path, encoding="utf-8") as fh:
        text = fh.read()
    fixed = re.sub(r'\(uuid\s+([0-9a-fA-F][0-9a-fA-F-]*)\s*\)',
                   r'(uuid "\1")', text)
    if fixed != text:
        with io.open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(fixed)
    return len(fixed) - len(text)


def make_sheet_file(title, page_no, ctx, libsyms, uuid_str):
    sch = Schematic(version="20260306", generator="sgc_gen", paper=PageSettings(paperSize="A3"))
    sch.uuid = uuid_str
    sch.titleBlock = TitleBlock(
        title=title, date="2026-11-04", revision="A-WIP", company="Ski Gate Chrono",
        comments={1: "N1 7-page split - from sgc_schematic_spec.md v0.1 + N1_PARITY_PLAN.md",
                  2: "generated - not ERC-reviewed"})
    sch.libSymbols = list(libsyms)
    sch.schematicSymbols = ctx.symbols
    sch.graphicalItems = ctx.wires
    sch.globalLabels = ctx.labels
    sch.noConnects = ctx.ncs
    sch.textBoxes = ctx.boxes
    sch.sheetInstances = [HierarchicalSheetInstance(instancePath="/", page=str(page_no))]
    return sch


def main():
    lib = SymbolLib.from_file(os.path.join(HERE, "sgc_p1.kicad_sym"))
    libsyms = {s.entryName: s for s in lib.symbols if s.unitId is None}
    # merge KiCad-standard defs (Device:R/C/L, power:GND) for pin geometry
    _b = open(os.path.join(HERE, "std_symbols.txt"), encoding="utf-8").read()
    # Portable temp path: /tmp does not exist on Windows, and this generator runs on both.
    _tmp_lib = os.path.join(tempfile.gettempdir(), "_std_lib.kicad_sym")
    open(_tmp_lib, "w").write(
        '(kicad_symbol_lib (version 20231120) (generator kicad_symbol_editor)' + chr(10) + _b + chr(10) + ')' + chr(10))
    _stdlib = SymbolLib.from_file(_tmp_lib)
    libsyms.update({s.entryName: s for s in _stdlib.symbols if s.unitId is None})
    all_libsymbols = list(lib.symbols) + list(_stdlib.symbols)

    # ---- partition check: every part on exactly one sheet ----
    by_ref = {p["ref"]: p for p in PARTS}
    seen = set()
    for _, _, refs in SHEETS:
        for r in refs:
            assert r in by_ref, f"SHEETS lists unknown ref {r}"
            assert r not in seen, f"SHEETS lists {r} twice"
            seen.add(r)
    missing = set(by_ref) - seen
    assert not missing, f"parts missing from SHEETS: {sorted(missing)}"

    # ---- root uuid reuse (stable regeneration) ----
    pro_path = os.path.join(HERE, "sgc_p1.kicad_pro")
    sch_path = os.path.join(HERE, "sgc_p1.kicad_sch")
    root_uuid = None
    if os.path.exists(sch_path):
        try:
            _head = open(sch_path, encoding="utf-8").read(800)
            _m = re.search(r'\(uuid "?([0-9a-f-]{36})"?\)', _head)
            if _m:
                root_uuid = _m.group(1)
        except Exception as _e:
            print(f"WARN: could not reuse root sheet UUID ({_e})")
    if root_uuid is None:
        root_uuid = str(uuid.uuid4())
    # KiCad's hierarchy path convention: the ROOT sheet is "/", and a child sheet
    # is "/<that-sheet's-box-uuid>". The root's own uuid is NOT a component of the
    # path. Including it makes every instance path unresolvable, which is what the
    # loader reported as "annotation errors" and silently repaired.
    root_path = "/"

    # ---- notes -> sheet of the nearest part ----
    notes_by_file = {fn: [] for _, fn, _ in SHEETS}
    for (nx, ny, nw, nh, text) in NOTES:
        best, bd = None, float("inf")
        for _, fn, refs in SHEETS:
            for r in refs:
                p = by_ref[r]
                d = abs(p["x"] - nx) + abs(p["y"] - ny)
                if d < bd:
                    bd, best = d, fn
        notes_by_file[best].append((nx, ny, nw, nh, text))

    # ---- emit child sheets ----
    total_syms = total_pwr = total_w = total_l = total_nc = 0
    sheet_boxes = []
    bx, by = 30.0, 40.0
    for i, (title, fname, refs) in enumerate(SHEETS):
        ctx = SheetCtx()
        # Symbols living on a CHILD sheet must carry the full path
        # /<root-uuid>/<this-sheet-box-uuid>. Using the bare root path makes every
        # sheet's references collide (six sheets each claiming #PWR01, U1, C1...) and
        # KiCad reports "annotation errors" on load. Deterministic box uuid, same one
        # the root sheet box uses, so the two always agree.
        sheet_path = root_path + det_uuid(fname + ":box")
        for r in refs:
            ctx.emit_part(by_ref[r], libsyms, sheet_path)
        nets = set()
        for r in refs:
            nets.update(v for v in by_ref[r]["nets"].values() if v != "NC" and v in POWER_SYMBOLS)
        ctx.emit_power_anchors(nets, sheet_path, x_off=12.0 * i)
        for (nx, ny, nw, nh, text) in notes_by_file[fname]:
            ctx.note(nx, ny, nw, nh, text)
        s_uuid = det_uuid(fname)
        sch = make_sheet_file(f"SGC P1 - {title}", i + 2, ctx, all_libsymbols, s_uuid)
        _out = os.path.join(HERE, fname)
        sch.to_file(_out)
        _quote_uuids(_out)
        total_syms += len(ctx.symbols); total_pwr += ctx.pwr_n
        total_w += len(ctx.wires); total_l += len(ctx.labels); total_nc += len(ctx.ncs)
        print(f"  {fname}: {len(refs)} parts, {ctx.pwr_n} power, {len(ctx.labels)} labels, {len(ctx.boxes)} notes")
        # root sheet box (deterministic uuid -> stable .kicad_pro map)
        box = HierarchicalSheet(
            position=Position(X=bx, Y=by, angle=0),
            width=70.0, height=30.0,
            stroke=Stroke(width=0.0, type="solid"), fill=ColorRGBA(),
            uuid=det_uuid(fname + ":box"),
            sheetName=prop("Sheetname", title.split(":")[0].strip(), (bx + 3.0, by + 3.0)),
            fileName=prop("Sheetfile", fname, (bx + 3.0, by + 12.0)),
            properties=[],
            pins=[],
            # No `instances` block: KiCad 10 derives each sheet box's instance from the
            # root `sheet_instances` list and STRIPS a per-box instances block on load.
            # Emitting one is what produced the "annotation errors ... automatically
            # fixed" dialog after every regeneration. Confirmed against
            # `kicad-cli sch upgrade`, which deletes it.
            instances=[])
        sheet_boxes.append(box)
        bx += 85.0
        if bx > 330.0:
            bx, by = 30.0, by + 45.0

    # ---- root: hierarchy only (Arduino ABX00050 style) ----
    root = Schematic(version="20260306", generator="sgc_gen", paper=PageSettings(paperSize="A3"))
    root.uuid = root_uuid
    root.titleBlock = TitleBlock(
        title="SGC P1 - ANNA-B402 carrier, 22x55mm 4L (hierarchy)", date="2026-11-04",
        revision="A-WIP", company="Ski Gate Chrono",
        comments={1: "N1 7-page structure per N1_PARITY_PLAN.md - root is hierarchy-only",
                  2: "generated - not ERC-reviewed"})
    root.libSymbols = []
    root.schematicSymbols = []
    root.graphicalItems = []
    root.globalLabels = []
    root.noConnects = []
    root.textBoxes = []
    root.sheets = sheet_boxes
    # KiCad keeps page numbering for the WHOLE hierarchy here, keyed by "/<box-uuid>".
    # This is the authoritative list: with only "/" present the loader cannot map the
    # child sheets to pages and reports annotation errors.
    root.sheetInstances = [HierarchicalSheetInstance(instancePath="/", page="1")] + [
        HierarchicalSheetInstance(
            instancePath=root_path + det_uuid(fn + ":box"),
            page=str(i + 2))
        for i, (_t, fn, _r) in enumerate(SHEETS)]
    root.to_file(sch_path)
    _quote_uuids(sch_path)

    # ---- .kicad_pro: merge, never clobber ----
    _existing_pro = None
    if os.path.exists(pro_path):
        try:
            with open(pro_path, encoding="utf-8") as _f:
                _existing_pro = json.load(_f)
        except Exception as _e:
            print(f"WARN: existing .kicad_pro unreadable ({_e}) - regenerating a minimal one")
    pro = {
        "board": {"design_settings": {}, "layer_presets": [], "viewports": []},
        "boards": [], "cvpcb": {"equivalence_files": []},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": "sgc_p1.kicad_pro", "version": 1},
        "net_settings": {"classes": [{"bus_width": 12, "clearance": 0.2, "diff_pair_gap": 0.25,
                                      "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
                                      "line_style": 0, "microvia_diameter": 0.3,
                                      "microvia_drill": 0.1, "name": "Default",
                                      "pcb_color": "rgba(0, 0, 0, 0.000)",
                                      "schematic_color": "rgba(0, 0, 0, 0.000)",
                                      "track_width": 0.2, "via_diameter": 0.6, "via_drill": 0.3,
                                      "wire_width": 6}], "meta": {"version": 3}},
        "pcbnew": {"last_paths": {"gencad": "", "idf": "", "netlist": "", "plot": "",
                                  "pos_files": "", "specctra_dsn": "", "step": "", "svg": "",
                                  "vrml": ""}, "page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[root_uuid, "Root"]] + [[det_uuid(fn + ":box"), t.split(":")[0].strip()]
                                           for t, fn, _ in SHEETS],
        "text_variables": {},
    }
    if _existing_pro is not None:
        _existing_pro["sheets"] = pro["sheets"]   # sheet map is authoritative from the generator
        _existing_pro.setdefault("meta", {})["filename"] = "sgc_p1.kicad_pro"
        pro = _existing_pro
    with open(pro_path, "w", encoding="utf-8") as f:
        json.dump(pro, f, indent=2)

    print(f"wrote root + {len(SHEETS)} child sheets: {total_syms} symbol instances "
          f"({total_syms - total_pwr} components + {total_pwr} power), "
          f"{total_w} wires, {total_l} global labels, {total_nc} no-connects")


def prefix_lib_symbols():
    """Prefix embedded lib_symbols keys with the lib nickname on EVERY sheet
    ("sgc_p1:NAME") - bare keys render every instance as "??" in KiCad
    (bench lesson 2026-10-29). Text-level because kiutils deepcopy mangles unitIds."""
    import re as _re
    lib = SymbolLib.from_file(os.path.join(HERE, "sgc_p1.kicad_sym"))
    names = [sym.entryName for sym in lib.symbols]
    files = ["sgc_p1.kicad_sch"] + [fn for _, fn, _ in SHEETS]
    for fname in files:
        path = os.path.join(HERE, fname)
        if not os.path.exists(path):
            continue
        t = open(path, encoding="utf-8").read()
        for n in names:
            esc = _re.escape(n)
            t = _re.sub(r'(\(symbol ")' + esc + r'(")', r'\1sgc_p1:' + n + r'\2', t)
        for _n, _nick in STD_NICK.items():
            _esc = _re.escape(_n)
            t = _re.sub(r'(\(symbol ")' + _esc + r'(")', r'\1' + _nick + ':' + _n + r'\2', t)
        open(path, "w", encoding="utf-8").write(t)
    print(f"lib_symbols keys prefixed on {len(files)} files (project + standard)")


if __name__ == "__main__":
    main()
    prefix_lib_symbols()
