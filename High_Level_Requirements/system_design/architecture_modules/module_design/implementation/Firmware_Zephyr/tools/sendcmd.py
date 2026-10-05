#!/usr/bin/env python3
"""Send one serial command, capture the reply.
Usage: py sendcmd.py <command> <out.log> [port] [wait_s]
"""
import sys
import time

import serial

cmd = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else 'cmd.log'
port = sys.argv[3] if len(sys.argv) > 3 else 'COM3'
wait = float(sys.argv[4]) if len(sys.argv) > 4 else 4

s = serial.Serial(port, 115200, timeout=1)
time.sleep(2)
s.reset_input_buffer()
s.write((cmd + '\n').encode())
time.sleep(wait)
d = s.read(40000)
open(out, 'wb').write(d)
print(len(d), 'bytes ->', out)
