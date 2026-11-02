#!/usr/bin/env python3
"""gndctx.py — dump the netlist context around the GND net definition."""
s = open('C:/Users/v17ni/zio_claw/sgc_bench/hier6.net', encoding='utf-8', errors='replace').read()
i = s.find('(name "GND")')
print('found at', i)
print(s[max(0, i - 500):i + 800])
