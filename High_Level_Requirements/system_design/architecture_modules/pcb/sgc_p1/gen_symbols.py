#!/usr/bin/env python3
"""
gen_symbols.py — builds sgc_p1.kicad_sym (KiCad 8 symbol library) from sgc_netlist.py.

Regeneration source (run from this dir):  python3 gen_symbols.py
Format: KiCad 8 (kicad_symbol_lib version 20231120), written via kiutils.
"""
import os
import sys

from kiutils.symbol import SymbolLib, Symbol, SymbolPin
from kiutils.items.common import Position, Property, Effects, Font, Justify, Stroke, Fill
from kiutils.items.syitems import SyRect, SyPolyLine, SyCircle

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sgc_netlist import SYMBOL_PINS, POWER_SYMBOLS, PASSIVE_LEN

LIB_NAME = "sgc_p1"
F = lambda s=1.27: Font(height=s, width=s)


def prop(name, val, at=(0, 0), hide=False, just=None):
    ef = Effects(font=F(), justify=Justify(horizontally=just) if just else Justify())
    if hide:
        ef.hide = True
    return Property(name, val, position=Position(X=at[0], Y=at[1], angle=0), effects=ef)


def _unit(entry, uid, sid):
    return Symbol(entryName=entry, unitId=uid, styleId=sid)


def build_symbol(name, spec):
    """Build one top-level symbol (with _0_1 graphics unit and _1_1 pin unit)."""
    pins = spec["pins"]
    hw = spec["hw"]

    # Column layout
    cols = {"L": [p for p in pins if p[3] == "L"],
            "R": [p for p in pins if p[3] == "R"]}
    top_pins = [p for p in pins if p[3] == "T"]
    bot_pins = [p for p in pins if p[3] == "B"]

    # Body height from tallest column (vertical passives use PASSIVE_LEN)
    if top_pins or bot_pins:
        ln = PASSIVE_LEN.get(name, 3.81)
        body_top, body_bot = ln, -ln
    else:
        h2 = (max(len(cols["L"]), len(cols["R"])) - 1) / 2.0 * 2.54
        body_top, body_bot = h2 + 2.54, -(h2 + 2.54)

    root = Symbol(entryName=name, inBom=True, onBoard=True,
                  properties=[
                      prop("Reference", spec["prefix"], (0, round(body_top + 1.8, 3))),
                      prop("Value", spec["value"], (0, round(body_bot - 2.2, 3))),
                      prop("Footprint", spec["fp"], (0, 0), hide=True),
                      prop("Datasheet", spec["ds"], (0, 0), hide=True),
                      prop("Description", spec["desc"], (0, 0), hide=True),
                  ])

    g = _unit(name, 0, 1)
    if top_pins or bot_pins:
        ln = PASSIVE_LEN.get(name, 3.81)
        if name == "R":
            g.graphicItems = [SyRect(start=Position(-hw, -ln + 1.27), end=Position(hw, ln - 1.27),
                                     stroke=Stroke(width=0.254, type="default"), fill=Fill(type="background"))]
        elif name == "L":
            g.graphicItems = [
                SyRect(start=Position(-hw, -ln + 1.27), end=Position(hw, ln - 1.27),
                       stroke=Stroke(width=0.254, type="default"), fill=Fill(type="background")),
                SyPolyLine(points=[Position(-hw, 0), Position(hw, 0)],
                           stroke=Stroke(width=0.254, type="default"), fill=Fill(type="none")),
            ]
        elif name == "C":
            g.graphicItems = [
                SyPolyLine(points=[Position(-0.762, 0.508), Position(0.762, 0.508)],
                           stroke=Stroke(width=0.254, type="default"), fill=Fill(type="none")),
                SyPolyLine(points=[Position(-0.762, -0.508), Position(0.762, -0.508)],
                           stroke=Stroke(width=0.254, type="default"), fill=Fill(type="none")),
            ]
        else:  # LED_RGB
            g.graphicItems = [SyRect(start=Position(-hw, -2.54), end=Position(hw, 2.54),
                                     stroke=Stroke(width=0.254, type="default"), fill=Fill(type="background"))]
    else:
        g.graphicItems = [SyRect(start=Position(-hw, body_top), end=Position(hw, body_bot),
                                 stroke=Stroke(width=0.254, type="default"), fill=Fill(type="background"))]

    u = _unit(name, 1, 1)
    pin_list = []

    def add_pin(num, pname, etype, x, y, angle):
        pin_list.append(SymbolPin(electricalType=etype, graphicalStyle="line",
                                  position=Position(X=round(x, 3), Y=round(y, 3), angle=angle), length=2.54,
                                  name=pname, number=num,
                                  nameEffects=Effects(font=F(1.016)), numberEffects=Effects(font=F(1.016))))

    for side, ang, xf in (("L", 0, -(hw + 2.54)), ("R", 180, hw + 2.54)):
        for i, (num, pname, etype, _s) in enumerate(cols[side]):
            y = ((len(cols[side]) - 1) / 2.0) * 2.54 - i * 2.54
            add_pin(num, pname, etype, xf, y, ang)
    for i, (num, pname, etype, _s) in enumerate(top_pins):
        add_pin(num, pname, etype, -2.54 + i * 5.08, PASSIVE_LEN.get(name, 3.81) + 2.54, 270)
    for i, (num, pname, etype, _s) in enumerate(bot_pins):
        add_pin(num, pname, etype, -2.54 + i * 5.08, -(PASSIVE_LEN.get(name, 3.81) + 2.54), 90)

    u.pins = pin_list
    root.units = [g, u]
    return root


