#!/usr/bin/env python3
"""d2_schottky.py — complete the MT3608 boost topology in led_reserve.kicad_sch.

Problem (verified via netlist hier7): L2 was wired SW<->+5V (the DIODE slot) and
VBAT->SW had NO inductor — boost could never work. Fix:
 1. rewire L2 bottom: +5V riser -> VBAT flag rail (2 new wires, 1 deleted)
 2. add D2 sgc_p1:D_SCHOTTKY @ (177,64.5) vertical: A(2, bottom)->BOOST_SW, K(1, top)->+5V
    K typed power_out: the +5V rail source when boosting (clears #PWR93 not-driven)
 3. update the sheet note.
All edits asserted; wire endpoints land exactly on existing junction points."""
import re

FN = 'led_reserve.kicad_sch'
NL, T = chr(10), chr(9)

def wire(x1, y1, x2, y2, uid):
    return (T + '(wire' + NL + T*2 + '(pts' + NL + T*3 + '(xy %s %s) (xy %s %s)' % (x1, y1, x2, y2) + NL +
            T*2 + ')' + NL + T*2 + '(stroke' + NL + T*3 + '(width 0)' + NL + T*3 + '(type default)' + NL + T*2 + ')' + NL +
            T*2 + '(uuid "%s")' % uid + NL + T + ')' + NL)

s = open(FN, encoding='utf-8', newline='').read()
orig = len(s)

# ---- 1. delete the L2-bottom -> +5V riser; insert new wires in its place ----
old_riser = wire('176.15', '87.41', '176.15', '53.98', 'b8eae01b-067d-4c1c-bebf-ecdd1c55f5381')  # uuid value irrelevant, matched by pts
pat = re.compile(re.escape(T + '(wire') + re.escape(NL) + re.escape(T*2) + r'\(pts' + re.escape(NL) + re.escape(T*3) +
                 r'\(xy 176\.15 87\.41\) \(xy 176\.15 53\.98\)' + '[\\s\\S]{0,300}?' + re.escape(NL + T + ')'))
hits = pat.findall(s)
assert len(hits) == 1, 'old L2 riser match count: %d' % len(hits)
new_wires = (wire('176.15', '87.41', '176.15', '30.48', '1d2a0001-aaaa-4aaa-8aaa-0000000000a1') +
             wire('176.15', '30.48', '130.81', '30.48', '1d2a0002-aaaa-4aaa-8aaa-0000000000a2') +
             wire('177', '59.42', '177', '53.98', '1d2a0003-aaaa-4aaa-8aaa-0000000000a3') +
             wire('177', '69.58', '177', '75.98', '1d2a0004-aaaa-4aaa-8aaa-0000000000a4') +
             wire('177', '75.98', '154.31', '75.98', '1d2a0005-aaaa-4aaa-8aaa-0000000000a5'))
s = pat.sub(lambda m: new_wires, s, count=1)

# ---- 2. embedded lib entry sgc_p1:D_SCHOTTKY (inserted before the Device:L entry) ----
def prop(name, val, y, rot, hide=False):
    h = (NL + T*3 + '(hide yes)') if hide else ''
    return (T*2 + '(property "%s" "%s"' % (name, val) + NL + T*3 + '(at 0 %s %s)' % (y, rot) + NL + T*3 + '(show_name no)' + NL +
            T*3 + '(do_not_autoplace no)' + h + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL +
            T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL)

def pin(typ, num, name, at):
    return (T*3 + '(pin %s line' % typ + NL + T*4 + '(at %s)' % at + NL + T*4 + '(length 2.54)' + NL + T*4 + '(name "%s"' % name + NL +
            T*5 + '(effects' + NL + T*6 + '(font' + NL + T*7 + '(size 1.016 1.016)' + NL + T*6 + ')' + NL + T*5 + ')' + NL + T*4 + ')' + NL +
            T*4 + '(number "%s"' % num + NL + T*5 + '(effects' + NL + T*6 + '(font' + NL + T*7 + '(size 1.016 1.016)' + NL + T*6 + ')' + NL +
            T*5 + ')' + NL + T*4 + ')' + NL + T*3 + ')' + NL)

def poly(pts, fill):
    body = NL.join(T*4 + '(xy %s %s)' % p for p in pts)
    return (T*3 + '(polyline' + NL + T*4 + '(pts' + NL + body + NL + T*4 + ')' + NL + T*4 + '(stroke' + NL + T*5 + '(width 0.254)' + NL +
            T*5 + '(type default)' + NL + T*4 + ')' + NL + T*4 + '(fill' + NL + T*5 + '(type %s)' % fill + NL + T*4 + ')' + NL + T*3 + ')' + NL)

