#!/usr/bin/env python3
"""B3 packed-run decoder + physics verification.

Decodes the payload hexdump produced by firmware 'H <id>' (rh lines) and
verifies the ArduinoFW packed-stream format end to end:
  header LE16: type(2b)<<14 | seq(4b)<<10 | delta_ms(10b)
  T1 6 B: 8 x int4 deltas (7 IMU + baro)   T2 10 B: 8 x int8
  T3 18 B: 7 x int16 absolute + uint16 baro_pa_div2

PASS needs: full payload consumed, seq continuity, dt ~10 ms,
|q| within 0.98..1.02, baro 800..1050 hPa, |la| < 2500 mg.

Usage: py decode_packed.py <capture.log>
"""
import math
import re
import sys


def s4(v):
    return v - 16 if v >= 8 else v


def s8(v):
    return v - 256 if v >= 128 else v


def s16(v):
    return v - 65536 if v >= 32768 else v


def main():
    log = open(sys.argv[1], 'r', errors='replace').read()
    data = bytearray()
    bad = 0
    offs = []
    for m in re.finditer(r'\{"ev":"rh","off":(\d+),"hex":"([0-9a-fA-F]+)"\}', log):
        h = m.group(2)
        if len(h) % 2 or len(h) > 48:   # interleaved/truncated line
            bad += 1
            continue
        try:
            data.extend(bytes.fromhex(h))
            offs.append(int(m.group(1)))
        except ValueError:
            bad += 1
    # offset continuity: detect dropped lines
    drops = sum(1 for a, b in zip(offs, offs[1:]) if b - a > 24)
    print(f"rh lines ok={len(offs)} bad={bad} offset-gaps={drops}")
    if drops:
        print("WARNING: dropped rh lines - decode will desync after gap")
    if not data:
        print("no rh lines found")
        return 2
    print(f"payload bytes: {len(data)}")

    pos = 0
    q = [0.0] * 4        # w, x, y, z (floats only for norms)
    qi = [0] * 4         # int16 reconstructions
    la = [0] * 3
    baro_div2 = 0
    ts = 0
    prev_seq = None
    types = {1: 0, 2: 0, 3: 0}
    norms, baros, lamag, dts = [], [], [], []
    seq_err = dt_err = 0

    while pos + 2 <= len(data):
        hdr = data[pos] | (data[pos + 1] << 8)
        ptype = (hdr >> 14) & 3
        seq = (hdr >> 10) & 0xF
        dms = hdr & 0x3FF
        pos += 2

        if prev_seq is not None and (seq - prev_seq) & 0xF != 1:
            seq_err += 1
        prev_seq = seq
        ts += dms
        dts.append(dms)

        if ptype == 3:
            if pos + 16 > len(data):
                break
            v = [s16(data[pos + 2 * i] | (data[pos + 2 * i + 1] << 8))
                 for i in range(7)]
            qi = [v[0], v[1], v[2], v[3]]          # w, x, y, z
            la = [v[4], v[5], v[6]]
            baro_div2 = data[pos + 14] | (data[pos + 15] << 8)
            pos += 16
        elif ptype in (1, 2):
            n = 4 if ptype == 1 else 8
            if pos + n > len(data):
                break
            if ptype == 1:
                nib = []
                for b in data[pos:pos + 4]:
                    nib.append(s4(b >> 4))
                    nib.append(s4(b & 0xF))
                d = nib
            else:
                d = [s8(b) for b in data[pos:pos + 8]]
            pos += n
            qi = [qi[i] + d[i] for i in range(4)]
            la = [la[i] + d[4 + i] for i in range(3)]
            baro_div2 += d[7]

        types[ptype] += 1
        q = [v / 16384.0 for v in qi]
        norms.append(math.sqrt(sum(x * x for x in q)))
        baros.append(baro_div2 * 2.0 / 100.0)       # Pa/2 -> hPa
        lamag.append(math.sqrt(sum(x * x for x in la)))

    n = sum(types.values())
    avg = len(data) / n if n else 0
    print(f"frames: {n}  T1/T2/T3: {types[1]}/{types[2]}/{types[3]}")
    print(f"avg bytes/frame: {avg:.2f}   payload consumed: {pos}/{len(data)}")
    print(f"seq errors: {seq_err}   dt!=10ms count: "
          f"{sum(1 for d in dts if d != 10)} (of {len(dts)})")
    print(f"|q|  min/mean/max: {min(norms):.4f}/{sum(norms)/len(norms):.4f}/{max(norms):.4f}")
    print(f"baro min/max: {min(baros):.1f}/{max(baros):.1f} hPa")
    print(f"|la| max: {max(lamag):.0f} mg (mean {sum(lamag)/len(lamag):.0f})")
    print(f"duration from ts deltas: {ts/1000.0:.1f} s")

    ok = (pos == len(data) and n >= 100 and seq_err == 0
          and all(abs(d - 10) <= 1 for d in dts)
          and 0.98 <= min(norms) and max(norms) <= 1.02
          and 800 <= min(baros) and max(baros) <= 1050
          and max(lamag) < 2500 and avg <= 8.5)
    print("VERDICT:", "PASS - stored data decodes with sound physics" if ok
          else "CHECK ABOVE - something is off")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
