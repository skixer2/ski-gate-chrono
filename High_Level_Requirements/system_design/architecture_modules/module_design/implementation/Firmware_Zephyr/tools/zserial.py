#!/usr/bin/env python3
"""SGC Zephyr serial console: reset target via SWD, then capture boot log.

Usage: py zserial.py [port] [seconds]
  arg1 = serial port   (default COM3, the Nicla Sense CMSIS-DAP console)
  arg2 = capture time  (default 5 s)

Requires: pyserial + OpenOCD (auto-discovered: PlatformIO user-profile xPack
first, then openocd on PATH). COM port must be free (close serial monitors).
"""
import os, shutil, serial, subprocess, time, sys

def find_openocd():
    pio = os.path.expanduser(r'~\.platformio\packages\tool-openocd')
    if os.path.isfile(os.path.join(pio, 'bin', 'openocd.exe')):
        return (os.path.join(pio, 'bin', 'openocd.exe'),
                ['-s', os.path.join(pio, 'share', 'openocd', 'scripts')])
    on_path = shutil.which('openocd') or shutil.which('openocd.exe')
    if on_path:
        return (on_path, [])
    sys.exit('[ERROR] openocd not found: install PlatformIO (user profile) or add openocd to PATH')

OOCDBIN, OOCDSCRIPTS = find_openocd()
PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM3'
SECS = int(sys.argv[2]) if len(sys.argv) > 2 else 5

s = serial.Serial(PORT, 115200, timeout=1)
time.sleep(0.3)
s.reset_input_buffer()
subprocess.run([OOCDBIN, *OOCDSCRIPTS,
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