libentry = (T*1 + '(symbol "sgc_p1:D_SCHOTTKY"' + NL +
    T*2 + '(pin_names' + NL + T*3 + '(offset 1.016)' + NL + T*2 + ')' + NL +
    T*2 + '(exclude_from_sim no)' + NL + T*2 + '(in_bom yes)' + NL + T*2 + '(on_board yes)' + NL + T*2 + '(in_pos_files yes)' + NL +
    T*2 + '(duplicate_pin_numbers_are_jumpers no)' + NL +
    prop('Reference', 'D', '7.62', '0') + prop('Value', 'SS34', '-7.62', '0') +
    prop('Footprint', 'Diode_SMD:D_SMA', '0', '0', True) + prop('Datasheet', '', '0', '0', True) +
    prop('Description', 'Schottky 40V 3A SMA - MT3608 boost rectifier, V2 LED reserve, DNP v1', '0', '0', True) +
    T*2 + '(symbol "D_SCHOTTKY_0_1"' + NL +
    poly([('-1.905', '2.54'), ('1.905', '2.54')], 'none') +                      # cathode bar
    poly([('-1.905', '2.54'), ('-1.905', '3.81'), ('-0.635', '3.81')], 'none') + # schottky hook L
    poly([('1.905', '2.54'), ('1.905', '1.27'), ('0.635', '1.27')], 'none') +    # schottky hook R
    poly([('-1.905', '-2.54'), ('1.905', '-2.54'), ('0', '2.54'), ('-1.905', '-2.54')], 'background') +  # triangle
    T*2 + ')' + NL +
    T*2 + '(symbol "D_SCHOTTKY_1_1"' + NL +
    pin('power_out', '1', 'K', '0 5.08 270') +
    pin('passive', '2', 'A', '0 -5.08 90') +
    T*2 + ')' + NL + T*1 + ')' + NL)

anchor = T + '(symbol "Device:L"'
i = s.find(anchor)
assert i > 0, 'Device:L lib entry anchor not found'
s = s[:i] + libentry + s[i:]

# ---- 3. D2 instance (inserted before the J4 instance) ----
def iprop(name, val, at):
    return (T*2 + '(property "%s" "%s"' % (name, val) + NL + T*3 + '(at %s)' % at + NL + T*3 + '(show_name no)' + NL +
            T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL +
            T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL)

def iprop_h(name, val):
    return (T*2 + '(property "%s" "%s"' % (name, val) + NL + T*3 + '(at 177 64.5 0)' + NL + T*3 + '(hide yes)' + NL +
            T*3 + '(show_name no)' + NL + T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL +
            T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL)

inst = (T + '(symbol' + NL + T*2 + '(lib_id "sgc_p1:D_SCHOTTKY")' + NL + T*2 + '(at 177 64.5 0)' + NL +
    T*2 + '(unit 1)' + NL + T*2 + '(body_style 1)' + NL + T*2 + '(exclude_from_sim no)' + NL + T*2 + '(in_bom yes)' + NL +
    T*2 + '(on_board yes)' + NL + T*2 + '(in_pos_files yes)' + NL + T*2 + '(dnp yes)' + NL +
    T*2 + '(uuid "1d2a0010-bbbb-4bbb-8bbb-0000000000b1")' + NL +
    iprop('Reference', 'D2', '179.6 63.5 0') +
    iprop('Value', 'SS34 (TBD-JP)', '179.6 65.6 0') +
    iprop_h('Footprint', 'Diode_SMD:D_SMA') + iprop_h('Datasheet', '') +
    iprop_h('Description', 'MT3608 boost rectifier SW->+5V, V2 LED reserve, DNP v1') +
    T*2 + '(pin "1"' + NL + T*3 + '(uuid "1d2a0011-bbbb-4bbb-8bbb-0000000000b2")' + NL + T*2 + ')' + NL +
    T*2 + '(pin "2"' + NL + T*3 + '(uuid "1d2a0012-bbbb-4bbb-8bbb-0000000000b3")' + NL + T*2 + ')' + NL +
    T*2 + '(instances' + NL + T*3 + '(project "sgc_p1"' + NL +
    T*4 + '(path "/4921ceaf-687f-4bbc-9968-2c2379bb8701/30db32a4-7fa6-413b-a0de-da7d48f5eae5"' + NL +
    T*5 + '(reference "D2")' + NL + T*5 + '(unit 1)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL + T + ')' + NL)

j4 = re.search(r'\(property "Reference" "J4"', s)
assert j4, 'J4 instance not found'
ins_at = s.rfind(NL + T + '(symbol', 0, j4.start())
assert ins_at > 0
s = s[:ins_at] + NL + inst + s[ins_at:]

# ---- 4. sheet note ----
old_note = 'MT3608 5V boost + TXS0102 level translator + J4 connector. ALL DNP v1 (JP 2026-11-01).'
new_note = ('MT3608 5V boost + TXS0102 level translator + J4 connector. ALL DNP v1 (JP 2026-11-01). '
            'D2 SS34 Schottky SW->+5V added + L2 rewired VBAT->SW (boost topology completed 2026-11-03).')
n = s.count(old_note)
assert n == 1, 'note occurrences: %d' % n
s = s.replace(old_note, new_note)

open(FN, 'w', encoding='utf-8', newline='').write(s)
print('OK: %d -> %d bytes (+%d)' % (orig, len(s), len(s) - orig))
print('edits: 1 wire deleted, 5 wires added, 1 lib entry, 1 instance D2, note updated')
