#!/usr/bin/env python3
"""LED-strip V2 DNP RESERVE cluster builder (JP decision 2026-10-31 23:36, option b).
U10 MT3608 boost, U11 TXS0102 translator, L2/C17/C18/C19, R11/R12, D2..D6 SK6812.
Pure text surgery (kiutils READ-ONLY). sheet_pt = (inst.X + lib.X, inst.Y - lib.Y)."""
import uuid, sys
from kiutils.schematic import Schematic

HERE = '/home/node/.openclaw/workspace/ski_gate_chrono/High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1'
SCH, LIB = HERE + '/sgc_p1.kicad_sch', HERE + '/sgc_p1.kicad_sym'
NETLIST, VALIDATOR = HERE + '/sgc_netlist.py', HERE + '/validate_sgc_p1.py'
PROJ = '4921ceaf-687f-4bbc-9968-2c2379bb8701'

def u(): return str(uuid.uuid4())
def f(v):
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    return '0' if s == '-0' else s

def pin_xy(side, slot, hw=7.62, step=2.54):
    if side == 'L': return (-hw - 2.54, hw - slot * step, 0)
    if side == 'R': return (hw + 2.54, hw - slot * step, 180)
    if side == 'T': return (0, hw + 2.54, 90)
    return (0, -hw - 2.54, 270)

NEW_SYMS = {
    'MT3608': dict(prefix='U', value='MT3608', hw=7.62,
        fp='Package_TO_SOT_SMD:SOT-23-6',
        ds='https://www.jlcpcb.com/partdetail/Aerosemi-MT3608_C84817.html',
        desc='Aerosemi MT3608 boost 2.0-24V in, 1.2MHz, EN shutdown ~2uA (pin6 NC not drawn). V2 LED reserve, DNP v1',
        pins=[('5','IN','power_in','L',0), ('4','EN','input','L',1), ('3','FB','input','L',2),
              ('1','SW','output','R',0), ('2','GND','power_in','B',0)]),
    'TXS0102': dict(prefix='U', value='TXS0102 (OD-9 TBD)', hw=7.62,
        fp='Package_SO:VSSOP-8_2.3x2mm_P0.5mm',
        ds='https://www.ti.com/lit/ds/symlink/txs0102.pdf',
        desc='TI TXS0102-class dual-supply translator VCCA +1V8 / VCCB +5V for SK6812 DIN level. V2 LED reserve, DNP v1',
        pins=[('1','VCCA','power_in','L',0), ('2','A1','bidirectional','L',1),
              ('3','A2','bidirectional','L',2), ('7','OE','input','L',3),
              ('8','VCCB','power_in','R',0), ('6','B1','bidirectional','R',1),
              ('5','B2','bidirectional','R',2), ('4','GND','power_in','B',0)]),
    'SK6812': dict(prefix='D', value='SK6812MINI', hw=7.62,
        fp='LED_SMD:LED_SK6812MINI_PLCC4_3.5x3.5mm_P1.75mm',
        ds='https://www.lcsc.com/product-detail/light-emitting-diodes-LED_OPSCO-SK6812MINI_C114586.html',
        desc='SK6812-mini RGB LED, 5V, 4-wire chain (DIN<-prev DOUT). x5 strip on VDD_LED. V2 LED reserve, DNP v1',
        pins=[('1','VDD','power_in','T',0), ('2','DIN','input','L',1),
              ('3','GND','power_in','B',0), ('4','DOUT','output','R',1)]),
}

PLACE = [
    ('U11','TXS0102',312,248,{'1':'+1V8','2':'SK6812_DATA','3':'NC','4':'GND','5':'NC','6':'LED_DIN_5V','7':'+1V8','8':'+5V'},None,None),
    ('U10','MT3608',352,248,{'1':'BOOST_SW','2':'GND','3':'BOOST_FB','4':'BOOST_EN','5':'VBAT'},None,None),
    ('L2','L',384,248,{'1':'BOOST_SW','2':'+5V'},'4.7uH 2A','Inductor_SMD:L_0805_2012Metric'),
    ('R11','R',304,266,{'1':'+5V','2':'BOOST_FB'},'75k 1%','Resistor_SMD:R_0402_1005Metric'),
    ('R12','R',320,266,{'1':'BOOST_FB','2':'GND'},'10k 1%','Resistor_SMD:R_0402_1005Metric'),
    ('C17','C',336,266,{'1':'VBAT','2':'GND'},'10uF','Capacitor_SMD:C_0603_1608Metric'),
    ('C18','C',352,266,{'1':'+5V','2':'GND'},'22uF','Capacitor_SMD:C_0805_2012Metric'),
    ('C19','C',368,266,{'1':'+5V','2':'BOOST_FB'},'22pF','Capacitor_SMD:C_0402_1005Metric'),
    ('D2','SK6812',306,282,{'1':'+5V','2':'LED_DIN_5V','3':'GND','4':'LED_CHAIN_23'},None,None),
    ('D3','SK6812',330,282,{'1':'+5V','2':'LED_CHAIN_23','3':'GND','4':'LED_CHAIN_34'},None,None),
    ('D4','SK6812',354,282,{'1':'+5V','2':'LED_CHAIN_34','3':'GND','4':'LED_CHAIN_45'},None,None),
    ('D5','SK6812',378,282,{'1':'+5V','2':'LED_CHAIN_45','3':'GND','4':'LED_CHAIN_56'},None,None),
    ('D6','SK6812',402,282,{'1':'+5V','2':'LED_CHAIN_56','3':'GND','4':'NC'},None,None),
]

