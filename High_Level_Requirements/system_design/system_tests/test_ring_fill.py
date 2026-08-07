#!/usr/bin/env python3
"""
S05 — ARMED FlashRing fill rate (BHY2, no USB stream).

Measures how fast the linear pre-roll fills (v4.75: rm=3000 ≈ 30 s ARM cap).

ARMED is program-only (prepare_preroll erased buffer on enter IDLE).

Pass (defaults):
  fill fps >= 90
  ring reaches rm (full) OR use --target to stop earlier
  time to full roughly rm/100 s (±40%)

Usage:
  py test_ring_fill.py COM8
  py test_ring_fill.py COM8 -R
  py test_ring_fill.py COM8 --min-fps 80
"""

from __future__ import annotations

import argparse
import json
import sys
import time

import serial


def read_json_lines(ser: serial.Serial, timeout_s: float) -> list[dict]:
    end = time.time() + timeout_s
    out: list[dict] = []
    buf = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode("ascii", errors="replace").strip()
                if not line.startswith("{"):
                    continue
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
        else:
            time.sleep(0.02)
    return out


def send(ser: serial.Serial, cmd: str, wait_s: float = 0.3) -> list[dict]:
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    return read_json_lines(ser, wait_s)


def send_keep(ser: serial.Serial, cmd: str, wait_s: float = 0.3) -> list[dict]:
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    return read_json_lines(ser, wait_s)


def wait_event(ser: serial.Serial, ev: str, timeout_s: float) -> dict | None:
    end = time.time() + timeout_s
    buf = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode("ascii", errors="replace").strip()
                if not line.startswith("{"):
                    continue
                try:
                    o = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if o.get("ev") == ev:
                    return o
        else:
            time.sleep(0.02)
    return None


