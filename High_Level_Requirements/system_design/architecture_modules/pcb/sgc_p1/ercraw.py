#!/usr/bin/env python3
import re
rpt = open('C:/Users/v17ni/zio_claw/sgc_bench/erc_now.rpt', encoding='utf-8', errors='replace').read()
lines = rpt.split('\n')
out = []
for i, l in enumerate(lines):
    m = re.match(r'\[(\w+)\]: (.*)', l.strip())
    if m and 'driven' in m.group(1) or (m and m.group(1) == 'pin_to_pin'):
        ats = []
        for j in range(i + 1, min(i + 5, len(lines))):
            am = re.search(r'@\(.*\): Symbol (\S+)(?: Pin (\S+))? \[([^\]]+)\]', lines[j])
            if am:
                ats.append(f'{am.group(1)}.{am.group(2) or ""} [{am.group(3)}]')
            if lines[j].strip().startswith('['):
                break
        out.append(f'{m.group(1)}: ' + ' | '.join(ats))
print(f'{len(out)} driven/connectivity violations:')
for o in out:
    print(' ', o)
