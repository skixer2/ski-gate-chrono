#!/usr/bin/env python3
"""gen_led_v2.py — LABEL-STYLE REDRAW of led_reserve.kicad_sch (JP request 2026-11-03).

Replaces the wire-routed layout (70 wires, 14 junctions, 5 labels) with a readable
label-style sheet: ZERO wires, every pin carries a global label or a power flag.
Netlist membership is preserved EXACTLY (validated after; kicad-cli netdiff on PC).

Net truth (from review/hier9.net - asserted):
  +1V8: U11.1,U11.7 | +5V: C18.1,C19.1,D2.1,J4.1,R11.1,U11.8 | BOOST_EN: U10.4
  BOOST_FB: C19.2,R11.2,R12.1,U10.3 | BOOST_SW: D2.2,L2.1,U10.1
  GND: C17.2,C18.2,J4.3,R12.2,U10.2,U11.4 | LED_DIN_5V: J4.2,U11.6
  SK6812_DATA: U11.2 | VBAT: C17.1,L2.2,U10.5 | NC: U11.3,U11.5
"""
import re

FN = 'led_reserve.kicad_sch'
NL, T = chr(10), chr(9)
SHEET_PATH = '/4921ceaf-687f-4bbc-9968-2c2379bb8701/30db32a4-7fa6-413b-a0de-da7d48f5eae5'

s = open(FN, encoding='utf-8', newline='').read()
orig = len(s)

# ---------- geometry: new instance positions ----------
POS = {  # ref -> (x, y)
    'L2': (55.88, 109.22), 'U10': (86.36, 109.22), 'C17': (55.88, 139.7),
    'D2': (86.36, 139.7), 'C18': (111.76, 139.7), 'C19': (124.46, 139.7),
    'R11': (137.16, 121.92), 'R12': (137.16, 139.7), 'U11': (172.72, 109.22),
    'J4': (215.9, 109.22),
}
# pin rel offsets per lib (from embedded symbols, verified)
PIN = {
    'L':  {'1': (0, 3.81), '2': (0, -3.81)},
    'C':   {'1': (0, 3.81), '2': (0, -3.81)},
    'R':   {'1': (0, 3.81), '2': (0, -3.81)},
    'D2':  {'1': (0, 5.08), '2': (0, -5.08)},          # D_SCHOTTKY
    'U10': {'1': (10.16, 7.62), '2': (0, -10.16), '3': (-10.16, 2.54), '4': (-10.16, 5.08), '5': (-10.16, 7.62)},
    'U11': {'1': (-10.16, 7.62), '2': (-10.16, 5.08), '3': (-10.16, 2.54), '4': (0, -10.16),
            '5': (10.16, 2.54), '6': (10.16, 5.08), '7': (-10.16, 0), '8': (10.16, 7.62)},
    'J4':  {'1': (-10.16, 7.62), '2': (-10.16, 5.08), '3': (-10.16, 2.54)},
}
LIBOF = {'L2': 'L', 'C17': 'C', 'C18': 'C', 'C19': 'C', 'R11': 'R', 'R12': 'R',
         'D2': 'D2', 'U10': 'U10', 'U11': 'U11', 'J4': 'J4'}

def pinpt(ref, pin):
    px, py = PIN[LIBOF[ref]][pin]
    x, y = POS[ref]
    return (round(x + px, 2), round(y - py, 2))

# ---------- connection plan ----------
LBL = {  # (ref, pin) -> (text, angle)
    ('L2', '1'):  ('BOOST_SW', 90), ('U10', '1'): ('BOOST_SW', 0),  ('D2', '2'): ('BOOST_SW', 270),
    ('U10', '3'): ('BOOST_FB', 180), ('C19', '2'): ('BOOST_FB', 270), ('R11', '2'): ('BOOST_FB', 270), ('R12', '1'): ('BOOST_FB', 90),
    ('U10', '4'): ('BOOST_EN', 180),
    ('U11', '2'): ('SK6812_DATA', 180),
    ('U11', '6'): ('LED_DIN_5V', 0), ('J4', '2'): ('LED_DIN_5V', 180),
}
FLAG = {  # (ref, pin) -> (lib_id, value)
    ('L2', '2'): ('sgc_p1:VBAT', 'VBAT'), ('U10', '5'): ('sgc_p1:VBAT', 'VBAT'), ('C17', '1'): ('sgc_p1:VBAT', 'VBAT'),
    ('U10', '2'): ('power:GND', 'GND'), ('C17', '2'): ('power:GND', 'GND'), ('C18', '2'): ('power:GND', 'GND'),
    ('R12', '2'): ('power:GND', 'GND'), ('U11', '4'): ('power:GND', 'GND'), ('J4', '3'): ('power:GND', 'GND'),
    ('D2', '1'): ('sgc_p1:+5V', '+5V'), ('C18', '1'): ('sgc_p1:+5V', '+5V'), ('C19', '1'): ('sgc_p1:+5V', '+5V'),
    ('R11', '1'): ('sgc_p1:+5V', '+5V'), ('U11', '8'): ('sgc_p1:+5V', '+5V'), ('J4', '1'): ('sgc_p1:+5V', '+5V'),
    ('U11', '1'): ('sgc_p1:+1V8', '+1V8'), ('U11', '7'): ('sgc_p1:+1V8', '+1V8'),
}
NC = [('U11', '3'), ('U11', '5')]

