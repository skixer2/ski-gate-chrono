#!/usr/bin/env python3
import re
net = open('C:/Users/v17ni/zio_claw/sgc_bench/hier6.net', encoding='utf-8', errors='replace').read()
n0 = net.find('(nets')
starts = [n0 + m.start() for m in re.finditer(r'\(net\s*\(code', net[n0:])]
targets = [('U8', '3'), ('U8', '6'), ('J5', '1'), ('J5', '2'), ('R13', '1'), ('C20', '1'), ('R4', '1'), ('D1', 'COM')]
for k, s in enumerate(starts):
    e = starts[k + 1] if k + 1 < len(starts) else len(net)
    blk = net[s:e]
    nm = re.search(r'\(name "([^"]+)"\)', blk)
    nodes = re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk)
    hit = [t for t in targets if t in nodes]
    if hit:
        print(nm.group(1), '->', nodes)
