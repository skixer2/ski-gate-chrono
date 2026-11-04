#!/usr/bin/env python3
"""Bench log inspector: context around a marker line + banner checks.
Usage: py logpeek.py <log> <marker-substring>
"""
import sys

path = sys.argv[1]
marker = sys.argv[2] if len(sys.argv) > 2 else 'rv":3'
L = open(path, 'rb').read().decode('utf-8', 'replace').splitlines()
print('lines', len(L))
for i, x in enumerate(L):
    if marker in x:
        print('--- CONTEXT around line', i, '---')
        for j in range(max(0, i - 4), min(len(L), i + 6)):
            print(j, repr(L[j])[:150])
for pat in ('Booting', 'recorder: ready', 'rec_stats', 'FAULT', 'assert', 'rh_'):
    hits = [i for i, x in enumerate(L) if pat in x]
    print(pat, '->', len(hits), 'hits', hits[:5])
