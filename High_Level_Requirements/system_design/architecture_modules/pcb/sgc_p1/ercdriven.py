#!/usr/bin/env python3
"""ercdriven.py — classify all 'power pin not driven' ERC violations by net,
cross-referenced against netlist drivers (power_out pins) and the pin types involved."""
import re
from collections import defaultdict

rpt = open('C:/Users/v17ni/zio_claw/sgc_bench/erc_now.rpt', encoding='utf-8', errors='replace').read()
net = open('C:/Users/v17ni/zio_claw/sgc_bench/hier6.net', encoding='utf-8', errors='replace').read()

# 1. netlist: net -> members [(ref, pin)] + lib pin types per (ref,pin)
nets = {}
n0 = net.find('(nets')
for m in re.finditer(r'\(net\s*\(code "\d+"\)\s*\(name "([^"]+)"\)', net[n0:]):
    pass
# simpler: iterate net blocks
i = n0
netblk_positions = []
for m in re.finditer(r'\(net\s*\(code', net[n0:]):
    netblk_positions.append(n0 + m.start())
for k, start in enumerate(netblk_positions):
    end = netblk_positions[k + 1] if k + 1 < len(netblk_positions) else len(net)
    blk = net[start:end]
    nm = re.search(r'\(name "([^"]+)"\)', blk)
    nodes = re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk)
    if nm:
        nets[nm.group(1)] = nodes

# pin types: from lib components section: (lib "MX25R...") (pins (pin (num)(name)(type)))
pintypes = {}   # ref-prefix -> {pin: type}
comps = []
for cm in re.finditer(r'\(comp\s*\(ref "([^"]+)"\)\s*\(value "([^"]+)"\)\s*\(footprint[^)]*\)\s*\(libsource\s*\(lib "([^"]+)"\)\s*\(part "([^"]+)"\)', net):
    comps.append(cm.groups())
libpins = {}
for lm in re.finditer(r'\(libpart\s*\(lib "([^"]+)"\)\s*\(part "([^"]+)"\)([\s\S]*?)\n\t\t\t\)', net):
    lib, part, body = lm.groups()
    pins = {}
    for pm in re.finditer(r'\(pin\s*\(num "([^"]+)"\)\s*\(name "([^"]*)"\)\s*\(type "([^"]+)"\)', body):
        pins[pm.group(1)] = pm.group(3)
    libpins[(lib, part)] = pins

def pin_type(ref, pin):
    # find the comp's libsource
    for c in comps:
        if c[0] == ref:
            lp = libpins.get((c[2], c[3]), {})
            return lp.get(pin, '?')
    return '?'

# 2. ERC violations: "power input pin not driven"
viol = re.findall(r'([^\n]*not driven[^\n]*)', rpt)
print('total not-driven violations:', len(viol))
by_net = defaultdict(list)
for v in viol:
    # violation text like: 'Symbol U3 Pin 5 [Power input, line]' + net mentioned?
    m = re.search(r'Symbol (\S+) Pin (\S+)', v)
    if m:
        ref, pin = m.group(1), m.group(2)
        # find net containing (ref,pin)
        for nname, nodes in nets.items():
            if (ref, pin) in nodes:
                by_net[nname].append((ref, pin, pin_type(ref, pin)))
                break
        else:
            by_net['<unresolved>'].append((ref, pin, '?'))

# 3. drivers per net (power_out presence)
drivers = {}
for nname, nodes in nets.items():
    d = [f'{r}.{p}' for r, p in nodes if pin_type(r, p) == 'power_out']
    drivers[nname] = d

print()
for nname in sorted(by_net):
    pins = by_net[nname]
    print(f'NET {nname}: {len(pins)} undriven-complaints, drivers={drivers.get(nname)}')
    for ref, pin, pt in sorted(set(pins)):
        print(f'    {ref}.{pin} ({pt})')
