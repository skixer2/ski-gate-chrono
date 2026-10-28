#!/usr/bin/env python3
"""flashlisten.py — flash + capture the boot banner in one shot.

Usage: py flashlisten.py <tag> [hex-path]
  Opens COM3 FIRST (the post-flash reset-run banner is only ~2 s),
  runs zflash.bat via subprocess (SWD does not own the CDC port),
  then streams 25 s of console to zflash_boot_<tag>.log and stdout.
  Purpose: verify boot lines like 'charger was PAUSED - restored'
  without racing a monitor against the reboot.
"""
import serial, subprocess, sys, time, os

TAG = sys.argv[1] if len(sys.argv) > 1 else 'untagged'
HEX = sys.argv[2] if len(sys.argv) > 2 else None
HERE = os.path.dirname(os.path.abspath(__file__))
if not HEX:
    HEX = r"C:\sgc\build\%s\03_pull_proto\zephyr\zephyr.hex" % TAG
LOG = r"C:\Users\v17ni\zio_claw\sgc_bench\logs\zflash_boot_%s.log" % TAG

s = serial.Serial("COM3", 115200, timeout=1)
s.reset_input_buffer()
r = subprocess.run(["cmd", "/c", os.path.join(HERE, "zflash.bat"), HEX],
                   capture_output=True, timeout=180, text=True)
boot = []
end = time.time() + 25
while time.time() < end:
    d = s.readline()
    if d:
        line = d.decode(errors="replace").rstrip()
        boot.append(line)
        print(line)
s.close()
open(LOG, "w").write("\n".join(boot))
print("FLASH_EXIT_%d" % r.returncode)
