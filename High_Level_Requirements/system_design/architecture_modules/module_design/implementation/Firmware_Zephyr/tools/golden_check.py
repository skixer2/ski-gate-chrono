#!/usr/bin/env python3
"""B2 golden-vector checker: firmware 'gv' lines vs Arduino_BHY2 reference decode.

Reference semantics (bench_bhy_test/lib/Arduino_BHY2/src/sensors/DataParser.cpp,
verified 2026-10-02): payload at data_ptr+0 --
  RV  (34):  int16 x,y,z,w Q14 @0,2,4,6 + uint16 accuracy @8
  LACC(31):  int16 x,y,z @0,2,4 (mg)
  BARO(129): uint24 @0 x 0.0078 -> hPa
  TEMP(128): int16 @0 x 0.01 -> degC

Usage: py golden_check.py <capture.log> [out.jsonl]
PASS: >=12 lines, all exact-match, all 4 sensors, all physics checks green.
"""
import json
import re
import sys


def s16(b, o):
    v = b[o] | (b[o + 1] << 8)
    return v - 65536 if v >= 32768 else v


def u24(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)


def ref_decode(sid, b):
    if sid == 34:
        return [s16(b, 0), s16(b, 2), s16(b, 4), s16(b, 6), s16(b, 8)]
    if sid == 31:
        return [s16(b, 0), s16(b, 2), s16(b, 4)]
    if sid == 129:
        return [u24(b, 0)]
    if sid == 128:
        return [s16(b, 0)]
    return None


def main():
    log = open(sys.argv[1], 'r', errors='replace').read()
    out = open(sys.argv[2], 'w') if len(sys.argv) > 2 else None
    rows = []
    pat = re.compile(r'\{"ev":"gv","sid":(\d+),"hex":"([0-9a-f]+)","v":\[([-\d,]*)\]\}')
    for m in pat.finditer(log):
        sid = int(m.group(1))
        b = bytes.fromhex(m.group(2))
        fw = [int(x) for x in m.group(3).split(',') if x != '']
        ref = ref_decode(sid, b[1:])          # b[0]=SID, payload=b[1:]
        ok = ref is not None and fw == ref
        rows.append({'sid': sid, 'hex': m.group(2), 'fw': fw, 'ref': ref, 'match': ok})
        if out:
            out.write(json.dumps(rows[-1]) + '\n')
    if out:
        out.close()

    n = len(rows)
    good = sum(1 for r in rows if r['match'])
    sids = sorted(set(r['sid'] for r in rows))
    print(f"golden lines: {n}  exact matches: {good}")
    print(f"sensors covered: {sids}  (need [31, 34, 128, 129])")

    phys = []
    for r in rows:
        if not r['match']:
            continue
        if r['sid'] == 34:
            x, y, z, w, a = [v / 16384.0 for v in r['ref']]
            phys.append((abs((x * x + y * y + z * z + w * w) ** 0.5 - 1.0) < 0.01,
                         'quat |q|~1'))
        elif r['sid'] == 129:
            hpa = r['ref'][0] * 0.0078
            phys.append((800.0 <= hpa <= 1050.0, f'baro {hpa:.1f} hPa in 800-1050'))
        elif r['sid'] == 128:
            phys.append((-1000 <= r['ref'][0] <= 6000,
                         f'temp {r["ref"][0] * 0.01:.2f} C in -10..60'))
    bad = [why for ok, why in phys if not ok]
    print(f"physics checks: {sum(1 for ok, _ in phys if ok)}/{len(phys)} pass")
    for why in bad:
        print(f"  PHYS FAIL: {why}")

    verdict = (n >= 12 and good == n and sids == [31, 128, 129, 34]
               and len(phys) == sum(1 for ok, _ in phys if ok) and not bad)
    print("VERDICT:", "PASS - golden vectors locked" if verdict
          else "INCOMPLETE/MISMATCH - see above")


if __name__ == '__main__':
    main()
