#!/usr/bin/env python3
"""netdiff.py — compare two kicad-cli netlists: net -> sorted node membership.
Pin TYPES are expected to differ (retypes); MEMBERSHIP must be identical."""
import re, sys

def parse(fn):
    net = open(fn, encoding='utf-8', errors='replace').read()
    n0 = net.find('(nets')
    starts = [n0 + m.start() for m in re.finditer(r'\(net\s*\(code', net[n0:])]
    out = {}
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else len(net)
        blk = net[s:e]
        nm = re.search(r'\(name "([^"]+)"\)', blk)
        nodes = tuple(sorted(re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk)))
        if nm:
            out[nm.group(1)] = nodes
    return out

a = parse('C:/Users/v17ni/zio_claw/sgc_bench/hier6.net')
b = parse('C:/Users/v17ni/zio_claw/sgc_bench/hier7.net')
print('nets before/after:', len(a), '/', len(b))
only_a = set(a) - set(b)
only_b = set(b) - set(a)
diff = [n for n in set(a) & set(b) if a[n] != b[n]]
print('nets only in BEFORE:', sorted(only_a) or 'none')
print('nets only in AFTER :', sorted(only_b) or 'none')
print('membership-different nets:', len(diff))
for n in sorted(diff):
    print('  ', n, '\n    before:', a[n], '\n    after :', b[n])
print('VERDICT:', 'IDENTICAL MEMBERSHIP' if not (only_a or only_b or diff) else 'DIFFERENCES FOUND')
