#!/usr/bin/env python3
"""Dump one stored run's payload via serial 'H <id>' until rh_end.
Usage: py dump_run.py <id> <out.log> [port]
"""
import sys
import time

import serial

run_id = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else 'run_dump.log'
port = sys.argv[3] if len(sys.argv) > 3 else 'COM3'

s = serial.Serial(port, 115200, timeout=2)
time.sleep(2)
s.reset_input_buffer()
s.write(('H %s\n' % run_id).encode())
d = b''
while b'rh_end' not in d and len(d) < 400000:
    d += s.read(5000)
open(out, 'wb').write(d)
print(len(d), 'bytes ->', out)
