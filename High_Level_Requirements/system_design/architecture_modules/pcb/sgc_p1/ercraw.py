#!/usr/bin/env python3
import re
rpt = open('C:/Users/v17ni/zio_claw/sgc_bench/erc_now.rpt', encoding='utf-8', errors='replace').read()
lines = rpt.split('\n')
for i, l in enumerate(lines):
    if 'not driven' in l:
        print('---', i)
        print('\n'.join(lines[max(0, i - 4):i + 2]))