# sanity vs hier9 truth
truth = {
    '+1V8': [('U11','1'),('U11','7')], '+5V': [('C18','1'),('C19','1'),('D2','1'),('J4','1'),('R11','1'),('U11','8')],
    'BOOST_EN': [('U10','4')], 'BOOST_FB': [('C19','2'),('R11','2'),('R12','1'),('U10','3')],
    'BOOST_SW': [('D2','2'),('L2','1'),('U10','1')],
    'GND': [('C17','2'),('C18','2'),('J4','3'),('R12','2'),('U10','2'),('U11','4')],
    'LED_DIN_5V': [('J4','2'),('U11','6')], 'SK6812_DATA': [('U11','2')],
    'VBAT': [('C17','1'),('L2','2'),('U10','5')],
}
got = {}
for (r, p), (txt, _a) in LBL.items():
    got.setdefault(txt, []).append((r, p))
for (r, p), (_lib, val) in FLAG.items():
    got.setdefault(val, []).append((r, p))
for k, v in truth.items():
    assert sorted(got.get(k, [])) == sorted(v), (k, sorted(got.get(k, [])), sorted(v))
print('connection plan matches hier9 for all', len(truth), 'nets')

# ---------- 1. delete all wires, junctions, global_labels, no_connects ----------
def del_blocks(s, opener_re, expect):
    out, n = [], 0
    pos = 0
    for m in re.finditer(opener_re, s):
        if m.start() < pos:
            continue
        end = s.find(NL + T + ')', m.start())
        assert end > 0
        out.append((pos, m.start()))
        pos = end + len(NL + T + ')')
        n += 1
    out.append((pos, len(s)))
    assert n == expect, (n, expect)
    return ''.join(s[a:b] for a, b in out)

for opener, expect, tag in ((r'\t\(wire' + NL + r'\t+\(pts', 76, 'wires'),
                            (r'\t\(junction', 14, 'junctions'),
                            (r'\t\(global_label', 5, 'labels'),
                            (r'\t\(no_connect', 2, 'nc')):
    s = del_blocks(s, opener, expect)
    print('deleted', tag)

# ---------- 2. delete the 4 old power flags (by lib_id) ----------
def find_block(s, start):
    depth, i = 0, start
    while True:
        c = s[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1

for lib_id in ('sgc_p1:VBAT', 'power:GND', 'sgc_p1:+1V8', 'sgc_p1:+5V'):
    m = re.search(r'\t\(symbol' + NL + r'\t\t\(lib_id "%s"\)' % re.escape(lib_id), s)
    assert m, lib_id
    b0 = m.start()
    b1 = find_block(s, b0)
    s = s[:b0] + s[b1:]
    print('deleted old flag', lib_id)

# ---------- 3. move the 10 part instances ----------
for ref, (nx, ny) in POS.items():
    rm = re.search(r'\(property "Reference" "%s"' % re.escape(ref), s)
    assert rm, ref
    b0 = s.rfind(NL + T + '(symbol', 0, rm.start())
    b1 = find_block(s, b0)
    blk = s[b0:b1]
    am = re.search(r'(?m)^' + T*2 + r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d]+))?\)', blk)
    assert am, (ref, 'instance at line not found')
    ox, oy = float(am.group(1)), float(am.group(2))
    dx, dy = nx - ox, ny - oy
    blk = blk[:am.start()] + T*2 + '(at %g %g %s)' % (nx, ny, am.group(3) or 0) + blk[am.end():]
    # property ats = 3-tab at lines; shift each
    def shift(m2):
        return T*3 + '(at %g %g %s)' % (float(m2.group(1)) + dx, float(m2.group(2)) + dy, m2.group(3) or 0)
    blk = re.sub(r'(?m)^' + T*3 + r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d]+))?\)', shift, blk)
    s = s[:b0] + blk + s[b1:]
