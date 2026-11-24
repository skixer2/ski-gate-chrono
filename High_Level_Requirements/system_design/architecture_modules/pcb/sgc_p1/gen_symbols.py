#!/usr/bin/env python3
"""
gen_symbols.py — builds sgc_p1.kicad_sym from sgc_netlist.py.

Regeneration source (run from this dir):  python3 gen_symbols.py
Format: written via kiutils, then post-processed to the CURRENT KiCad format.

WHY THE POST-PROCESS EXISTS (2026-11-24)
----------------------------------------
kiutils is a KiCad 8-era library. It stamps the file `(version 20231120)` and
writes the old property shape `(effects (font ...) hide)`. KiCad 10 auto-upgrades
that in memory on every load and reports "An error was found when loading the
schematic that has been automatically fixed. Please save the schematic to repair
the broken file."

This file is loaded by sym-lib-table, so an old-format library re-introduces the
dialog even when all seven sheets are clean. Regenerating used to silently undo
any manual upgrade. Now the generator itself emits the current format.
"""
import os
import subprocess
import sys

from kiutils.symbol import SymbolLib, Symbol, SymbolPin
from kiutils.items.common import Position, Property, Effects, Font, Justify, Stroke, Fill
from kiutils.items.syitems import SyRect, SyPolyLine, SyCircle

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sgc_netlist import SYMBOL_PINS, POWER_SYMBOLS, PASSIVE_LEN, PARTS

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


def _upgrade_to_current(path):
    """Migrate the just-written library to KiCad 10 format.

    kiutils always stamps `(version 20231120)` and the old property shape, so
    the bytes on disk are old-format no matter what we pass to SymbolLib.
    `kicad-cli sym upgrade` rewrites in place. If the CLI is missing we fail
    LOUDLY: silently shipping a KiCad 8 library is what caused the repair dialog
    to keep coming back.
    """
    candidates = [
        r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
        r"C:\Progra~1\KiCad\10.0\bin\kicad-cli.exe",
        "kicad-cli",
    ]
    for cli in candidates:
        try:
            r = subprocess.run([cli, "sym", "upgrade", path],
                               capture_output=True, text=True, timeout=120)
        except (OSError, subprocess.SubprocessError):
            continue
        out = ((r.stdout or "") + (r.stderr or "")).strip()
        print(f"  kicad-cli sym upgrade: exit={r.returncode} {out[:90]}")
        if r.returncode == 0:
            return True
    print("  WARNING: kicad-cli not found - library left in OLD format.")
    print("  KiCad 10 will report 'automatically fixed' on load. Install the")
    print("  CLI or run: kicad-cli sym upgrade " + path)
    return False


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
    # Every non-standard symbol that is actually instantiated must be emitted.
    # Derive from SYMBOL_PINS (was a hand-kept list -> silently dropped symbols
    # whenever a new part was added, or when led_reserve.kicad_sch needed one).
    # R/C/L/GND/LED_RGB come from KiCad standard libs.
    STD = {"R", "C", "L"}
    used = {p["sym"] for p in PARTS}
    emit = [n for n in SYMBOL_PINS if n not in STD and (n in used or n in ("JST-BAT-3", "SWD-2X5", "LED_RGB"))]
    for name in emit:
        symbols.append(build_symbol(name, SYMBOL_PINS[name]))
    for pname, style in POWER_SYMBOLS.items():
        if pname == "GND":
            continue   # power:GND from KiCad standard lib
        symbols.append(build_power_symbol(pname, style))

    lib = SymbolLib(version="20231120", generator="sgc_gen", symbols=symbols)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sgc_p1.kicad_sym")
    lib.to_file(out)
    _upgrade_to_current(out)
    print(f"wrote {out}: {len(symbols)} symbols")


if __name__ == "__main__":
    main()
