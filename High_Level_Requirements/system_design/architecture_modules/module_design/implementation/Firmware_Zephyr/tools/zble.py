#!/usr/bin/env python3
"""zble.py — SGC bench harness over BLE from the PC (bleak).

The automated-bench seed (JP directive 09-25): scan, connect SGC-Dev,
subscribe the console characteristic 5347ABCA, run ONE command, collect
notifications, and — for 's' streams — reassemble the blob and VERIFY
the CRC32 against the trailer. Full protocol loop with no phone.

Usage: py zble.py "D"          -> directory lines
       py zble.py "w 40960"    -> record (replies are async prints)
       py zble.py "s 0 0 40982" -> stream + CRC verdict
Requires: py -m pip install bleak   (PC Bluetooth on)
"""
import asyncio, sys, zlib

UUID = '5347abca-0000-1000-8000-00805f9b34fb'

frames = []

def on_notify(sender, data):
    frames.append(bytes(data))
    if len(data) >= 3 and data[0] == 0xFF and data[1] == 0xFF:
        return  # end marker handled by the verifier
    sys.stdout.write('  frame seq=%d len=%d\n' % (data[0] | (data[1] << 8), data[2]))
    sys.stdout.flush()

async def main():
    from bleak import BleakScanner, BleakClient
    cmd = sys.argv[1]
    secs = 30 if cmd.startswith('s ') else 8

    dev = await BleakScanner.find_device_by_name('SGC-Dev', timeout=15)
    if dev is None:
        print('SGC-Dev not found (scan timeout)')
        return 1

    async with BleakClient(dev, timeout=15) as client:
        await client.start_notify(UUID, on_notify)
        await client.write_gatt_char(UUID, (cmd + '\n').encode(), response=True)
        await asyncio.sleep(secs)
        await client.stop_notify(UUID)

    if cmd.startswith('s '):
        blob = b''.join(f[3:] for f in frames
                        if len(f) >= 3 and not (f[0] == 0xFF and f[1] == 0xFF))
        ended = any(len(f) == 3 and f[0] == 0xFF for f in frames)
        if not ended:
            print('VERDICT: FAIL (no end marker)')
            return 1
        trailer = blob[-6:]
        payload = blob[:-6]
        ok = (trailer[0] == 0xC3 and trailer[1] == 0x32 and
              int.from_bytes(trailer[2:], 'little') ==
              (zlib.crc32(payload) & 0xFFFFFFFF))
        print('VERDICT: %s (%d B payload, trailer %s)' %
              ('CRC OK' if ok else 'CRC FAIL', len(payload), trailer.hex()))
        return 0 if ok else 1
    return 0

sys.exit(asyncio.run(main()))
