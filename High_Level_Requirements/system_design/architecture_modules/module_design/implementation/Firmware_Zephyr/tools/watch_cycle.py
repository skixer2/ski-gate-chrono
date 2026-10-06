#!/usr/bin/env python3
"""Serial watch + app reconnect cycle via adb (single-script, no shell quoting).
Usage: py watch_cycle.py <out.log>
"""
import subprocess
import sys
import time

import serial

OUT = sys.argv[1] if len(sys.argv) > 1 else 'watch_cycle.log'
PY = r'C:\Users\v17ni\AppData\Local\Programs\Python\Python313\python.exe'


def adb(*args):
    subprocess.run(['adb'] + list(args), capture_output=True, timeout=20)


def dump_and_tap(x, y, wait=6):
    adb('shell', 'input', 'tap', str(x), str(y))
    time.sleep(wait)


s = serial.Serial('COM3', 115200, timeout=1)
d = b''

# app: assume connected panel -> Disconnect, then Scan, then SGC-Dev
dump_and_tap(1008, 165, 6)    # Disconnect
dump_and_tap(802, 1770, 8)    # Scan Devices
dump_and_tap(540, 1719, 20)   # SGC-Dev row (connect + reads)

t0 = time.time()
while time.time() - t0 < 15:
    d += s.read(5000)
open(OUT, 'wb').write(d)
print(len(d), 'bytes ->', OUT)
