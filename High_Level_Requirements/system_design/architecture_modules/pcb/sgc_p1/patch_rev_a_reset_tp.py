#!/usr/bin/env python3
"""Rev A hardware additions (JP 2026-11-21) - staged patch for sgc_p1.

Two design items, both confirmed against the netlist/schematic first:

1. PINHOLE HARD POWER-CUT on the BQ25120A /MR pin (E1).
   Today U3 pin E1 ("/MR") is assigned net "NC" - the charger's manual-reset
   input is simply unused. JP: "a pinhole reset to cut the power from the
   battery (e.g., a weak resistor to GND)" and "the device must be USB-detached
   to be hardware reset."
   On the BQ25120A /MR is a 64-ms-debounced pushbutton reset: pulling it low
   asserts a system reset and (with the default SYS watchdog) restarts the
   PMIC rails - i.e. a true power cycle of the system, which is precisely what
   a wedged SoftDevice needs. Wiring:
       /MR (E1) -- R_ M R -- +1V8   (100k pull-up, /MR is active-low)
                     |
                  J6 pinhole -- GND
   A 2-pin 1.27 mm pinhole pair (or a 2-pad castellated pinhole) is enough: the
   probe shorts /MR to GND for >64 ms.

2. UART CONSOLE TEST POINTS actually PLACED (not just named nets).
   `sgc_netlist.py` already names UART_TX_TP (P0.20, module pin 10) and
   UART_RX_TP (P0.09, module pin 22), and README said "TP set per OD-3: note
   only". Net names without pads are not debug access, so place them.

Anchor discipline: every replacement asserts its count; the script is
idempotent (refuses to double-apply).
"""
import io
import sys

NL = chr(10)

P = "sgc_netlist.py"
s = io.open(P, encoding="utf-8").read()

if "J6" in s and "PIERCE" not in s and 'ref="J6"' in s:
    print("ALREADY APPLIED - nothing to do")
    sys.exit(0)

# ---------------------------------------------------------------- 1. symbols
# A 2-pin pinhole header symbol for the /MR hard reset.
sym_anchor = '''    "SWD-2X5": dict('''
assert s.count(sym_anchor) == 1, "SWD-2X5 anchor"

new_sym = '''    "PINHOLE-2": dict(
        prefix="J", value="Pinhole reset 2 pad", hw=5.08,
        fp="", ds="",
        desc="2-pad pinhole: short to GND to pull BQ25120A /MR low (>64 ms) = hard system power cycle. JP 2026-11-21.",
        pins=[
            ("1", "MR",  "passive", "L"),
            ("2", "GND", "passive", "L"),
        ],
    ),
    "SWD-2X5": dict('''
s = s.replace(sym_anchor, new_sym, 1)

# ------------------------------------------------------------- 2. placement
# Place J6 next to the charger on the power sheet, and give U3 /MR a real net.
old_u3 = '''        "E1": "NC", "E2": "BQ_CD", "E3": "GND", "E4": "I2C0_SDA", "E5": "I2C0_SCL"}),'''
assert s.count(old_u3) == 1, "U3 E1 anchor"
new_u3 = '''        "E1": "BQ_MR", "E2": "BQ_CD", "E3": "GND", "E4": "I2C0_SDA", "E5": "I2C0_SCL"}),'''
s = s.replace(old_u3, new_u3, 1)

# Insert the pinhole + its pull-up right after the J2 battery entry.
j2_anchor = '''    dict(ref="J2", sym="JST-BAT-3", x=35, y=200, fp=E, dnp=False, nets={
        "1": "VBAT", "2": "TS_NTC", "3": "GND"}),'''
assert s.count(j2_anchor) == 1, "J2 anchor"

new_j2 = j2_anchor + '''

    # --- Rev A addition (JP 2026-11-21): hard power-cycle pinhole -------------
    # BQ25120A /MR is 64 ms-debounced. R_MR holds it high; the pinhole shorts
    # it to GND. Requires USB detached (JP's stated assumption): on VBUS the
    # PMIC restarts its rails but the board stays powered from USB.
    dict(ref="J6", sym="PINHOLE-2", x=35, y=225, fp=E, dnp=False, nets={
        "1": "BQ_MR", "2": "GND"}),
    dict(ref="R_MR", sym="R", x=55, y=225, fp="Resistor_SMD:R_0402_1005Metric",
         dnp=False, value="100k",
         nets={"1": "+1V8", "2": "BQ_MR"}),   # /MR active-low, 100k pull-up'''
s = s.replace(j2_anchor, new_j2, 1)

# ------------------------------------------------- 3. place the UART test pads
# The nets already exist (UART_TX_TP / UART_RX_TP). Put two real pads on them.
swd_anchor = '''    dict(ref="J3", sym="SWD-2X5", x=60, y=258, fp=E, dnp=False, nets={'''
assert s.count(swd_anchor) == 1, "J3 anchor"

new_tp = '''    # Rev A addition (JP 2026-11-21): the netlist already named UART_TX_TP /
    # UART_RX_TP, but no pad existed, so there was no physical console access.
    # 1.0 mm round pads suit a pogo pin probe; UART_TX is the one you need for a
    # read-only console, UART_RX only for driving commands from the PC.
    dict(ref="TP_TX", sym="TESTPOINT", x=95, y=258, fp=E, dnp=False,
         nets={"1": "UART_TX_TP"}),
    dict(ref="TP_RX", sym="TESTPOINT", x=105, y=258, fp=E, dnp=False,
         nets={"1": "UART_RX_TP"}),
''' + swd_anchor
s = s.replace(swd_anchor, new_tp, 1)

# TESTPOINT symbol
tp_anchor = '''    "PINHOLE-2": dict('''
assert s.count(tp_anchor) == 1, "PINHOLE-2 anchor (for TESTPOINT insert)"
new_tp_sym = '''    "TESTPOINT": dict(
        prefix="TP", value="TestPoint 1.0mm", hw=2.54,
        fp="TestPoint:TestPoint_Pad_D1.0mm", ds="",
        desc="Bare 1.0 mm pad for a pogo-pin probe (UART console). JP 2026-11-21.",
        pins=[
            ("1", "1", "passive", "L"),
        ],
    ),
''' + tp_anchor
s = s.replace(tp_anchor, new_tp_sym, 1)

# ---------------------------------------------------------- 4. sheet coverage
# Every ref must appear on exactly one child sheet or the generator asserts.
old_sheet = '''     ["J1","J2","J3","J5","R7","R8","C16","D5","R41","D6","D7","J4"]),'''
assert s.count(old_sheet) == 1, "connectors sheet anchor"
new_sheet = '''     ["J1","J2","J3","J5","J4","J6","R_MR","TP_TX","TP_RX","R7","R8","C16","D5","R41","D6","D7"]),'''
s = s.replace(old_sheet, new_sheet, 1)

io.open(P, "w", encoding="utf-8").write(s)
print("sgc_netlist.py patched: /MR pinhole (J6 + R_MR), TP_TX/TP_RX pads, sheet coverage")