# ---------- 1. sgc_netlist.py ----------
nl = open(NETLIST, encoding='utf-8').read()
if '"MT3608": dict(' not in nl:
    pins_txt = ''
    for name, d in NEW_SYMS.items():
        pl = '\n'.join('            ("%s",  "%s","%s", "%s"),' % (n, nm, et, sd) for n, nm, et, sd, sl in d['pins'])
        pins_txt += ('    "%s": dict(\n        prefix="%s", value="%s", fp="%s",\n        ds="%s", desc="%s", hw=%s,\n        pins=[\n%s\n        ]),\n'
                     % (name, d['prefix'], d['value'], d['fp'], d['ds'], d['desc'], d['hw'], pl))
    nl = nl.replace('SYMBOL_PINS = {\n', 'SYMBOL_PINS = {\n' + pins_txt, 1)
    nl = nl.replace('"VBUS_USB": "rail",\n}', '"VBUS_USB": "rail",\n    "+5V":      "rail",\n}', 1)
    parts_txt = '\n    # --- V2 LED STRIP RESERVE (JP 2026-10-31 b): all DNP v1 ---\n'
    for ref, sym, x, y, nets, val, fp in PLACE:
        nets_s = ', '.join('"%s": "%s"' % (k, v) for k, v in nets.items())
        val_s = ', value="%s"' % val if val else ''
        fp_s = ', fp="%s"' % fp if fp else ''
        parts_txt += '    dict(ref="%s", sym="%s", x=%d, y=%d,%s dnp=True%s, nets={%s}),\n' % (ref, sym, x, y, fp_s, val_s, nets_s)
    nl = nl.replace('PARTS = [\n', 'PARTS = [\n' + parts_txt, 1)
    open(NETLIST, 'w', encoding='utf-8').write(nl)
    print('sgc_netlist.py: +3 SYMBOL_PINS, +5V power, +13 PARTS')

