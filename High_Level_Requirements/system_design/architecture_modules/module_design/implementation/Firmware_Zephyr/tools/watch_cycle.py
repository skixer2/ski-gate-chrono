#!/usr/bin/env python3
"""State-aware serial watch + app reconnect: parses uiautomator dumps to find
buttons by content-desc substring. Usage: py watch_cycle.py <out.log>"""
import os
import re
import subprocess
import sys
import time

import serial

OUT = sys.argv[1] if len(sys.argv) > 1 else 'watch_cycle.log'


def adb(*args):
    return subprocess.run(['adb'] + list(args), capture_output=True,
                          timeout=25).stdout


def ui_dump():
    adb('shell', 'rm', '/sdcard/ui.xml')
    adb('shell', 'uiautomator', 'dump', '/sdcard/ui.xml')
    tmp = os.environ.get('TEMP', 'C:\\Windows\\Temp') + '\\ui_dump.xml'
    adb('pull', '/sdcard/ui.xml', tmp)
    try:
        with open(tmp, 'rb') as f:
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return 


def find_and_tap(needle, wait=6.0):
    xml = ui_dump()
    for m in re.finditer(r'<node[^>]*content-desc="([^"]*)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"[^>]*/?>', xml):
        desc = m.group(1).replace('&#10;', ' ')
        if needle.lower() in desc.lower():
            x = (int(m.group(2)) + int(m.group(4))) // 2
            y = (int(m.group(3)) + int(m.group(5))) // 2
            adb('shell', 'input', 'tap', str(x), str(y))
            time.sleep(wait)
            return True
    return False


s = serial.Serial('COM3', 115200, timeout=1)
d = b''

# state machine: whichever button exists
for _ in range(6):
    if find_and_tap('Disconnect', 4):
        break
    if find_and_tap('Scan Devices', 4):
        break
    time.sleep(2)

for _ in range(6):
    if find_and_tap('SGC-Dev', 12):
        break
    time.sleep(2)

# capture through the connect+read window
t0 = time.time()
while time.time() - t0 < 12:
    d += s.read(5000)
    if b'abc9_read' in d and d.count(b'abc9_read') >= 2:
        time.sleep(3)
        d += s.read(20000)
        break

open(OUT, 'wb').write(d)
print(len(d), 'bytes; abc9_read events:', d.count(b'abc9_read'))
