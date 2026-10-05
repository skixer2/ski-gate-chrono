#!/usr/bin/env python3
"""Print log lines a..b (0-based slice). Usage: py loglines.py <log> <a> <b>"""
import sys

L = open(sys.argv[1], 'rb').read().decode('utf-8', 'replace').splitlines()
a, b = int(sys.argv[2]), int(sys.argv[3])
for i in range(a, min(b, len(L))):
    print(i, L[i][:140])
