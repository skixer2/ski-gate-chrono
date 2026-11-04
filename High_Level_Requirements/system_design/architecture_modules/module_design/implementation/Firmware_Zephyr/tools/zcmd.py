#!/usr/bin/env python3
"""zcmd.py — drive the SGC console over the DAPLink serial port.

Usage: py zcmd.py [port] [secs] [command]
  py zcmd.py COM3 5 D          -> directory, 5 s capture
  py zcmd.py COM3 12 "w 40960" -> record a run, 12 s capture
  py zcmd.py COM3 20 "s 0 0 40982" -> stream run 0 as hex frames

The firmware echoes replies on serial (text lines, hex frames for 's').
COM3 is exclusive: close any monitor first. ZioClaw uses this remotely
via node exec; JP can use it from any terminal.
"""
import sys, time, serial

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
# Command: args joined; if no spaces came through (node-exec wrapper
# mangles quotes -> spaced args arrive broken), '_' becomes ' '.
# So remote usage is space-free:  py zcmd.py COM3 12 w_40960
CMD = ' '.join(sys.argv[3:]) if len(sys.argv) > 3 else 'D'
if ' ' not in CMD:
    CMD = CMD.replace('_', ' ')

s = serial.Serial(PORT, 115200, timeout=1)
s.reset_input_buffer()
s.write((CMD + '\n').encode())
end = time.time() + SECS
while time.time() < end:
    d = s.readline()
    if d:
        print(d.decode(errors='replace').rstrip())
s.close()
