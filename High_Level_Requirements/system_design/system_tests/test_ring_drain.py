#!/usr/bin/env python3
"""
S06 - LOGGING drain of pre-roll (pop-2 + push-1 live, v4.77+).

Flow:
  IDLE -> ARM -> wait until ring fairly full (or ring_full)
  -> force LOGGING WITHOUT g_force_logging?  We need natural drain path.

  Problem: serial 'l' sets g_force_logging and skips ring.

  So S06 uses a small baro trick only if test mode can inject - OR we add
  firmware support. For BHY2-only bench without moving the device, we use:

    Firmware path that drains ring: natural LOGGING entry (start det).
    On a desk, start det won't fire. Options:
      1) Add serial command 'L' = LOGGING without force flag (drain path)
      2) Use stream mode pressure ramp (S03) - not pure BHY2

  v4.73: serial 'L' (capital) enters LOGGING with g_force_logging=false
  so drain+encode runs. End with 'p'.

Pass:
  After ARM fill, 'L' -> run_saved with fr >= ~0.8 * ring_count_at_entry
  (pre-roll encoded). Optional fps of drain phase from status timing.

Usage:
  py test_ring_drain.py COM8
  py test_ring_drain.py COM8 -R
  py test_ring_drain.py COM8 --fill-s 6
"""

from __future__ import annotations

