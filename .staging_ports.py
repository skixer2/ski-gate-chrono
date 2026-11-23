import serial.tools.list_ports
for p in sorted(serial.tools.list_ports.comports()):
    print(p.device, "|", p.description)
