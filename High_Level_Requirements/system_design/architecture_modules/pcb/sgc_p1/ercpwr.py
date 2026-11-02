#!/usr/bin/env python3
"""ercpwr.py — dump every not-driven/power violation WITH its @(x,y) context lines,
so #PWR power flags can be identified by position in the schematic."""
import re

rpt = open('C:/Users/v17ni/zio_claw/sgc_bench/erc_now.rpt', encoding='utf-8', errors='replace').read()
lines = rpt.split('\n')
for i, l in enumerate(lines):
    m = re.match(r'\[(\w+)\]: (.*)', l.strip())
    if m and ('not driven' in m.group(2).lower() or m.group(1) == 'pin_to_pin'):
        print(f'[{m.group(1)}] {m.group(2)}')
        for j in range(i + 1, min(i + 5, len(lines))):
            if lines[j].strip().startswith('['):
                break
            if lines[j].strip():
                print(f'    {lines[j].strip()}')
        print()
