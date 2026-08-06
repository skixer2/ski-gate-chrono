#!/usr/bin/env python3
"""
S04 — BHY2 real-sensor LOGGING rate test (no USB frame injection).

Purpose:
  Measure real BHY2 LOGGING sample rate (encode + flash write).

  v4.62+ force-'l' Opt-A spike path:
    BHY2 → packer → page buf → raw SPI program (pre-erased 48 KB slab)
    NO LittleFS on the hot path. run_saved.store == "raw".

  Production (start-detector) still uses LittleFS (store == "lfs").

IMPORTANT state machine rule (firmware):
  LOGGING may only be entered from ARMED.
  POST_RUN may only be entered from LOGGING.
  So the bench sequence is: IDLE → a(ARMED) → l(LOGGING) → … → p(POST_RUN)

NOTE (v4.58+):
  Serial 'l' force-LOGGING disables the end detector so a stationary bench
  device is not auto-closed after ~5 s of flat pressure. Production runs
  (start-detector entry) still use end detection.

Flow:
  - factory reset optional (-R)
  - ensure test mode OFF (real BHY2 path, g_stream_active stays false)
  - arm with 'a'  → expect green LED / st ARMED
  - force LOGGING with 'l' → expect red LED / run_created
  - wait --duration seconds (capture any early run_saved/end)
  - force POST_RUN with 'p' → expect run_saved {fr, dur_ms, fps10}

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


def wait_event(ser: serial.Serial, ev: str, timeout_s: float,
               extra_keys: dict | None = None) -> dict | None:
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
                if obj.get("ev") != ev:
                    continue
                if extra_keys:
                    ok = all(obj.get(k) == v for k, v in extra_keys.items())
                    if not ok:
                        continue
                return obj
        else:
            time.sleep(0.02)
    return None


def send(ser: serial.Serial, cmd: str, wait_s: float = 0.25) -> list[dict]:
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    return read_json_lines(ser, wait_s)


def send_keep(ser: serial.Serial, cmd: str, wait_s: float = 0.25) -> list[dict]:
    """Like send(), but do NOT wipe the RX buffer (keeps pending events)."""
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    return read_json_lines(ser, wait_s)


def ensure_test_mode_off(ser: serial.Serial, attempts: int = 4) -> bool:
    """Return True if test mode is OFF. Retries after boot when T is silent."""
    tm = None
    last_batch: list[dict] = []
    for i in range(attempts):
        # Longer first listens after factory reset / boot
        wait = 1.2 if i == 0 else 0.6
        r = send_keep(ser, "T", wait)
        last_batch = r
        for o in r:
            if o.get("ev") == "cmd" and o.get("cmd") == "T":
                tm = o.get("tm")
                break
        if tm is not None:
            break
        time.sleep(0.4)

    if tm is None:
        print("  ⚠ could not read test-mode flag from T response:", last_batch[:5])
        # Fallback: status path — if device answers ?, assume default tm=0
        st = send_keep(ser, "?", 0.6)
        for o in st:
            if o.get("ev") == "status":
                print(f"  fallback status ok (assume tm=0): st={o.get('st')}")
                return True
        return False

    if tm is True or tm == 1:
        r2 = send_keep(ser, "T", 0.6)
        for o in r2:
            if o.get("ev") == "cmd" and o.get("cmd") == "T":
                tm = o.get("tm")
                break

    print(f"  Test mode OFF: {tm is False or tm == 0} (tm={tm})")
    return tm is False or tm == 0


def find_state(events: list[dict]) -> str | None:
    for o in reversed(events):
        if o.get("ev") == "st" and o.get("to"):
            return o.get("to")
        if o.get("ev") == "status" and o.get("st"):
            return o.get("st")
    return None


def evaluate_run(saved: dict, duration_s: float, min_fps: float, ver: str | None) -> int:
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

    store = saved.get("store", "?")
    print(f"  run_saved: id={rid} ok={ok} fr={fr} dur_ms={dur_ms} fps={fps:.1f}")
    print(f"  store={store} compressed={saved.get('sz')} runs={saved.get('runs')} "
          f"we={saved.get('we')}")
    if store != "raw":
        print("  ⚠ expected store=raw on force-l (v4.62+ Opt-A spike); "
              "got LFS path — flash may be old firmware")

    # Require the full requested window (allow 10% short on duration).
    min_dur_ms = int(0.9 * duration_s * 1000)
    min_frames = int(0.9 * duration_s * 100)
    pass_fps = fps >= min_fps
    pass_fr = fr >= min_frames
    pass_dur = dur_ms >= min_dur_ms

    print("\n" + "=" * 50)
    if pass_fps and pass_fr and pass_dur and ok:
        print(f"✓ S04 PASSED — {fps:.1f} fps, {fr} frames in {dur_ms} ms")
        rc = 0
    else:
        print("✗ S04 FAILED")
        if not ok:
            print("  run_saved ok=false")
        if not pass_fps:
            print(f"  fps {fps:.1f} < min {min_fps:.1f}")
        if not pass_fr:
            print(f"  frames {fr} < min {min_frames}")
        if not pass_dur:
            print(f"  dur_ms {dur_ms} < min {min_dur_ms} "
                  f"(ended early? end-detector / force-l unsupported on this FW?)")
            if dur_ms > 0 and fr > 0:
                print(f"  note: short-run rate still ~{fps:.1f} fps "
                      f"— useful, but S04 needs full {duration_s:.0f}s window")
        rc = 1
    if ver:
        print(f"  Firmware: v{ver}")
    print("=" * 50)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description="S04 — BHY2 real LOGGING rate test")
    ap.add_argument("port", help="Serial port, e.g. COM8")
    ap.add_argument("--duration", type=float, default=20.0, help="LOGGING seconds")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--min-fps", type=float, default=90.0, help="Pass threshold")
    ap.add_argument("-R", "--factory-reset", action="store_true",
                    help="Factory reset before test")
    ap.add_argument("--arm-wait", type=float, default=2.0,
                    help="Seconds to wait after boot for BHY2 quat settle before arm")
    args = ap.parse_args()

    print("=" * 50)
    print("S04 — BHY2 Real LOGGING Rate Test")
    print(f"  Port:     {args.port}")
    print(f"  Duration: {args.duration:.1f}s")
    print(f"  Min FPS:  {args.min_fps:.1f}")
    print("=" * 50)

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.5)
    ser.reset_input_buffer()

    # Wake
    send(ser, "i", 0.3)
    st = send(ser, "?", 0.5)
    ver = None
    for r in st:
        if r.get("ev") == "status":
            ver = r.get("ver")
            print(f"  State: {r.get('st')} batt={r.get('batt')} "
                  f"runs={r.get('runs')} ver={ver}")
    if ver is None:
        for r in send(ser, "V", 0.3):
            if r.get("ver") is not None:
                ver = r.get("ver")
                print(f"  Firmware: v{ver}")

    if args.factory_reset:
        print("  Factory reset…")
        send(ser, "R", 0.5)
        boot = wait_event(ser, "boot", 10.0)
        print(f"  boot: {boot}")
        if boot and boot.get("ver"):
            ver = boot.get("ver")
        # Boot continues with flash/LFS/BLE/BHY2 init — give it time before T
        print("  Waiting 2.5s for post-boot init…")
        time.sleep(2.5)
        read_json_lines(ser, 0.5)
        send_keep(ser, "i", 0.4)

    # Real BHY2 path — must NOT be in test/stream mode
    if not ensure_test_mode_off(ser):
        print("  ✗ Could not ensure test mode OFF")
        ser.close()
        return 1

    # Give BHY2 a moment so arm quat check can pass
    if args.arm_wait > 0:
        print(f"  Waiting {args.arm_wait:.1f}s for BHY2 settle…")
        time.sleep(args.arm_wait)
        read_json_lines(ser, 0.2)

    # ── IDLE → ARMED ───────────────────────────────────────────
    print("\n── ARM (required before LOGGING) ──")
    send(ser, "i", 0.2)
    arm_evs = send(ser, "a", 0.8)
    for r in arm_evs:
        ev = r.get("ev")
        if ev in ("st", "arm_refused", "arm_blocked", "state_blocked", "run_created"):
            print(f"  {r}")

    st_after_arm = find_state(arm_evs)
    # Confirm with ?
    for r in send(ser, "?", 0.4):
        if r.get("ev") == "status":
            st_after_arm = r.get("st", st_after_arm)
            print(f"  status: st={st_after_arm}")

    if st_after_arm != "ARMED":
        print(f"  ✗ Not ARMED (st={st_after_arm}). "
              f"Cannot enter LOGGING from IDLE — SM requires ARMED.")
        print("  Tip: if arm_refused quat_magnitude, wait longer / check BHY2.")
        ser.close()
        return 1
    print("  ✓ ARMED (expect green LED)")

    # ── ARMED → LOGGING ────────────────────────────────────────
    print(f"\n── LOGGING for {args.duration:.1f}s (BHY2 live) ──")
    log_evs = send(ser, "l", 0.8)
    for r in log_evs:
        ev = r.get("ev")
        if ev in ("st", "run_created", "state_blocked", "enc_baro"):
            print(f"  {r}")

    st_log = find_state(log_evs)
    for r in send(ser, "?", 0.4):
        if r.get("ev") == "status":
            st_log = r.get("st", st_log)
            print(f"  status: st={st_log}")

    if st_log != "LOGGING":
        print(f"  ✗ Not LOGGING (st={st_log})")
        ser.close()
        return 1
    print("  ✓ LOGGING (expect red LED)")

    t0 = time.perf_counter()
    last_print = -1
    saved = None
    early_end = None
    while time.perf_counter() - t0 < args.duration:
        batch = read_json_lines(ser, 0.25)
        for r in batch:
            ev = r.get("ev")
            if ev == "run_saved" and saved is None:
                saved = r
                print(f"  evt: {r}")
            elif ev == "end":
                early_end = r
                print(f"  evt: {r}")
            elif ev in ("state_blocked", "timeout", "st"):
                print(f"  evt: {r}")
        # If end detector already closed the run, no point waiting full duration
        if saved is not None:
            print("  ⚠ run_saved during LOGGING window (early close)")
            break
        elapsed = int(time.perf_counter() - t0)
        if elapsed != last_print and elapsed % 5 == 0:
            print(f"  … {elapsed}s")
            last_print = elapsed

    # ── LOGGING → POST_RUN (only if still logging) ─────────────
    if saved is None:
        print("── POST_RUN ──")
        post_evs = send_keep(ser, "p", 0.5)
        for r in post_evs:
            if r.get("ev") in ("st", "run_saved", "state_blocked", "stream_end", "end"):
                print(f"  {r}")
            if r.get("ev") == "run_saved" and saved is None:
                saved = r
        if saved is None:
            saved = wait_event(ser, "run_saved", 30.0)
        if saved is None:
            more = read_json_lines(ser, 3.0)
            for r in more:
                if r.get("ev") == "run_saved":
                    saved = r
                    break
                if r.get("ev") in ("st", "state_blocked"):
                    print(f"  late: {r}")
    else:
        if early_end:
            print("  (skipped p — already closed by end detector)")
        else:
            print("  (skipped p — run_saved already received)")

    print("\n── Result ──")
    if not saved:
        for r in send(ser, "?", 0.5):
            if r.get("ev") == "status":
                print(f"  final status: {r}")
        print("  ✗ No run_saved event")
        ser.close()
        return 1

    rc = evaluate_run(saved, args.duration, args.min_fps, ver)
    ser.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
