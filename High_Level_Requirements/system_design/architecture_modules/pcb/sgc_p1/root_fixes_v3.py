#!/usr/bin/env python3
"""root_fixes_v3.py — DS blocker/major fixes on the ROOT sheet (JP GO 2026-11-03).

B1: R4 pull-up re-topped V_PMID -> +1V8 (label text swap at its stub end)
B2: D1.COM -> V_PMID (global label on pin; SMLP34RGB2W3 = common ANODE, verified)
B3: C20 220nF VREG->GND (new cap; label VREG on U8.6 + C20.1, GND flags)
B4: J5 CON-2 connector for OFF-BOARD piezo button (Langir 16mm per arch doc §3):
    pin1 -> BUTTON net, pin2 -> GND
M1: R13 10k pulldown HOSTBOOT->GND (standalone flash boot, Nicla parity)
M2: R9/R10 100k -> 4.7k 1% (I2C0 pullups for 400kHz)
All label-style (zero new wires). Every edit asserted."""
import re

FN = 'sgc_p1.kicad_sch'
NL, T = chr(10), chr(9)
SHEET_PATH = '/4921ceaf-687f-4bbc-9968-2c2379bb8701'

s = open(FN, encoding='utf-8', newline='').read()
orig = len(s)

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

# ---- B1: the global label "V_PMID" at R4.1's stub (267.97,35.56) -> "+1V8" ----
m = re.search(r'\(global_label "V_PMID"' + NL + T*2 + r'\(shape \w+\)' + NL + T*2 + r'\(at 267\.97 35\.56', s)
assert m, 'R4.1 V_PMID label not found'
s = s[:m.start()] + s[m.start():m.end()].replace('"V_PMID"', '"+1V8"') + s[m.end():]
print('B1: R4 pull-up -> +1V8')

# ---- M2: R9/R10 values ----
for ref in ('R9', 'R10'):
    rm = re.search(r'\(property "Reference" "%s"' % ref, s)
    assert rm, ref
    b0 = s.rfind(NL + T + '(symbol', 0, rm.start())
    b1 = find_block(s, b0)
    blk = s[b0:b1]
    assert blk.count('(property "Value" "100k"') == 1, ref
    s = s[:b0] + blk.replace('(property "Value" "100k"', '(property "Value" "4.7k 1%"') + s[b1:]
print('M2: R9/R10 -> 4.7k 1%')

# ---- emitters ----
def glabel(txt, x, y, ang, uid):
    just = 'left' if ang in (0, 90) else 'right'
    return (T + '(global_label "%s"' % txt + NL + T*2 + '(shape bidirectional)' + NL + T*2 + '(at %g %g %d)' % (x, y, ang) + NL +
            T*2 + '(fields_autoplaced yes)' + NL + T*2 + '(effects' + NL + T*3 + '(font' + NL + T*4 + '(size 1.27 1.27)' + NL +
            T*3 + ')' + NL + T*2 + '(justify %s)' % just + NL + T*2 + ')' + NL + T*2 + '(uuid "%s")' % uid + NL +
            T*2 + '(property "Intersheetrefs" "${INTERSHEET_REFS}"' + NL + T*3 + '(at %g %g %d)' % (x, y, ang) + NL +
            T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL + T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL +
            T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL + T + ')' + NL)

