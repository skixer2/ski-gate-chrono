#!/usr/bin/env python3
"""Grep logcat dump for flutter/SGC/FT lines. Usage: py lcgrep.py <log>"""
import sys

pats = ('flutter', 'SGC', '[FT]', 'syncing', 'char not found', 'run count',
        'device runs', 'parse run list')
L = open(sys.argv[1], 'rb').read().decode('utf-8', 'replace').splitlines()
hits = [l for l in L if any(p.lower() in l.lower() for p in pats)]
print(f"{len(hits)} hits of {len(L)} lines")
for l in hits[-60:]:
    print(l[:200])
