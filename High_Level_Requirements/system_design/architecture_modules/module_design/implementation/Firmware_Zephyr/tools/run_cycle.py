#!/usr/bin/env python3
"""Bench run cycle: arm → go → N s → end; capture serial to log.
Usage: py run_cycle.py <seconds> <out.log> [port]
"""
import sys
import time

import serial

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
out = sys.argv[2] if len(sys.argv) > 2 else 'cycle.log'
port = sys.argv[3] if len(sys.argv) > 3 else 'COM3'

s = serial.Serial(port, 115200, timeout=1)
time.sleep(2)
s.reset_input_buffer()
s.write(b'a\n')
time.sleep(2)
s.write(b'g\n')
time.sleep(secs)
s.write(b'e\n')
time.sleep(15)
d = s.read(200000)
# keep reading a bit for POST_RUN->IDLE lines
t0 = time.time()
while time.time() - t0 < 10:
    d += s.read(20000)
    if b'run_ready' in d:
        break
open(out, 'wb').write(d)
print(len(d), 'bytes ->', out)