def part(ref, lib_id, val, x, y, fp, uid, puids, dnp=False):
    d = ' yes' if dnp else ' no'
    out = (T + '(symbol' + NL + T*2 + '(lib_id "%s")' % lib_id + NL + T*2 + '(at %g %g 0)' % (x, y) + NL +
           T*2 + '(unit 1)' + NL + T*2 + '(body_style 1)' + NL + T*2 + '(exclude_from_sim no)' + NL + T*2 + '(in_bom yes)' + NL +
           T*2 + '(on_board yes)' + NL + T*2 + '(in_pos_files yes)' + NL + T*2 + '(dnp%s)' % d + NL + T*2 + '(uuid "%s")' % uid + NL +
           T*2 + '(property "Reference" "%s"' % ref + NL + T*3 + '(at %g %g 0)' % (x + 3, y - 1.2) + NL + T*3 + '(show_name no)' + NL +
           T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL +
           T*2 + '(property "Value" "%s"' % val + NL + T*3 + '(at %g %g 0)' % (x + 3, y + 1.2) + NL + T*3 + '(show_name no)' + NL +
           T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL +
           T*2 + '(property "Footprint" "%s"' % fp + NL + T*3 + '(at %g %g 0)' % (x, y) + NL + T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL +
           T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL +
           T*2 + '(property "Datasheet" ""' + NL + T*3 + '(at %g %g 0)' % (x, y) + NL + T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL +
           T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL +
           T*2 + '(property "Description" "DS review fix 2026-11-03"' + NL + T*3 + '(at %g %g 0)' % (x, y) + NL + T*3 + '(hide yes)' + NL + T*3 + '(show_name no)' + NL +
           T*3 + '(do_not_autoplace no)' + NL + T*3 + '(effects' + NL + T*4 + '(font' + NL + T*5 + '(size 1.27 1.27)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL)
    for n, pu in enumerate(puids, 1):
        out += T*2 + '(pin "%d"' % n + NL + T*3 + '(uuid "%s")' % pu + NL + T*2 + ')' + NL
    out += (T*2 + '(instances' + NL + T*3 + '(project "sgc_p1"' + NL + T*4 + '(path "%s"' % SHEET_PATH + NL +
            T*5 + '(reference "%s")' % ref + NL + T*5 + '(unit 1)' + NL + T*4 + ')' + NL + T*3 + ')' + NL + T*2 + ')' + NL + T + ')' + NL)
    return out

def gndflag(refnum, x, y, uid, puid):
    return part(refnum, 'power:GND', 'GND', x, y, '', uid, [puid])

_u = [0x1d3a0000]
def _uuid():
    _u[0] += 1
    v = _u[0]
    return '%08x-a1b2-4c3d-8e9f-%012x' % (v, v)

# ---- B2: D1.COM label ----
blob = glabel('V_PMID', 378.35, 175.0, 0, _uuid())

# ---- B3/M1: labels on U8.3/U8.6 + R13 + C20 at x=287.02 ----
blob += glabel('HOSTBOOT', 312.22, 40.87, 180, _uuid())
blob += glabel('VREG', 312.22, 48.49, 180, _uuid())
blob += glabel('HOSTBOOT', 287.02, 36.83, 90, _uuid())
blob += glabel('VREG', 287.02, 46.99, 90, _uuid())
blob += part('R13', 'Device:R', '10k 1%', 287.02, 40.64, 'Resistor_SMD:R_0402_1005Metric', _uuid(), [_uuid(), _uuid()])
blob += part('C20', 'Device:C', '220n', 287.02, 50.8, 'Capacitor_SMD:C_0402_1005Metric', _uuid(), [_uuid(), _uuid()])
blob += gndflag('#PWR19', 287.02, 44.45, _uuid(), _uuid())
blob += gndflag('#PWR20', 287.02, 54.61, _uuid(), _uuid())

# ---- B4: J5 CON-2 piezo connector near U6 zone ----
blob += glabel('BUTTON', 99.06, 238.76, 180, _uuid())
blob += glabel('BUTTON', 99.06, 241.3, 180, _uuid()) if False else ''  # placeholder guard
blob += gndflag('#PWR21', 99.06, 241.3, _uuid(), _uuid())

# insert all new objects before the first instance symbol
anchor = re.search(r'\t\(symbol' + NL + r'\t\t\(lib_id "', s)
assert anchor
s = s[:anchor.start()] + blob + s[anchor.start():]
print('B2/B3/M1 labels+parts inserted')

# ---- B4: J5 needs CON-2 symbol: embed in root lib_symbols + instance ----
con2 = '''		(symbol "sgc_p1:CON-2"
			(pin_names
				(offset 1.016)
			)
			(exclude_from_sim no)
			(in_bom yes)
			(on_board yes)
			(in_pos_files yes)
			(duplicate_pin_numbers_are_jumpers no)
			(property "Reference" "J"
				(at 0 9.42 0)
				(show_name no)
				(do_not_autoplace no)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Value" "JST-SH-2 piezo (TBD-JP)"
				(at 0 -9.82 0)
				(show_name no)
				(do_not_autoplace no)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Footprint" "Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal"
				(at 0 0 0)
				(show_name no)
				(do_not_autoplace no)
				(hide yes)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Datasheet" "https://www.jst-mfg.co.jp/product/index.html"
				(at 0 0 0)
				(show_name no)
				(do_not_autoplace no)
				(hide yes)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(property "Description" "Off-board piezo arming button (Langir 16mm, arch doc 3) - 2-pin JST-SH"
				(at 0 0 0)
				(show_name no)
				(do_not_autoplace no)
				(hide yes)
				(effects
					(font
						(size 1.27 1.27)
					)
				)
			)
			(symbol "CON-2_0_1"
				(rectangle
					(start -7.62 10.16)
					(end 7.62 -7.62)
					(stroke
						(width 0.254)
						(type default)
					)
					(fill
						(type background)
					)
				)
			)
			(symbol "CON-2_1_1"
				(pin passive line
					(at -10.16 7.62 0)
					(length 2.54)
					(name "PIN1"
						(effects
							(font
								(size 1.016 1.016)
							)
						)
					)
					(number "1"
						(effects
							(font
								(size 1.016 1.016)
							)
						)
					)
				)
				(pin passive line
					(at -10.16 5.08 0)
					(length 2.54)
					(name "PIN2"
						(effects
							(font
								(size 1.016 1.016)
							)
						)
					)
					(number "2"
						(effects
							(font
								(size 1.016 1.016)
							)
						)
					)
				)
			)
		)
		'''
i = s.find(T + '(symbol "sgc_p1:CON-3"')
assert i > 0
s = s[:i] + con2 + s[i:]
# J5 instance (append after blob insertion point: before first instance again)
j5 = part('J5', 'sgc_p1:CON-2', 'JST-SH-2 piezo (TBD-JP)', 109.22, 246.38,
          'Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal', _uuid(), [_uuid(), _uuid()])
anchor = re.search(r'\t\(symbol' + NL + r'\t\t\(lib_id "', s)
s = s[:anchor.start()] + j5 + s[anchor.start():]
print('B4: J5 + CON-2 embedded')

open(FN, 'w', encoding='utf-8', newline='').write(s)
print('OK: %d -> %d bytes' % (orig, len(s)))