# ---------- 2. lib emitters (string concat, no brace traps) ----------
def lib_symbol_text(name, d, embedded):
    t = '\t' if embedded else '  '
    pre = 'sgc_p1:' if embedded else ''
    L = []
    L.append(t + '(symbol "' + pre + name + '"')
    if embedded:
        L += [t*2 + '(exclude_from_sim no)', t*2 + '(in_bom yes)', t*2 + '(on_board yes)',
              t*2 + '(in_pos_files yes)', t*2 + '(duplicate_pin_numbers_are_jumpers no)']
    else:
        L[-1] += ' (in_bom yes) (on_board yes)'
    props = [('Reference', d['prefix'], '0 ' + f(d['hw'] + 1.8), False),
             ('Value', d['value'], '0 -' + f(d['hw'] + 2.2), False),
             ('Footprint', d['fp'], '0 0 0', True),
             ('Datasheet', d['ds'], '0 0 0', True),
             ('Description', d['desc'], '0 0 0', True)]
    for k, v, at, hide in props:
        L.append(t*2 + '(property "%s" "%s" (at %s 0)' % (k, v, at))
        L.append(t*3 + '(show_name no)')
        L.append(t*3 + '(do_not_autoplace no)')
        if hide: L.append(t*3 + '(hide yes)')
        L.append(t*3 + '(effects')
        L.append(t*4 + '(font')
        L.append(t*5 + '(size 1.27 1.27)')
        L.append(t*4 + ')')
        L.append(t*3 + ')')
        L.append(t*2 + ')')
    L.append(t*2 + '(symbol "' + name + '_0_1"')
    L.append(t*3 + '(rectangle')
    L.append(t*4 + '(start -%s %s)' % (f(d['hw']), f(d['hw'])))
    L.append(t*4 + '(end %s -%s)' % (f(d['hw']), f(d['hw'])))
    L.append(t*4 + '(stroke')
    L.append(t*5 + '(width 0.254)'); L.append(t*5 + '(type default)'); L.append(t*4 + ')')
    L.append(t*4 + '(fill')
    L.append(t*5 + '(type background)'); L.append(t*4 + ')')
    L.append(t*3 + ')')
    L.append(t*2 + ')')
    L.append(t*2 + '(symbol "' + name + '_1_1"')
    for num, nm, et, side, slot in d['pins']:
        x, y, ang = pin_xy(side, slot, d['hw'])
        for seg in ['(pin %s line' % et, '(at %s %s %s)' % (f(x), f(y), ang), '(length 2.54)',
                    '(name "%s"' % nm, '(effects', '(font', '(size 1.016 1.016)', ')', '))',
                    '(number "%s"' % num, '(effects', '(font', '(size 1.016 1.016)', ')', '))']:
            depth = 3 if seg.startswith('(pin') else (4 if seg.startswith('(at') or seg.startswith('(length')
                      or seg.startswith('(name') or seg.startswith('(number') else
                      (5 if seg.startswith('(effects') or seg == '(font' or seg == '))' and False else
                      (6 if seg.startswith('(size') or seg == ')' else 4)))
            if seg in ('))', ')'):
                depth = 5 if seg == ')' else 4
                if seg == '))': depth = 4
            L.append(t*depth + seg)
        L.append(t*4 + ')')  # close pin
    L.append(t*2 + ')')
    if embedded: L.append(t*2 + '(embedded_fonts no)')
    L.append(t + ')')
    return '\n'.join(L)

def power_5v_text(embedded):
    t = '\t' if embedded else '  '
    name = 'sgc_p1:+5V' if embedded else '+5V'
    L = [t + '(symbol "' + name + '"']
    if embedded:
        L += [t*2 + '(power global)', t*2 + '(exclude_from_sim no)', t*2 + '(in_bom yes)',
              t*2 + '(on_board yes)', t*2 + '(in_pos_files yes)', t*2 + '(duplicate_pin_numbers_are_jumpers no)']
    else:
        L[-1] += ' (power global) (in_bom yes) (on_board yes)'
    for k, v, at, hide in [('Reference', '#PWR', '0 0 0', True), ('Value', '+5V', '0 3.81', False),
                           ('Footprint', '', '0 0 0', True), ('Datasheet', '', '0 0 0', True),
                           ('Description', "Power symbol creates a global label with name '+5V'", '0 0 0', True)]:
        L.append(t*2 + '(property "%s" "%s" (at %s 0)' % (k, v, at))
        L.append(t*3 + '(show_name no)'); L.append(t*3 + '(do_not_autoplace no)')
        if hide: L.append(t*3 + '(hide yes)')
        L.append(t*3 + '(effects'); L.append(t*4 + '(font'); L.append(t*5 + '(size 1.27 1.27)')
        L.append(t*4 + ')'); L.append(t*3 + ')'); L.append(t*2 + ')')
    L.append(t*2 + '(symbol "+5V_0_1"')
    for p1, p2 in [((-1.27, 2.54), (1.27, 2.54)), ((0, 0), (0, 2.54))]:
        L.append(t*3 + '(polyline'); L.append(t*4 + '(pts')
        L.append(t*5 + '(xy %s %s) (xy %s %s)' % (f(p1[0]), f(p1[1]), f(p2[0]), f(p2[1])))
        L.append(t*4 + ')')
        L.append(t*4 + '(stroke'); L.append(t*5 + '(width 0)'); L.append(t*5 + '(type default)'); L.append(t*4 + ')')
        L.append(t*4 + '(fill'); L.append(t*5 + '(type none)'); L.append(t*4 + ')')
        L.append(t*3 + ')')
    L.append(t*2 + ')')
    L.append(t*2 + '(symbol "+5V_1_1"')
    L += [t*3 + '(pin power_in line', t*4 + '(at 0 0 90)', t*4 + '(length 0)', t*4 + '(name ""',
          t*5 + '(effects', t*6 + '(font', t*7 + '(size 1.27 1.27)', t*6 + ')', t*5 + '))',
          t*4 + '(number "1"', t*5 + '(effects', t*6 + '(font', t*7 + '(size 1.27 1.27)', t*6 + ')', t*5 + '))',
          t*3 + ')']
    L.append(t*2 + ')')
    if embedded: L.append(t*2 + '(embedded_fonts no)')
    L.append(t + ')')
    return '\n'.join(L)

