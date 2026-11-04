#!/usr/bin/env python3
"""Tail-N flutter lines from a logcat dump. Usage: py lctail.py <log> [N]"""
import sys

L = open(sys.argv[1], 'rb').read().decode('utf-8', 'replace').splitlines()
n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
fl = [l for l in L if 'flutter' in l.lower()]
for l in fl[-n:]:
    print(l[:220])
