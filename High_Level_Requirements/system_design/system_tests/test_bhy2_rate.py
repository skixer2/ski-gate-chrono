#!/usr/bin/env python3
"""
S04 — BHY2 real-sensor LOGGING rate test (no USB frame injection).

Purpose:
  Measure encode+LittleFS write rate under real conditions:
    BHY2 sensors → packer → page buffer → LittleFS
  without the USB pull-model bottleneck (~25 fps in stream tests).

Flow (production-like):
  IDLE → (optional) leave test mode OFF
  force LOGGING via serial 'l'  (bypasses start detector)
  wait --duration seconds while device samples BHY2 at 100 Hz tick
  force POST_RUN via 'p'
  read run_saved {fr, dur_ms, fps10}

Pass criteria (defaults):
  fps >= 90  (target 100 Hz with headroom for flash sync)
  frames >= 0.9 * duration * 100

Usage:
  py test_bhy2_rate.py COM8
  py test_bhy2_rate.py COM8 --duration 20
  py test_bhy2_rate.py COM8 --duration 20 -R
  py test_bhy2_rate.py COM8 --min-fps 80
"""

from __future__ import annotations

import argparse
import json
import os
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
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if obj.get("ev") == ev:
                    return obj
        else:
            time.sleep(0.02)
    return None


def send(ser: serial.Serial, cmd: str, wait_s: float = 0.2) -> list[dict]:
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    return read_json_lines(ser, wait_s)


def main() -> int:
    ap = argparse.ArgumentParser(description="S04 — BHY2 real LOGGING rate test")
    ap.add_argument("port", help="Serial port, e.g. COM8")
    ap.add_argument("--duration", type=float, default=20.0, help="LOGGING seconds")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--min-fps", type=float, default=90.0, help="Pass threshold")
    ap.add_argument("-R", "--factory-reset", action="store_true",
                    help="Factory reset before test")
    args = ap.parse_args()

    print("=" * 50)
    print("S04 — BHY2 Real LOGGING Rate Test")
    print(f"  Port:     {args.port}")
    print(f"  Duration: {args.duration:.1f}s")
    print(f"  Min FPS:  {args.min_fps:.1f}")
    print("=" * 50)

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.4)
    ser.reset_input_buffer()

    # Wake / status
    send(ser, "i", 0.3)
    st = send(ser, "?", 0.5)
    ver = None
    for r in st:
        if r.get("ev") == "status":
            ver = r.get("ver")
            print(f"  State: {r.get('st')} batt={r.get('batt')} runs={r.get('runs')} ver={ver}")
        if r.get("ev") == "version" or "ver" in r:
            ver = r.get("ver", ver)
    if ver is None:
        # try V command
        for r in send(ser, "V", 0.3):
            if "ver" in r:
                ver = r.get("ver")
                print(f"  Firmware: v{ver}")

    # Ensure NOT in test mode — real BHY2 path
    # Toggle T until tm is false (best-effort)
    for r in send(ser, "T", 0.3):
        if r.get("ev") == "cmd" and r.get("cmd") == "T":
            tm = r.get("tm")
            print(f"  Test mode after T: {tm}")
            if tm is True:
                for r2 in send(ser, "T", 0.3):
                    if r2.get("ev") == "cmd":
                        print(f"  Test mode toggled to: {r2.get('tm')}")

    if args.factory_reset:
        print("  Factory reset…")
        send(ser, "R", 0.5)
        boot = wait_event(ser, "boot", 8.0)
        print(f"  boot: {boot}")
        time.sleep(1.0)
        send(ser, "i", 0.3)

    # Force LOGGING (bypasses start detector / arm quat checks)
    print(f"\n── Force LOGGING for {args.duration:.1f}s (BHY2 live) ──")
    send(ser, "i", 0.2)
    evs = send(ser, "l", 0.5)
    for r in evs:
        if r.get("ev") in ("st", "run_created"):
            print(f"  {r}")

    t0 = time.perf_counter()
    # Let it run; drain occasionally so USB buffer doesn't fill with noise
    while time.perf_counter() - t0 < args.duration:
        read_json_lines(ser, 0.2)
        elapsed = time.perf_counter() - t0
        if int(elapsed) % 5 == 0 and abs(elapsed - int(elapsed)) < 0.25:
            print(f"  … {elapsed:.0f}s")

    print("── Force POST_RUN ──")
    send(ser, "p", 0.2)
    saved = wait_event(ser, "run_saved", 30.0)
    # collect a bit more
    more = read_json_lines(ser, 2.0)
    if saved is None:
        for r in more:
            if r.get("ev") == "run_saved":
                saved = r
                break

    print("\n── Result ──")
    if not saved:
        print("  ✗ No run_saved event")
        ser.close()
        return 1

    fr = int(saved.get("fr") or 0)
    dur_ms = int(saved.get("dur_ms") or 0)
    fps10 = saved.get("fps10")
    ok = bool(saved.get("ok"))
    rid = saved.get("id")

    if fps10 is not None:
        fps = float(fps10) / 10.0
    elif dur_ms > 0:
        fps = fr * 1000.0 / dur_ms
    else:
        fps = 0.0

    print(f"  run_saved: id={rid} ok={ok} fr={fr} dur_ms={dur_ms} fps={fps:.1f}")
    print(f"  compressed={saved.get('sz')} runs={saved.get('runs')}")

    min_frames = int(0.9 * args.duration * 100)
    pass_fps = fps >= args.min_fps
    pass_fr = fr >= min_frames

    print("\n" + "=" * 50)
    if pass_fps and pass_fr and ok:
        print(f"✓ S04 PASSED — {fps:.1f} fps, {fr} frames in {dur_ms} ms")
        rc = 0
    else:
        print("✗ S04 FAILED")
        if not ok:
            print("  run_saved ok=false")
        if not pass_fps:
            print(f"  fps {fps:.1f} < min {args.min_fps:.1f}")
        if not pass_fr:
            print(f"  frames {fr} < min {min_frames}")
        rc = 1
    if ver:
        print(f"  Firmware: v{ver}")
    print("=" * 50)

    ser.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
