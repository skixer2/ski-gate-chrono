#!/usr/bin/env python3
"""ercdriven.py — classify all 'not driven' ERC violations by net,
cross-referenced against netlist drivers (power_out pins) and pin types.
v2: parse the @(x,y): Symbol Ref Pin [type, ...] context lines that follow each
violation header (headers themselves carry no symbol info)."""
import re
from collections import defaultdict

rpt = open('C:/Users/v17ni/zio_claw/sgc_bench/erc_now.rpt', encoding='utf-8', errors='replace').read()
net = open('C:/Users/v17ni/zio_claw/sgc_bench/hier6.net', encoding='utf-8', errors='replace').read()
rlines = rpt.split('\n')

# 1. violations: header line contains 'not driven'; symbol+pin from following @( ) lines
viol = []   # (vtype, ref, pin)
for i, l in enumerate(rlines):
    m = re.match(r'\[(\w+)\]: (.*)', l.strip())
    if m and 'not driven' in m.group(2).lower():
        for j in range(i + 1, min(i + 5, len(rlines))):
            am = re.search(r'@\(.*\): Symbol (\S+)(?: Pin (\S+))? \[([^\]]+)\]', rlines[j])
            if am:
                viol.append((m.group(1), am.group(1), am.group(2) or ''))
                break
            if rlines[j].strip().startswith('['):
                break
print(f'total not-driven violations: {len(viol)}')

# 2. netlist: net name -> [(ref,pin)]
nets = {}
n0 = net.find('(nets')
starts = [n0 + m.start() for m in re.finditer(r'\(net\s*\(code', net[n0:])]
for k, s in enumerate(starts):
    e = starts[k + 1] if k + 1 < len(starts) else len(net)
    blk = net[s:e]
    nm = re.search(r'\(name "([^"]+)"\)', blk)
    nodes = re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk)
    if nm:
        nets[nm.group(1)] = nodes

# 3. lib pin types
libpins = {}
for lm in re.finditer(r'\(libpart\s*\(lib "([^"]+)"\)\s*\(part "([^"]+)"\)([\s\S]*?)\n\t\t\t\)', net):
    lib, part, body = lm.groups()
    pins = {}
    for pm in re.finditer(r'\(pin\s*\(num "([^"]+)"\)\s*\(name "[^"]*"\)\s*\(type "([^"]+)"\)', body):
        pins[pm.group(1)] = pm.group(2)
    libpins[(lib, part)] = pins
comps = {}
for cm in re.finditer(r'\(comp\s*\(ref "([^"]+)"\)([\s\S]*?)\n\t\t\)', net):
    ref, body = cm.groups()
    ls = re.search(r'\(libsource\s*\(lib "([^"]+)"\)\s*\(part "([^"]+)"\)', body)
    if ls:
        comps[ref] = (ls.group(1), ls.group(2))

def pin_type(ref, pin):
    lp = libpins.get(comps.get(ref, ('?', '?')), {})
    return lp.get(pin, '?')

# 4. map each violation pin -> net; drivers = power_out pins on that net
node2net = {}
for nname, nodes in nets.items():
    for r, p in nodes:
        node2net[(r, p)] = nname

by_net = defaultdict(list)
for vtype, ref, pin in viol:
    nname = node2net.get((ref, pin), '<unresolved>')
    by_net[nname].append((vtype, ref, pin))

for nname in sorted(by_net):
    pins = sorted(set(by_net[nname]))
    members = nets.get(nname, [])
    typed = [f'{r}.{p}({pin_type(r,p)})' for r, p in members]
    drivers = [f'{r}.{p}' for r, p in members if pin_type(r, p) in ('power_out', 'output')]
    print(f'\nNET "{nname}": {len(pins)} complaints; drivers={drivers}')
    print(f'    members: {", ".join(typed) if typed else "?"}')
    for vtype, ref, pin in pins:
        print(f'    -> {ref}.{pin} ({pin_type(ref, pin)}) [{vtype}]')
