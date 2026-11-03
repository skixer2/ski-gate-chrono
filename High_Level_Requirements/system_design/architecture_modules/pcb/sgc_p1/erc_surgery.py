#!/usr/bin/env python3
"""erc_surgery.py — pure text surgery on JP's canon snapshot (2026-11-03).

Pin-type fixes (ERC triage, see ERC_TRIAGE.md):
  ROOT sgc_p1.kicad_sch, embedded lib symbols:
   - USB4085: pin A4 VBUS passive -> power_out   (ONLY A4: single power_out per net avoids out-out conflict)
   - JST-BAT-3: pin 1 VBAT passive -> power_out
   - BHI260AP: QSPI_D0/D1/D2/D3 input->bidirectional; QSPI_CLK, QSPI_CSN input->output;
               MCSB1 input->output; HSDO output->bidirectional
   - MX25R6435F: SO/SIO1 output -> bidirectional (shared MISO)
   - BQ25120A: pin B3 PMID power_out -> passive (duplicate DSBGA bump of A3)
  Footprint assignments:
   - lib MX25R1635F default  -> sgc_p1:USON-8_2x3mm_P0.5mm ; instance U2 same
   - lib MX25R6435F default  -> sgc_p1:USON-8_4x3mm_P0.8mm ; instance U7 same; U7 Value -> MX25R6435FZBIH3
   - CON-3 lib default + instances: BM03B...Horizontal -> SM03B...Horizontal (side entry, JP choice)
  CHILD led_reserve.kicad_sch:
   - same BM03B -> SM03B swap (lib default + J4 instance)
Every edit asserts its exact expected match count. NO other bytes are touched."""
import re, sys

ROOT = 'sgc_p1.kicad_sch'
CHILD = 'led_reserve.kicad_sch'
BM = 'Connector_JST:JST_SH_BM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal'
SM = 'Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal'
log = []

def block(s, marker):
    """return (start, end) of an embedded lib symbol block: marker .. next lib symbol start."""
    i = s.find(marker)
    assert i >= 0, 'marker not found: %s' % marker
    j = s.find('\n\t\t(symbol "sgc_p1:', i + 10)
    k = s.find('\n\t)', i + 10)
    cands = [x for x in (j, k) if x > 0]
    return i, min(cands) if cands else len(s)

def retype(s, b0, b1, pin_name, old, new, expect, number=None):
    """retype pins named pin_name (optionally with pin number) inside block [b0,b1)."""
    blk = s[b0:b1]
    pat = r'\(pin %s (\w+)(?=(?:(?!\(pin )[%s]){0,700}\(name "%s")' % (old, r'\s\S', re.escape(pin_name))
    if number:
        pat = r'\(pin %s (\w+)(?=(?:(?!\(pin )[%s]){0,700}\(name "%s"(?:(?!\(pin )[%s]){0,900}\(number "%s")' % (old, r'\s\S', re.escape(pin_name), r'\s\S', number)
    hits = list(re.finditer(pat, blk))
    assert len(hits) == expect, 'retype %s(%s) %s->%s: expected %d hits, got %d' % (pin_name, number or '-', old, new, expect, len(hits))
    for m in reversed(hits):
        blk = blk[:m.start()] + '(pin %s %s' % (new, m.group(1)) + blk[m.end():]
    log.append('  retyped %s%s: %s x%d -> %s' % (pin_name, '(%s)' % number if number else '', old, expect, new))
    return s[:b0] + blk + s[b1:]

def setprop_in(s, b0, b1, prop, newval, expect=1):
    blk = s[b0:b1]
    pat = r'(\(property "%s" ")[^"]*(")' % prop
    hits = re.findall(pat, blk)
    assert len(hits) == expect, 'setprop %s: expected %d, got %d' % (prop, expect, len(hits))
    blk = re.sub(pat, r'\g<1>%s\g<2>' % newval.replace('\\', '\\\\'), blk, count=0)
    log.append('  property %s -> %s (x%d)' % (prop, newval, expect))
    return s[:b0] + blk + s[b1:]

