#!/usr/bin/env python3
"""netmembers.py — print the pin members of named nets in a KiCad s-expr netlist."""
import sys, re

net_path = sys.argv[1]
names = sys.argv[2:]
s = open(net_path, encoding='utf-8').read()

# each net: (nets (node (ref X) (pin N) ...) (name "Y") ...)
for nm in names:
    pat = re.compile(r'\(nets\s*\((?:[^()]|\([^()]*\))*?\(name "%s"\)\s*\)\s*\)' % re.escape(nm))
    # simpler: find '(name "X")' then backtrack to enclosing nets block
    idx = 0
    while True:
        i = s.find('(name "%s")' % nm, idx)
        if i == -1: break
        start = s.rfind('(nets', 0, i)
        depth, j = 0, start
        while j < len(s):
            if s[j] == '(': depth += 1
            elif s[j] == ')':
                depth -= 1
                if depth == 0: break
            j += 1
        blk = s[start:j+1]
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', blk)
        print(nm, '->', nodes)
        idx = j
