#!/usr/bin/env python3
"""erchist.py — histogram of all ERC violation types + first samples, from erc_now.rpt."""
import re
from collections import defaultdict

rpt = open('C:/Users/v17ni/zio_claw/sgc_bench/erc_now.rpt', encoding='utf-8', errors='replace').read()
lines = rpt.split('\n')

# unconnected-items blocks have their own header line " ** Found N unconnected items"
unconn = re.search(r'\*\* Found (\d+) unconnected items', rpt)

hist = defaultdict(int)
samples = defaultdict(list)
for l in lines:
    m = re.match(r'\[(\w+)\]: (.*)', l.strip())
    if m:
        t, desc = m.group(1), m.group(2)
        hist[t] += 1
        if len(samples[t]) < 3:
            samples[t].append(desc[:100])

total = sum(hist.values())
print(f'violation lines total: {total}')
if unconn:
    print(f'unconnected-items blocks: {unconn.group(1)} (counted separately by KiCad)')
print()
for t, n in sorted(hist.items(), key=lambda kv: -kv[1]):
    print(f'{n:4d}  {t}')
    for s in samples[t]:
        print(f'        e.g. {s}')
