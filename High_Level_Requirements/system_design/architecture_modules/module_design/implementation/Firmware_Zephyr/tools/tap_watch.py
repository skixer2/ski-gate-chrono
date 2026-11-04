#!/usr/bin/env python3
"""Tap SGC-Dev row (adb) THEN open serial and capture. The connect+reads
happen within seconds of the tap; serial opens ~1 s later. Catches them.
Usage: py tap_watch.py <out.log> [secs]"""
import subprocess
import sys
import time

import serial

OUT = sys.argv[1] if len(sys.argv) > 1 else 'tap_watch.log'
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 25

# assume scan list is on screen with SGC-Dev at top row
subprocess.run(['adb', 'shell', 'input', 'tap', '540', '1719'],
               capture_output=True, timeout=15)

s = serial.Serial('COM3', 115200, timeout=1)
d = b''
t0 = time.time()
while time.time() - t0 < SECS:
    d += s.read(5000)
open(OUT, 'wb').write(d)
print(len(d), 'bytes; abc9:', d.count(b'abc9_read'),
      'connected:', d.count(b'connected:'))
