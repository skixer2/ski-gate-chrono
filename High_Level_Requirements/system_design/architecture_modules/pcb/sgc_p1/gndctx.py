#!/usr/bin/env python3
"""gndctx v2 — the GND net entry inside the (nets section of the netlist."""
import re
s = open('C:/Users/v17ni/zio_claw/sgc_bench/hier6.net', encoding='utf-8', errors='replace').read()
n0 = s.find('(nets')
i = s.find('(name "GND")', n0)
print('nets section at', n0, '— GND net name at', i)
# backtrack to the enclosing (net block start
blk = s.rfind('(net', n0, i)
depth, j = 0, blk
while j < len(s):
    if s[j] == '(': depth += 1
    elif s[j] == ')':
        depth -= 1
        if depth == 0: break
    j += 1
netblk = s[blk:j + 1]
nodes = re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)'', netblk)
print('GND net members (%d):' % len(nodes))
for r, p in nodes:
    print('  %s.%s' % (r, p))