# ---------- 3. schematic surgery ----------
s = open(SCH, encoding='utf-8').read()
cands = [s.find(x) for x in ('\n\t(junction', '\n\t(no_connect', '\n\t(wire', '\n\t(text_box')]
m_first = min(x for x in cands if x != -1)
lib_close = s.rfind('\n\t)', 0, m_first)
lib_add = '\n'.join(lib_symbol_text(n, d, True) for n, d in NEW_SYMS.items()) + '\n' + power_5v_text(True) + '\n'
s = s[:lib_close] + '\n' + lib_add + s[lib_close:]

sch_ro = Schematic.from_file(SCH)
def lib_pins_of(sym):
    cand = [x for x in sch_ro.libSymbols if x.entryName == sym or x.entryName == 'sgc_p1:' + sym
            or x.entryName.endswith(':' + sym)]
    us = next(iter(cand)).units
    unit = next((uu for uu in us if uu.unitId == 1), None) or next((uu for uu in us if uu.pins), us[0])
    return {p.number: (p.position.X, p.position.Y) for p in unit.pins}

POINTS = {}
for ref, sym, x, y, nets, val, fp in PLACE:
    if sym in NEW_SYMS:
        lp = {n: pin_xy(sd, sl, NEW_SYMS[sym]['hw'])[:2] for n, nm, et, sd, sl in NEW_SYMS[sym]['pins']}
    else:
        lp = lib_pins_of(sym)
    POINTS[ref] = {pn: (round(x + lp[pn][0], 3), round(y - lp[pn][1], 3)) for pn in nets if pn in lp}

wires, labels, powers, ncs = [], [], [], []
seen_w = set()
def wire(a, b):
    k = (a, b) if a <= b else (b, a)
    if a != b and k not in seen_w:
        seen_w.add(k); wires.append((a, b))

NETPINS = {}
for ref, sym, x, y, nets, val, fp in PLACE:
    for pn, net in nets.items():
        if net == 'NC':
            ncs.append(POINTS[ref][pn]); continue
        NETPINS.setdefault(net, []).append(POINTS[ref][pn])

PWR_NETS = {'GND', '+1V8', 'VBAT', '+5V'}
rail_y = {}
top, bot = 226.0, 300.0   # rails above cluster / below cluster (GND)
i = 0
for net in NETPINS:
    if net in PWR_NETS and net == 'GND':
        rail_y[net] = bot + 2.54 * 0
        bot += 2.54
    else:
        rail_y[net] = top - 2.54 * i
        i += 1

escape_dx = {}   # pin-x column -> next multiplier (globally unique escape x)
for net, pl in NETPINS.items():
    ry = rail_y[net]
    esc = []
    for pt in pl:
        n = escape_dx.get(pt[0], 0) + 1
        escape_dx[pt[0]] = n
        ex = round(pt[0] + 2.54 * n, 3)
        wire(pt, (ex, pt[1]))          # horizontal escape (globally unique x)
        wire((ex, pt[1]), (ex, ry))    # vertical to rail
        esc.append(ex)
    esc.sort()
    for x1, x2 in zip(esc, esc[1:]):   # segmented rail: every vertical lands on an ENDPOINT
        wire((x1, ry), (x2, ry))
    anchor = (esc[0], ry)
    if net in PWR_NETS:
        powers.append((net, anchor))
    else:
        labels.append((net, anchor))

pwr_n = [90]
def emit_wire(a, b):
    return ('\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
            % (f(a[0]), f(a[1]), f(b[0]), f(b[1]), u()))
def emit_label(net, pt):
    return ('\t(global_label "%s"\n\t\t(shape input)\n\t\t(at %s %s 0)\n\t\t(fields_autoplaced yes)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify left)\n\t\t)\n\t\t(uuid "%s")\n\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"\n\t\t\t(at %s %s 0)\n\t\t\t(hide yes)\n\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n'
            % (net, f(pt[0]), f(pt[1]), u(), f(pt[0]), f(pt[1])))
def emit_nc(pt):
    return '\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)\n' % (f(pt[0]), f(pt[1]), u())
