#!/usr/bin/env python3
"""Two-sheet split: move the LED-reserve cluster (U10/U11/L2/R11/R12/C17-C19/J4
+ wires/labels/powers/NCs) from sgc_p1.kicad_sch to a new child sheet
led_reserve.kicad_sch. Global labels bridge sheets (no hier pins needed).
Pure text surgery (kiutils = READ-ONLY). Root gets a sheet box in the vacated zone."""
import re, uuid

HERE = '/home/node/.openclaw/workspace/ski_gate_chrono/High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1'
ROOT = HERE + '/sgc_p1.kicad_sch'
CHILD = HERE + '/led_reserve.kicad_sch'
DX, DY = -280.0, -225.0
CLUSTER_REFS = {'U10', 'U11', 'L2', 'R11', 'R12', 'C17', 'C18', 'C19', 'J4'}
CLUSTER_LIBS = {'sgc_p1:MT3608', 'sgc_p1:TXS0102', 'sgc_p1:CON-3', 'sgc_p1:+5V'}
BBOX = (288.0, 218.0, 416.0, 306.0)  # x0 y0 x1 y1 of the reserve zone

def u(): return str(uuid.uuid4())
def f(v):
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    return '0' if s == '-0' else s

s = open(ROOT, encoding='utf-8').read()

def block_at(idx):
    """paren-matched block starting at the '(' preceding idx"""
    start = s.rfind('(', 0, idx + 1)
    depth, i = 0, start
    while i < len(s):
        if s[i] == '(': depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0: return start, i + 1
        i += 1
    raise ValueError

def in_bbox(x, y):
    return BBOX[0] <= x <= BBOX[2] and BBOX[1] <= y <= BBOX[3]

moved, keep = [], []

# ---- 1. symbol instances: by lib_id or Reference ----
for m in re.finditer(r'\(symbol\n\s*\(lib_id "([^"]+)"', s):
    a, b = block_at(m.start())
    blk = s[a:b]
    refm = re.search(r'\(property "Reference" "([^"]+)"', blk)
    ref = refm.group(1) if refm else ''
    if m.group(1) in CLUSTER_LIBS or ref in CLUSTER_REFS:
        moved.append(('symbol', blk))
    elif not ref.startswith('#PWR'):
        keep.append(('symbol', blk))

# power instances of GND/VBAT/+1V8 INSIDE the bbox move too
for m in re.finditer(r'\(symbol\n\s*\(lib_id "sgc_p1:(GND|VBAT|\+1V8)"', s):
    a, b = block_at(m.start())
    blk = s[a:b]
    at = re.search(r'\(at ([-\d.]+) ([-\d.]+)', blk)
    if at and in_bbox(float(at.group(1)), float(at.group(2))):
        moved.append(('symbol', blk))

moved_blocks = set(id(blk) for _, blk in moved)

# ---- 2. wires / labels / no_connects inside bbox ----
def geo_blocks(pattern, pt_group=1):
    out = []
    for m in re.finditer(pattern, s):
        a, b = block_at(m.start())
        blk = s[a:b]
        for pm in re.finditer(r'\(xy ([-\d.]+) ([-\d.]+)', blk):
            if in_bbox(float(pm.group(1)), float(pm.group(2))):
                out.append(blk); break
        else:
            am = re.search(r'\(at ([-\d.]+) ([-\d.]+)', blk)
            if am and in_bbox(float(am.group(1)), float(am.group(2))):
                out.append(blk)
    return out

wire_blocks = geo_blocks(r'\(wire\n')
label_blocks = geo_blocks(r'\(global_label "')
nc_blocks = geo_blocks(r'\(no_connect\n')
moved_all = [blk for _, blk in moved] + wire_blocks + label_blocks + nc_blocks
moved_set = set(moved_all)

# ---- 3. remove moved blocks from root ----
for blk in moved_all:
    assert s.count(blk) == 1, 'block not unique: ' + blk[:60]
    s = s.replace(blk, '', 1)

# ---- 4. shift + transform moved blocks ----
def shift_text(blk):
    def sh_xy(m):
        return '(xy %s %s' % (f(float(m.group(1)) + DX), f(float(m.group(2)) + DY))
    def sh_at(m):
        tail = m.group(3) or ''
        return '(at %s %s%s' % (f(float(m.group(1)) + DX), f(float(m.group(2)) + DY), tail)
    blk = re.sub(r'\(xy ([-\d.]+) ([-\d.]+)', sh_xy, blk)
    blk = re.sub(r'\(at ([-\d.]+) ([-\d.]+)((?: [-\d.]+)?)', sh_at, blk)
    return blk

child_objs = '\n'.join(shift_text(b).strip('\n') for b in moved_all)

# ---- 5. child file ----
child = f'''(kicad_sch
	(version 20260306)
	(generator "eeschema")
	(generator_version "10.0")
	(uuid "{u()}")
	(paper "A4")
	(title_block
		(title "SGC P1 - LED strip reserve (v2 DNP)")
		(date "2026-11-02")
		(rev "A-WIP")
		(comment 1 "Off-board SK6812 x5 feed: MT3608 5V boost + TXS0102 level translator + J4 connector. ALL DNP v1 (JP 2026-11-01).")
		(comment 2 "Connections to root sheet via GLOBAL LABELS (BOOST_EN, SK6812_DATA, VBAT, GND, +1V8, +5V local).")
	)
	(lib_symbols
	)
{child_objs}
	(sheet_instances
		(path "/"
			(page "2")
		)
	)
)
'''
# child needs the lib symbols it uses: copy the four embedded entries from root
libs_needed = ['sgc_p1:MT3608', 'sgc_p1:TXS0102', 'sgc_p1:CON-3', 'sgc_p1:+5V', 'sgc_p1:GND', 'sgc_p1:VBAT', 'sgc_p1:+1V8', 'Device:L', 'Device:C', 'Device:R']
lib_txt = ''
for name in libs_needed:
    m = re.search(r'\n\t\(symbol "%s"[\s\S]*?\n\t\)\n' % re.escape(name), s)
    if m:
        lib_txt += m.group(0)
child = child.replace('\t(lib_symbols\n\t)', '\t(lib_symbols' + lib_txt + '\t)')

open(CHILD, 'w', encoding='utf-8').write(child)

# ---- 6. root: sheet entry in the vacated zone + child page in sheet_instances ----
SHEET_UUID = str(uuid.uuid4())
sheet_entry = '''	(sheet
		(at 300 240)
		(size 55 25)
		(fields_autoplaced yes)
		(stroke
			(width 0.1524)
			(type solid)
		)
		(fill
			(color 0 0 0 0.0000)
		)
		(uuid "{SU}")
		(property "Sheetname" "LED reserve (v2 DNP)"
			(at 300 239.28 0)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(property "Sheetfile" "led_reserve.kicad_sch"
			(at 300 265.72 0)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(instances
			(project "sgc_p1"
				(path "/4921ceaf-687f-4bbc-9968-2c2379bb8701"
					(page "2")
				)
			)
		)
	)
'''.replace("{SU}", str(uuid.uuid4()))
s = s.replace('\t(sheet_instances', sheet_entry + '\t(sheet_instances', 1)
open(ROOT, 'w', encoding='utf-8').write(s)
print(f'moved {len(moved_all)} blocks to child; root sheet entry added')