def build_power_symbol(name, style):
    """KiCad-standard-library-style power symbol: pin (len 0) at origin."""
    root = Symbol(entryName=name, isPower=True, inBom=True, onBoard=True,
                  properties=[
                      prop("Reference", "#PWR", (0, 0), hide=True),
                      prop("Value", name, (0, 3.81 if style == "rail" else -3.81)),
                      prop("Footprint", "", (0, 0), hide=True),
                      prop("Datasheet", "", (0, 0), hide=True),
                      prop("Description", f"Power symbol creates a global label with name '{name}'", (0, 0), hide=True),
                  ])
    g = _unit(name, 0, 1)
    st = Stroke(width=0.0, type="default")
    if style == "gnd":
        g.graphicItems = [SyPolyLine(points=[Position(0, 0), Position(0, -1.27),
                                          Position(1.27, -1.27), Position(0, -2.54),
                                          Position(-1.27, -1.27), Position(0, -1.27)],
                                     stroke=st, fill=Fill(type="none"))]
        pin = SymbolPin(electricalType="power_in", graphicalStyle="line",
                        position=Position(X=0, Y=0, angle=270), length=0.0, name="~", number="1")
    else:  # rail: arrow-less bar style, bar above origin
        g.graphicItems = [
            SyPolyLine(points=[Position(0, 0), Position(0, 2.54)], stroke=st, fill=Fill(type="none")),
            SyPolyLine(points=[Position(-1.27, 2.54), Position(1.27, 2.54)], stroke=st, fill=Fill(type="none")),
        ]
        pin = SymbolPin(electricalType="power_in", graphicalStyle="line",
                        position=Position(X=0, Y=0, angle=90), length=0.0, name="~", number="1")
    u = _unit(name, 1, 1)
    u.pins = [pin]
    root.units = [g, u]
    return root


def main():
    symbols = []
    for name in ["ANNA-B402", "BQ25120A", "MAX17048", "IS31FL3194", "LDC1612", "BHI260AP",
                 "BMP390", "MX25R6435F", "MX25R1635F", "USB4085", "JST-BAT-3", "SWD-2X5",
                 "LED_RGB"]:   # R/C/L/GND -> KiCad standard libs (Device:*/power:GND):
        symbols.append(build_symbol(name, SYMBOL_PINS[name]))
    for pname, style in POWER_SYMBOLS.items():
        if pname == "GND":
            continue   # power:GND from KiCad standard lib
        symbols.append(build_power_symbol(pname, style))

    lib = SymbolLib(version="20231120", generator="sgc_gen", symbols=symbols)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sgc_p1.kicad_sym")
    lib.to_file(out)
    print(f"wrote {out}: {len(symbols)} symbols")


if __name__ == "__main__":
    main()
