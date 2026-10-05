#!/usr/bin/env python3
"""Capture serial for N seconds to a log. Usage: py serial_watch.py <secs> <out.log> [port]"""
import sys
import time

import serial

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
out = sys.argv[2] if len(sys.argv) > 2 else 'watch.log'
port = sys.argv[3] if len(sys.argv) > 3 else 'COM3'

s = serial.Serial(port, 115200, timeout=1)
d = b''
t0 = time.time()
while time.time() - t0 < secs:
    d += s.read(5000)
open(out, 'wb').write(d)
print(len(d), 'bytes ->', out)
