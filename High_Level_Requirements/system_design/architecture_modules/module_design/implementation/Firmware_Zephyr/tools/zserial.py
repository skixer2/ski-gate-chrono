#!/usr/bin/env python3
"""SGC Zephyr serial console: reset target via SWD, then capture boot log.

Usage: py zserial.py [COM3] [seconds]
  arg1 = serial port   (default COM3, the Nicla Sense CMSIS-DAP console)
  arg2 = capture time  (default 5 s)

Requires: pyserial, PlatformIO's xPack OpenOCD (path below).
COM3 must not be held by another app (close Serial Monitors first).
"""
import serial, subprocess, time, sys

OOCDBIN = r'C:\Users\v17ni\.platformio\packages\tool-openocd\bin\openocd.exe'
OOCDSCRIPTS = r'C:\Users\v17ni\.platformio\packages\tool-openocd\share\openocd\scripts'

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 5

s = serial.Serial(PORT, 115200, timeout=1)
time.sleep(0.3)
s.reset_input_buffer()
subprocess.run([OOCDBIN, '-s', OOCDSCRIPTS,
  '-f', 'interface/cmsis-dap.cfg', '-c', 'transport select swd',
  '-f', 'target/nordic/nrf52.cfg',
  '-c', 'init', '-c', 'reset', '-c', 'run', '-c', 'shutdown'],
  capture_output=True, timeout=25)
buf = b''
t0 = time.time()
while time.time() - t0 < SECS:
    n = s.in_waiting
    if n:
        buf += s.read(n)
    else:
        time.sleep(0.1)
s.close()
print('SERIAL_OUT_BEGIN')
print(buf.decode('utf-8', 'replace'))
print('SERIAL_OUT_END')