def emit_pwr(sym, pt):
    pwr_n[0] += 1
    ref = '#PWR%d' % pwr_n[0]
    e = ('\t(symbol\n\t\t(lib_id "sgc_p1:%s")\n\t\t(at %s %s 0)\n\t\t(unit 1)\n\t\t(body_style 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n\t\t(dnp no)\n\t\t(uuid "%s")\n'
         % (sym, f(pt[0]), f(pt[1]), u()))
    for k, v, hide in [('Reference', ref, True), ('Value', sym, False), ('Footprint', '', True), ('Datasheet', '', True), ('Description', '', True)]:
        e += ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
              % (k, v, f(pt[0]), f(pt[1]), ('\t\t\t(hide yes)\n' if hide else '')))
    e += '\t\t(pin "1"\n\t\t\t(uuid "%s")\n\t\t)\n\t\t(instances\n\t\t\t(project "sgc_p1"\n\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n' % (u(), PROJ, ref)
    return e
def emit_instance(ref, sym, x, y, nets, val, fp):
    d_v = val if val else NEW_SYMS.get(sym, {}).get('value', sym)
    d_f = fp if fp else NEW_SYMS.get(sym, {}).get('fp', '')
    lib_id = {'R': 'Device:R', 'C': 'Device:C', 'L': 'Device:L'}.get(sym, 'sgc_p1:' + sym)
    e = ('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s 0)\n\t\t(unit 1)\n\t\t(body_style 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n\t\t(dnp yes)\n\t\t(uuid "%s")\n'
         % (lib_id, f(x), f(y), u()))
    for k, v, hide in [('Reference', ref, False), ('Value', d_v, False), ('Footprint', d_f, True), ('Datasheet', '', True), ('Description', 'V2 LED reserve, DNP v1', True)]:
        e += ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
              % (k, v, f(x + 4), f(y - 1), ('\t\t\t(hide yes)\n' if hide else '')))
    for pn in sorted(nets, key=lambda k: (len(k), k)):
        e += '\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)\n' % (pn, u())
    e += '\t\t(instances\n\t\t\t(project "sgc_p1"\n\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n' % (PROJ, ref)
    return e

objects = ''
for a, b in wires: objects += emit_wire(a, b)
for pt in ncs: objects += emit_nc(pt)
for net, pt in labels: objects += emit_label(net, pt)
for sym, pt in powers: objects += emit_pwr(sym, pt)
for ref, sym, x, y, nets, val, fp in PLACE: objects += emit_instance(ref, sym, x, y, nets, val, fp)

anchor = '\t(sheet_instances'
i = s.find(anchor)
s = s[:i] + objects + s[i:]
s = s.replace('Boost BOM (hw doc 7.3): L 4.7uH/2A, Cin 10uF, Cout 22uF, Rfb 75k/10k, Cff 22pF. NOT drawn - capture at layout.',
              'Boost BOM (hw doc 7.3): L2 4.7uH/2A, C17/C18, R11/R12, C19 - DRAWN as DNP reserve cluster (U10/U11/L2/D2-D6), v2 decision JP 2026-10-31.')
open(SCH, 'w', encoding='utf-8').write(s)
print('schematic: +%d wires, +%d NC, +%d labels, +%d power, +13 instances, +4 lib' % (len(wires), len(ncs), len(labels), len(powers)))

# ---------- 4. project lib ----------
pl = open(LIB, encoding='utf-8').read()
if 'MT3608' not in pl:
    add = '\n'.join(lib_symbol_text(n, d, False) for n, d in NEW_SYMS.items()) + '\n' + power_5v_text(False) + '\n'
    idx = pl.rstrip().rfind(')')
    pl = pl.rstrip()[:idx].rstrip() + '\n' + add + ')\n'
    open(LIB, 'w', encoding='utf-8').write(pl)
    print('sgc_p1.kicad_sym: +4 symbols')

# ---------- 5. validator ----------
v = open(VALIDATOR, encoding='utf-8').read()
v = v.replace('"U2": "MX25R1635FZUIH0"}', '"U2": "MX25R1635FZUIH0", "U10": "MT3608", "U11": "TXS0102"}')
v = v.replace('if (n_c, n_r, n_l, n_d) != (16, 10, 1, 1):', 'if (n_c, n_r, n_l, n_d) != (19, 12, 2, 1):')
v = v.replace('"MX25R1635F": 8, "USB4085": 20,', '"MX25R1635F": 8, "USB4085": 20, "MT3608": 5, "TXS0102": 8, "SK6812": 4,')
v = v.replace('expected 16/10/1/1', 'expected 19/12/2/1 (+5 SK6812 DNP separate)')
open(VALIDATOR, 'w', encoding='utf-8').write(v)
print('validator: census updated')