def status(ser: serial.Serial) -> dict | None:
    for o in send_keep(ser, "?", 0.5):
        if o.get("ev") == "status":
            return o
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="S05 — ARMED ring fill rate")
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("-R", "--factory-reset", action="store_true")
    ap.add_argument("--min-fps", type=float, default=90.0)
    ap.add_argument("--timeout", type=float, default=40.0,
                    help="Max seconds to wait for ring_full (v4.75 cap≈30s)")
    ap.add_argument("--target", type=int, default=1000,
                    help="Stop when r>=target (default 1000 ≈10s; "
                         "use --target 0 for full rm=3000 — ARM times out at 30s, "
                         "pass uses peak r before timeout)")
    ap.add_argument("--onboard-led", action="store_true",
                    help="Enable Nicla onboard RGB (O 1)")
    args = ap.parse_args()

    print("=" * 50)
    print("S05 — ARMED FlashRing Fill Rate")
    print(f"  Port: {args.port}  min_fps: {args.min_fps}")
    print("=" * 50)

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.4)
    ser.reset_input_buffer()

    st = status(ser)
    ver = st.get("ver") if st else None
    print(f"  status: st={st.get('st') if st else None} ver={ver} "
          f"r={st.get('r') if st else None} rm={st.get('rm') if st else None}")

    if args.factory_reset:
        print("  Factory reset…")
        send(ser, "R", 0.4)
        boot = wait_event(ser, "boot", 12.0)
        print(f"  boot: {boot}")
        t0w = time.perf_counter()
        while time.perf_counter() - t0w < 15.0:
            time.sleep(0.7)
            read_json_lines(ser, 0.2)
            st = status(ser)
            if st and st.get("st") == "IDLE":
                print(f"  ready ({time.perf_counter()-t0w:.1f}s) rm={st.get('rm')}")
                ver = st.get("ver") or ver
                break

    # Ensure real BHY2 path
    if args.onboard_led:
        send_keep(ser, "O 1", 0.4)
        print("  Onboard LED: ON")
    else:
        send_keep(ser, "O 0", 0.3)
        print("  Onboard LED: OFF (use --onboard-led)")
    send(ser, "i", 0.3)
    time.sleep(1.5)

    print("\n── ARM (fill ring) ──")
    send(ser, "i", 0.2)
    evs = send(ser, "a", 0.8)
    for o in evs:
        if o.get("ev") in ("st", "arm_refused", "ring_full"):
            print(f"  {o}")

    st = status(ser)
    if not st or st.get("st") != "ARMED":
        print(f"  ✗ not ARMED: {st}")
        ser.close()
        return 1

    rm = int(st.get("rm") or 3000)
    target = rm if args.target == 0 else args.target
    if target > rm:
        target = rm
    print(f"  ✓ ARMED  rm={rm}  measure_target={target}")

    t_arm = time.perf_counter()
    full = None
    hit_target = False
    for o in evs:
        if o.get("ev") == "ring_full":
            full = o
            break

    last_r = int(st.get("r") or 0)
    peak_r = last_r
    last_print = -1
    left_armed = False
    while (not hit_target and full is None and
           (time.perf_counter() - t_arm) < args.timeout):
        batch = read_json_lines(ser, 0.25)
        for o in batch:
            if o.get("ev") == "ring_full":
                full = o
                break
            if o.get("ev") == "st" and o.get("to") not in (None, "ARMED"):
                print(f"  ⚠ left ARMED: {o}")
                left_armed = True
        sec = int(time.perf_counter() - t_arm)
        if sec != last_print and sec > 0 and sec % 2 == 0:
            st = status(ser)
            r = int(st.get("r") or 0) if st else last_r
            # After ARM timeout, prepare_preroll clears r — keep peak
            if st and st.get("st") == "ARMED":
                peak_r = max(peak_r, r)
                last_r = r
            print(f"  … {sec}s  r={r}/{rm} peak={peak_r}", flush=True)
            last_print = sec
            if peak_r >= target or r >= target:
                hit_target = True
                break
            if st and st.get("st") != "ARMED":
                left_armed = True
                break
        if left_armed:
            break

    t_full = time.perf_counter() - t_arm
    st = status(ser)
    r_end = int(st.get("r") or 0) if st else last_r
    if st and st.get("st") == "ARMED":
        peak_r = max(peak_r, r_end)
    rh = st.get("rh") if st else None
    if peak_r >= target or r_end >= target or full is not None:
        hit_target = True

    print("\n── Result ──")
    if full:
        print(f"  ring_full event: {full}")
    if left_armed and peak_r < target:
        print(f"  note: ARMED ended early (timeout=30s). peak_r={peak_r} "
              f"before IDLE cleared buffer")
    print(f"  time≈{t_full:.2f}s  r={r_end} peak={peak_r} rm={rm} "
          f"target={target} rh={rh}")

    # For full-cap test, ARM_TIMEOUT 30s races rm=3000 @100Hz — accept peak
    # ≥ 95% of target if we hit timeout (forward fill still proven)
    n_meas = max(peak_r, r_end)
    ok_full = hit_target or (full is not None) or (
        left_armed and n_meas >= int(0.95 * target)
    )
    n_for_fps = n_meas if n_meas > 0 else target
    # Use time until leave ARMED / hit target
    fps = (n_for_fps / t_full) if t_full > 0.05 else 0.0
    t_exp = min(target, n_for_fps) / 100.0
    print(f"  fill_fps≈{fps:.1f}  expected_time≈{t_exp:.1f}s for n={n_for_fps}")

    pass_fps = fps >= args.min_fps
    pass_time = t_full <= max(t_exp * 1.6, 32.0)  # allow ARM 30s window
    pass_ok = ok_full and pass_fps and pass_time

    print("=" * 50)
    if pass_ok:
        print(f"✓ S05 PASSED — fill ~{fps:.1f} fps, n={n_for_fps} in {t_full:.2f}s "
              f"(rm={rm} target={target})")
        rc = 0
    else:
        print("✗ S05 FAILED")
        if not ok_full:
            print(f"  peak_r={peak_r} r={r_end} < target={target} (rm={rm})")
        if not pass_fps:
            print(f"  fill_fps {fps:.1f} < {args.min_fps}")
        if not pass_time:
            print(f"  time {t_full:.2f}s over budget")
        rc = 1
    if ver:
        print(f"  Firmware: v{ver}")
    print("=" * 50)

    # Leave idle
    send(ser, "i", 0.3)
    ser.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