print('moved 10 instances')

# ---------- 4. emit new labels / flags / NCs ----------
def glabel(txt, x, y, ang, uid):
    just = 'left' if ang in (0, 90) else 'right'
    return (T + '(global_label "%s"' % txt + NL + T*2 + '(shape bidirectional)' + NL + T*2 + '(at %g %g %d)' % (x, y, ang) + NL +
            T*2 + '(fields_autoplaced yes)' + NL + T*2 + '(effects' + NL + T*3 + '(font' + NL + T*4 + '(size 1.27 1.27)' + NL +
            T*3 + ')' + NL + T*2 + '(justify %s)' % just + NL + T*2 + ')' + NL + T*2 + '(uuid "%s")' % uid + NL +
            T*2 + '(property "Intersheetrefs" "${INTERSHEET_REFS}"' + NL + T*3 + '(at %g %g %d)' % (x, y, ang) + NL +
            T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL + T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL +
            T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL + T + ')' + NL)

def flag(refnum, lib_id, val, x, y, uid, puid):
    return (T + '(symbol' + NL + T*2 + '(lib_id "%s")' % lib_id + NL + T*2 + '(at %g %g 0)' % (x, y) + NL +
            T*2 + '(unit 1)' + NL + T*2 + '(body_style 1)' + NL + T*2 + '(exclude_from_sim no)' + NL + T*2 + '(in_bom yes)' + NL +
            T*2 + '(on_board yes)' + NL + T*2 + '(in_pos_files yes)' + NL + T*2 + '(dnp no)' + NL + T*2 + '(uuid "%s")' % uid + NL +
            T*2 + '(property "Reference" "%s"' % refnum + NL + T*3 + '(at %g %g 0)' % (x, y - 2.54) + NL + T*3 + '(show_name no)' + NL +
            T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL +
            T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL +
            T*2 + '(property "Value" "%s"' % val + NL + T*3 + '(at %g %g 0)' % (x, y + 2.54) + NL + T*3 + '(show_name no)' + NL +
            T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL +
            T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL +
            T*2 + '(property "Footprint" ""' + NL + T*3 + '(at %g %g 0)' % (x, y) + NL + T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL +
            T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL +
            T*3 + ')' + NL + T*2 + ')' + NL +
            T*2 + '(property "Datasheet" ""' + NL + T*3 + '(at %g %g 0)' % (x, y) + NL + T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL +
            T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL +
            T*3 + ')' + NL + T*2 + ')' + NL +
            T*2 + '(pin "1"' + NL + T*3 + '(uuid "%s")' % puid + NL + T*2 + ')' + NL +
            T*2 + '(instances' + NL + T*3 + '(project "sgc_p1"' + NL + T*4 + '(path "%s"' % SHEET_PATH + NL +
            T*5 + '(reference "%s")' % refnum + NL + T*5 + '(unit 1)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL + T + ')' + NL)

def ncflag(x, y, uid):
    return T + '(no_connect' + NL + T*2 + '(at %g %g)' % (x, y) + NL + T*2 + '(uuid "%s")' % uid + NL + T + ')' + NL

blob = ''
_u = [0x1d2b0000]
def _uuid():
    _u[0] += 1
    v = _u[0]
    return '%08x-cccc-4ccc-8ccc-%012x' % (v, v)
for (r, p), (txt, ang) in LBL.items():
    x, y = pinpt(r, p)
    blob += glabel(txt, x, y, ang, _uuid())
for k, ((r, p), (lib_id, val)) in enumerate(sorted(FLAG.items())):
    x, y = pinpt(r, p)
    blob += flag('#PWR%d' % (95 + k), lib_id, val, x, y, _uuid(), _uuid())
for (r, p) in NC:
    x, y = pinpt(r, p)
    blob += ncflag(x, y, _uuid())

# insert before the first moved part instance
anchor = re.search(r'\t\(symbol' + NL + r'\t\t\(lib_id "', s)
assert anchor
s = s[:anchor.start()] + blob + s[anchor.start():]

open(FN, 'w', encoding='utf-8', newline='').write(s)
print('OK: %d -> %d bytes; 0 wires, %d labels, %d flags, %d NC' % (orig, len(s), len(LBL), len(FLAG), len(NC)))