def setprop_in_sub(s, b0, b1, prop, newval):
    """set property ONLY inside the first sub-block (instance) starting at b0, up to next instance."""
    end = s.find('\n\t\t(symbol\n', b0 + 10)   # next instance at this nesting level
    e = end if end > 0 else b1
    e = min(e, b1)
    return setprop_in(s, b0, e, prop, newval, 1)

# ---------------- ROOT ----------------
s = open(ROOT, encoding='utf-8', newline='').read()
orig_len = len(s)
print('== ROOT %s' % ROOT)

b0, b1 = block(s, '(symbol "sgc_p1:USB4085"')
s = retype(s, b0, b1, 'VBUS', 'passive', 'power_out', 1, number='A4')
b0, b1 = block(s, '(symbol "sgc_p1:JST-BAT-3"')
s = retype(s, b0, b1, 'VBAT', 'passive', 'power_out', 1, number='1')
b0, b1 = block(s, '(symbol "sgc_p1:BHI260AP"')
for nm in ('QSPI_D0', 'QSPI_D1', 'QSPI_D2', 'QSPI_D3'):
    s = retype(s, b0, b1, nm, 'input', 'bidirectional', 1)
for nm in ('QSPI_CLK', 'QSPI_CSN', 'MCSB1'):
    s = retype(s, b0, b1, nm, 'input', 'output', 1)
s = retype(s, b0, b1, 'HSDO', 'output', 'bidirectional', 1)
b0, b1 = block(s, '(symbol "sgc_p1:MX25R6435F"')
s = retype(s, b0, b1, 'SO/SIO1', 'output', 'bidirectional', 1)
s = setprop_in(s, b0, b1, 'Footprint', 'sgc_p1:USON-8_4x3mm_P0.8mm')
b0, b1 = block(s, '(symbol "sgc_p1:MX25R1635F"')
s = setprop_in(s, b0, b1, 'Footprint', 'sgc_p1:USON-8_2x3mm_P0.5mm')
b0, b1 = block(s, '(symbol "sgc_p1:BQ25120A"')
s = retype(s, b0, b1, 'PMID', 'power_out', 'passive', 1, number='B3')
# CON-3 lib default footprint: BM03B -> SM03B (global in root: only CON-3 uses it)
n = s.count(BM)
assert n == 1, 'root BM03B occurrences: %d (expected 1)' % n
s = s.replace(BM, SM)
log.append('  CON-3 lib Footprint BM03B -> SM03B')

# instances: U2, U7 — set Footprint (+ U7 Value). lib_ids are unique in the sheet body.
for ref, libid, fp, val in (('U2', 'sgc_p1:MX25R1635F', 'sgc_p1:USON-8_2x3mm_P0.5mm', None),
                            ('U7', 'sgc_p1:MX25R6435F', 'sgc_p1:USON-8_4x3mm_P0.8mm', 'MX25R6435FZBIH3')):
    li = s.find('(lib_id "%s")' % libid)
    assert li > 0, 'instance lib_id not found: %s' % libid
    rm = re.search(r'\(property "Reference" "%s"' % ref, s[li:])
    assert rm, 'Reference %s not found after its lib_id' % ref
    b0i = s.rfind('\n\t(symbol', 0, li)
    assert b0i > 0, 'instance start not found for %s' % ref
    nxt = s.find('\n\t(symbol', li)
    b1i = nxt if nxt > 0 else len(s)
    s = setprop_in(s, b0i, b1i, 'Footprint', fp)
    if val:
        s = setprop_in(s, b0i, b1i, 'Value', val)

open(ROOT, 'w', encoding='utf-8', newline='').write(s)
print('\n'.join(log)); print('root: %d -> %d bytes' % (orig_len, len(s)))

# ---------------- CHILD ----------------
s = open(CHILD, encoding='utf-8', newline='').read()
print('== CHILD %s' % CHILD)
n = s.count(BM)
assert n == 2, 'child BM03B occurrences: %d (expected 2: lib default + J4 instance)' % n
s = s.replace(BM, SM)
log.append('  child BM03B -> SM03B x2 (CON-3 lib default + J4 instance)')
open(CHILD, 'w', encoding='utf-8', newline='').write(s)
print('\n'.join(log))
print('SURGERY OK')
