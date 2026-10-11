#!/usr/bin/env python3
"""sgc_gate_sim.py — host port of the app gate pipeline (offline prediction).

Parses an 'H <id>' serial dump (rh hex lines), decodes packed frames, then
runs the App's algorithms (impact_detector.dart + gate_time_estimator.dart
Bronze tier, App 1.53 semantics):
  impacts: |la| in g, median baseline (50), threshold max(b*2.5, 1.8 g),
           cooldown 200 ms, 7-sample local peak
  omega:   2*acos(|q_i . q_{i-1}|)/dt, EMA alpha=0.1
  quiet:   20-sample moving avg < 0.3 rad/s -> center = turn boundary
  assoc:   impact-first; containment +/-200 ms else nearest center <=1500;
           claimed windows consumed; fallback = apex(argmax omega)+150 ms
  side:    mean laY over window (<0 -> right)
Prints the predicted gate list; optionally diffs against synth truth lines
from a capture log ({"synth":"impact",...}).
Usage: py sgc_gate_sim.py <run_dump.log> [serial_capture.log]
"""
import math, re, sys

def s4(v): return v - 16 if v >= 8 else v
def s8(v): return v - 256 if v >= 128 else v
def s16(v): return v - 65536 if v >= 32768 else v

def frames_from_dump(path):
    data = bytearray()
    for m in re.finditer(r'\{"ev":"rh","off":(\d+),"hex":"([0-9a-fA-F]+)"\}', open(path, errors='replace').read()):
        h = m.group(2)
        if len(h) % 2 == 0 and len(h) <= 48:
            data.extend(bytes.fromhex(h))
    pos, q, la, baro, ts, out = 0, [0]*4, [0]*3, 0, 0, []
    while pos + 2 <= len(data):
        hdr = data[pos] | (data[pos+1] << 8); pos += 2
        ptype, seq, dms = (hdr >> 14) & 3, (hdr >> 10) & 0xF, hdr & 0x3FF
        ts += dms
        if ptype == 2:
            if pos + 16 > len(data): break
            v = [s16(data[pos+2*i] | (data[pos+2*i+1] << 8)) for i in range(7)]
            q, la, baro = v[0:4], v[4:7], data[pos+14] | (data[pos+15] << 8); pos += 16
        else:
            n = 4 if ptype == 0 else 8
            if pos + n > len(data): break
            if ptype == 0:
                d = []
                for b in data[pos:pos+4]: d += [s4(b >> 4), s4(b & 0xF)]
            else:
                d = [s8(b) for b in data[pos:pos+8]]
            pos += n
            q = [q[i] + d[i] for i in range(4)]
            la = [la[i] + d[4+i] for i in range(3)]
            baro += d[7]
        out.append((ts, q, la, baro))
    return out

G = 9810.0

def detect_impacts(fr):
    mags = [math.sqrt(l[0]**2 + l[1]**2 + l[2]**2) / G for (_, _, l, _) in fr]
    imps, last = [], -20
    for i in range(50, len(fr)):
        w = sorted(mags[i-50:i])
        base = w[0]   # noise floor, not median (turn plateau guard)
        thr = max(base * 2.5, 1.8)
        if mags[i] > thr and i - last >= 20:
            pk, pj = mags[i], i
            for j in range(max(0, i-3), min(len(fr), i+4)):
                if mags[j] > pk: pk, pj = mags[j], j
            imps.append((fr[pj][0], pk)); last = pj
    return imps

def omega_series(fr):
    om = []
    for i in range(1, len(fr)):
        a, b = fr[i-1][1], fr[i][1]
        dot = abs(sum(a[k]*b[k] for k in range(4)) / 16384.0**2)
        dot = min(1.0, max(0.0, dot))
        om.append(2 * math.acos(dot) / 0.010)
    return om