import argparse
import json
import sys
# Windows consoles often default to cp1252; force UTF-8 so banners don't crash.
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

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
    ap = argparse.ArgumentParser(description="S06 - ring drain on natural LOGGING")
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("-R", "--factory-reset", action="store_true")
    ap.add_argument("--fill-s", type=float, default=8.0,
                    help="Seconds to fill ring in ARMED before drain LOGGING")
    ap.add_argument("--log-s", type=float, default=3.0,
                    help="Extra live LOGGING seconds after drain")
    ap.add_argument("--min-pre-roll-frac", type=float, default=0.75,
                    help="run fr must be >= frac * r_at_L")
    args = ap.parse_args()

    print("=" * 50)
    print("S06 - FlashRing Drain (natural LOGGING path)")
    print(f"  Port: {args.port}  fill_s={args.fill_s} log_s={args.log_s}")
    print("=" * 50)

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.4)
    ser.reset_input_buffer()

    st = status(ser)
    ver = st.get("ver") if st else None
    print(f"  ver={ver} st={st.get('st') if st else None} rm={st.get('rm') if st else None}")

    if args.factory_reset:
        print("  Factory reset...")
        send(ser, "R", 0.4)
        print(f"  boot: {wait_event(ser, 'boot', 12.0)}")
        t0w = time.perf_counter()
        while time.perf_counter() - t0w < 15.0:
            time.sleep(0.7)
            read_json_lines(ser, 0.2)
            st = status(ser)
            if st and st.get("st") == "SLEEP":
                print(f"  ready ({time.perf_counter()-t0w:.1f}s)")
                ver = st.get("ver") or ver
                break

    send_keep(ser, "O 0", 0.3)
    send(ser, "i", 0.3)
    time.sleep(1.2)

    print("\n-- ARM + fill --")
    send(ser, "a", 0.6)
    st = status(ser)
    if not st or st.get("st") != "ARMED":
        print(f"  FAIL not ARMED {st}")
        ser.close()
        return 1
    print(f"  OK ARMED r={st.get('r')} rm={st.get('rm')}")

    t_fill0 = time.perf_counter()
    last = -1
    while time.perf_counter() - t_fill0 < args.fill_s:
        read_json_lines(ser, 0.2)
        sec = int(time.perf_counter() - t_fill0)
        if sec != last and sec > 0 and sec % 2 == 0:
            st = status(ser)
            print(f"  ... fill {sec}s r={st.get('r') if st else '?'}", flush=True)
            last = sec
            if st and int(st.get("r") or 0) >= int(st.get("rm") or 9999):
                print("  ring full - stopping fill early")
                break

    st = status(ser)
    r_at = int(st.get("r") or 0) if st else 0
    rm = int(st.get("rm") or 1000) if st else 1000
    print(f"  pre-roll at L: r={r_at}/{rm}")

    if r_at < 50:
        print("  FAIL ring almost empty - fill longer or check ARMED path")
        ser.close()
        return 1

    print("\n-- LOGGING drain path (serial L, not l; tm must be 0) --")
    # Ensure test mode OFF - when tm=1, 'L' is set_la not drain LOGGING
    for o in send_keep(ser, "?", 0.4):
        if o.get("ev") == "status" and o.get("tm") in (1, True, "1"):
            print("  tm=1 - toggling T to OFF before L")
            send_keep(ser, "T", 0.4)

    t_l0 = time.perf_counter()
    evs = send_keep(ser, "L", 1.0)
    saw_drain_cmd = False
    for o in evs:
        if o.get("ev") in ("st", "run_created", "state_blocked", "cmd"):
            print(f"  {o}")
        if o.get("ev") == "cmd" and o.get("cmd") == "L" and o.get("mode") == "drain":
            saw_drain_cmd = True
        if o.get("ev") == "cmd" and o.get("cmd") == "L" and "la" in o:
            print("  FAIL 'L' handled as test-mode set_la - flash v4.74+ or turn tm OFF")
            ser.close()
            return 1

    st = status(ser)
    if not st or st.get("st") != "LOGGING":
        print(f"  FAIL not LOGGING after 'L'. st={st} drain_ack={saw_drain_cmd}")
        print("  tip: flash v4.74+ (test_mode no longer steals L when tm=0)")
        send(ser, "i", 0.3)
        ser.close()
        return 1
    print("  OK LOGGING (drain path, force_logging=0)")

    # Wait for drain + a bit of live; poll r via ?
    # Also catch run_saved if end-det auto-closes (old FW) or we send p.
    last = -1
    drained_at = None
    saved = None
    while time.perf_counter() - t_l0 < args.fill_s + args.log_s + 20.0:
        batch = read_json_lines(ser, 0.15)
        for o in batch:
            if o.get("ev") == "run_saved" and saved is None:
                saved = o
                print(f"  evt: {o}", flush=True)
            if o.get("ev") == "st":
                print(f"  evt: {o}", flush=True)
        st = status(ser)
        if not st:
            time.sleep(0.15)
            continue
        r = int(st.get("r") or 0)
        sec = int(time.perf_counter() - t_l0)
        if sec != last and sec % 1 == 0:
            print(f"  ... log {sec}s st={st.get('st')} r={r}", flush=True)
            last = sec
        if drained_at is None and r == 0 and st.get("st") == "LOGGING":
            drained_at = time.perf_counter() - t_l0
            # pop2+push1 while backlog>0; last frame pops without push.
            # ~10-14 s typical for keep=1000 (SPI slower than ideal 10 ms).
            print(f"  ring empty after {drained_at:.2f}s LOGGING "
                  f"(expect ~10-14s for 1000 keep; fail if >>20s = r=1 deadlock)")
            live_end = time.perf_counter() + args.log_s
            while time.perf_counter() < live_end:
                for o in read_json_lines(ser, 0.2):
                    if o.get("ev") == "run_saved" and saved is None:
                        saved = o
                        print(f"  evt: {o}", flush=True)
            break
        if st.get("st") == "POST_RUN":
            # Auto end-det closed - run_saved may already be out or coming
            print("  note: already POST_RUN (end det or other)")
            break
        if st.get("st") not in ("LOGGING", "POST_RUN"):
            break
        if saved is not None:
            break
        time.sleep(0.1)

    if saved is None:
        print("-- POST_RUN (send p) --")
        post = send_keep(ser, "p", 0.8)
        for o in post:
            if o.get("ev") in ("st", "run_saved"):
                print(f"  {o}")
            if o.get("ev") == "run_saved":
                saved = o
        if saved is None:
            saved = wait_event(ser, "run_saved", 25.0)
            print(f"  run_saved: {saved}")
    else:
        print("-- POST_RUN (already have run_saved) --")

    print("\n-- Result --")
    if not saved:
        print("  FAIL no run_saved")
        ser.close()
        return 1

    fr = int(saved.get("fr") or 0)
    need = int(args.min_pre_roll_frac * min(r_at, 1000))  # keep window
    ok_fr = fr >= need
    # Drain should finish in ~ keep/100 ... keep/60 s (100-60 Hz effective)
    keep_est = min(r_at, 1000)
    drain_ok = True
    if drained_at is not None:
        drain_ok = drained_at <= max(18.0, keep_est / 50.0)  # fail hung r=1
    print(f"  fr={fr}  r_at_L={r_at}  need>={need}  "
          f"drain_s={drained_at} store={saved.get('store')} "
          f"fps10={saved.get('fps10')}")

    print("=" * 50)
    if ok_fr and saved.get("ok") and drain_ok:
        msg = f"OK S06 PASSED - encoded {fr} frames (pre-roll was {r_at})"
        if drained_at is not None:
            msg += f", drain {drained_at:.2f}s"
        print(msg)
        rc = 0
    else:
        print("FAIL S06 FAILED")
        if not ok_fr:
            print(f"  fr {fr} < {need} (pre-roll not fully encoded?)")
        if not saved.get("ok"):
            print("  run_saved ok=false")
        if not drain_ok:
            print(f"  drain_s={drained_at:.2f} too long - likely r=1 pop1/push1 deadlock")
        rc = 1
    if ver:
        print(f"  Firmware: v{ver}")
    print("=" * 50)

    send(ser, "i", 0.3)
    ser.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
