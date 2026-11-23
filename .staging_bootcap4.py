#!/usr/bin/env python3
"""bootcap4.py - boot capture on COM4 with in-process SWD reset.
Same as zbootcap.py but port is a parameter (zbootcap hardcodes COM3).
Usage: bootcap4.py <out> [secs] [port]"""
import subprocess, sys, time
import serial

out = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\v17ni\zio_claw\sgc_bench\bootcap4.txt"
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 15
port = sys.argv[3] if len(sys.argv) > 3 else "COM4"

OOCD = r"C:\Users\v17ni\.platformio\packages\tool-openocd\bin\openocd.exe"
SCR = r"-s C:\Users\v17ni\.platformio\packages\tool-openocd\share\openocd\scripts"

ser = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.5)
ser.reset_input_buffer()

p = subprocess.Popen([OOCD, SCR, "-f", "interface/cmsis-dap.cfg",
                      "-c", "transport select swd", "-f", "target/nordic/nrf52.cfg",
                      "-c", "init", "-c", "reset run", "-c", "shutdown"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

t0 = time.time()
buf = []
while time.time() - t0 < secs:
    line = ser.readline().decode(errors="replace").rstrip()
    if line:
        buf.append(line)
ser.close()
try:
    p.wait(timeout=5)
except Exception:
    p.kill()

with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(buf))
print(f"BOOTCAP_DONE {len(buf)} lines -> {out}")