def quiet_centers(omf):
    zs, below, W, TH = [], None, 20, 0.3
    for i in range(W, len(omf)):
        avg = sum(omf[i-W:i]) / W
        if avg < TH:
            below = i if below is None else below
        elif below is not None:
            zs.append(((below + i)//2) * 10); below = None
    return zs

def estimate(fr, imps, omf):
    zs = quiet_centers(omf)
    idx_of_ms = lambda ms: min(len(fr)-1, ms // 10)
    turns = []
    for z0, z1 in zip(zs, zs[1:]):
        ys = [fr[t][2][1] for t in range(idx_of_ms(z0), idx_of_ms(z1)+1)]
        side = 'R' if (sum(ys)/max(1,len(ys))) < 0 else 'L'
        apex_ms, apex_w = z0, -1.0
        for t in range(idx_of_ms(z0), idx_of_ms(z1)+1):
            if t < len(omf) and omf[t] > apex_w: apex_w, apex_ms = omf[t], fr[t][0]
        turns.append(dict(z0=z0, z1=z1, c=(z0+z1)//2, apex=apex_ms, side=side))
    claimed = [False]*len(turns)
    gates = []
    for ms, f in imps:
        best, bd, bc = -1, 1 << 30, False
        for j, t in enumerate(turns):
            if claimed[j]: continue
            cont = t['z0']-200 <= ms <= t['z1']+200
            dist = abs(ms - t['c'])
            if cont and not bc: best, bd, bc = j, dist, True
            elif cont and dist < bd: best, bd = j, dist
            elif not cont and not bc and dist <= 1500 and dist < bd: best, bd = j, dist
        side = turns[best]['side'] if best >= 0 else ('R' if fr[idx_of_ms(ms)][2][1] < 0 else 'L')
        if best >= 0: claimed[best] = True
        gates.append((ms, side, 'C', f))
    for j, t in enumerate(turns):
        if not claimed[j]:
            gates.append((t['apex']+150, t['side'], '*', None))
    gates.sort()
    return gates

def blend(ga, gb, win=800):
    """Two-device merge (JP 2026-10-11): impacts from EITHER device
    confirm a gate; estimates within `win` of the other device's impact
    become confirmed; estimate-estimate pairs collapse (mean time)."""
    allg = sorted(ga + gb, key=lambda g: g[0])
    out = []
    for g in allg:
        if out and abs(g[0] - out[-1][0]) <= win:
            ms, side, kind, f = out[-1]
            if kind == 'C' or g[2] == 'C':
                keep = g if g[2] == 'C' else out[-1]
                if kind == 'C' and g[2] == 'C':
                    keep = g if (g[3] or 0) >= (f or 0) else out[-1]
                out[-1] = keep
            else:
                out[-1] = ((ms + g[0]) // 2, side, '*', None)
        else:
            out.append(g)
    return out


if __name__ == '__main__':
    fr = frames_from_dump(sys.argv[1])
    imps = detect_impacts(fr)
    omf = []
    a = None
    for x in omega_series(fr):
        a = x if a is None else 0.1*x + 0.9*a
        omf.append(a)
    gates = estimate(fr, imps, omf)

    if len(sys.argv) >= 3 and sys.argv[2].endswith('_dump.log'):
        # BLEND MODE: two device dumps -> two-device gate list.
        fr2 = frames_from_dump(sys.argv[2])
        imps2 = detect_impacts(fr2)
        omf2 = []
        a2 = None
        for x in omega_series(fr2):
            a2 = x if a2 is None else 0.1 * x + 0.9 * a2
            omf2.append(a2)
        gates2 = estimate(fr2, imps2, omf2)
        gatesA = gates

        # Device-side self-deduction (JP 2026-10-11): the outside leg
        # blocks, so a device's impacts correlate with ONE turn
        # direction (= its la_y centripetal sign at impact times).
        def lateral_sign_at(frames, imps_):
            signs = []
            for ms, _f in imps_:
                i = min(len(frames)-1, ms // 10)
                ys = [frames[t][2][1] for t in range(max(0, i-15), min(len(frames), i+5))]
                if ys:
                    signs.append(1 if sum(ys) > 0 else -1)
            return signs

        for name, frs, im in (('A', fr, imps), ('B', fr2, imps2)):
            sg = lateral_sign_at(frs, im)
            pos = sum(1 for x in sg if x > 0)
            print(f"device {name}: {len(sg)} impacts, la_y sign at impact: "
                  f"{pos}(+)/{len(sg)-pos}(-) -> "
                  f"{'+Y-turn device' if pos > len(sg)-pos else '-Y-turn device'}")

        gates = blend(gates, gates2)
        print(f"deviceA: {len(gatesA)} gates ({sum(1 for g in gatesA if g[2]=='C')} confirmed)")
        print(f"deviceB: {len(gates2)} gates ({sum(1 for g in gates2 if g[2]=='C')} confirmed)")
        print(f"frames={len(fr)}+{len(fr2)} impacts={len(imps)}+{len(imps2)}; BLENDED gates:")
    else:
        print(f"frames={len(fr)} impacts={len(imps)} quiet_centers->turns; predicted gates:")
    for n, (ms, side, kind, f) in enumerate(gates, 1):
        fs = f" force={f:.2f}g" if f else ""
        print(f"  gate {n:2d}  t={ms/1000:6.2f}s  {side}  {kind}{fs}")
    if len(sys.argv) > 2 and not sys.argv[2].endswith('_dump.log'):
        truth = [(int(m.group(1)), int(m.group(2)) + 2000)
                 for m in re.finditer(r'"synth":"impact","n":(\d+),"t_ms":(\d+)', open(sys.argv[2], errors='replace').read())]
        det = [ms for ms, s, k, f in gates if k == 'C']
        print("truth impacts:", [t for _, t in truth])
        print("detected     :", det)
        for n, (k, t) in enumerate(truth):
            near = min(det, default=None, key=lambda d: abs(d - t))
            ok = near is not None and abs(near - t) <= 60
            print(f"  impact {k}: truth {t} ms -> detected {near} ms  {'OK' if ok else 'MISS/OFF'}")
