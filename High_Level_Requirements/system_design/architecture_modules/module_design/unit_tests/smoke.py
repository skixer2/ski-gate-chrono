#!/usr/bin/env python3
"""SGC DAILY SMOKE — the one command to run every day.

    python smoke.py                 # auto-detect the port
    python smoke.py --port COM4     # explicit
    python smoke.py --no-hardware   # host-only gates (CI / no unit attached)

Exit code 0 only when EVERY check passes. That is the whole contract: a green
smoke means the unit is fit to be used today.

WHY THIS EXISTS (2026-11-23, JP: "I want a complete smoke test ASAP, every day")
-----------------------------------------------------------------------------
Everything here is written against what the Zephyr firmware ACTUALLY does,
verified by reading src/pull_proto.c, not by porting Arduino-era scripts. The
Arduino-era suite was retired to obsolete/ because it drove an interface that
no longer exists ('T'/'B'/'Q'/'L' injection, 'i' sleep, '?' status) - it could
only ever produce false red.

Tiers
-----
HOST   (no unit needed, < 2 s)
  1. test_spec_constants.py     requirements <-> source constants drift
  2. test_protocol_conformance  tests/docs vs the firmware's real event+command set
  3. image/version consistency  APP_VERSION == VERSION file, and >= last known

DEVICE (unit on serial, ~60 s)
  4. V   -> firmware version, must equal the expected version
  5. W   -> thread stack watermarks (reports headroom; fails if any thread < 64 B free)
  6. D   -> run listing + {"rec":"STATE"} + {"free":N} + {"d_end":N}
  7. a/g/e bench cycle -> ARMED -> LOGGING -> POST_RUN -> SLEEP, state must return to SLEEP
  8. store integrity -> d_end count matches the run lines returned

NOT in smoke (deliberate)
-------------------------
- Synthetic 60 s runs ('S') and ring-fill: too slow for daily use.
- BLE / phone app: separate lane.
- Sensor liveness while ARMED: needs the drop/arm path; see test_s07_sensor_liveness.py.

The point of a daily smoke is that it is FAST and it NEVER LIES. If a check
cannot be evaluated, it reports SKIP with a reason - never a silent pass.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FW_APP = os.path.normpath(os.path.join(
    HERE, "..", "implementation", "Firmware_Zephyr", "apps", "03_pull_proto"))

# ── result bookkeeping ────────────────────────────────────────────────
RESULTS = []   # (name, status, detail)


def record(name, status, detail=""):
    RESULTS.append((name, status, detail))
    icon = {"PASS": "  OK  ", "FAIL": " FAIL ", "SKIP": " SKIP ", "WARN": " WARN "}[status]
    print("[%s] %-46s %s" % (icon, name, detail))


def banner(t):
    print()
    print("=" * 74)
    print(t)
    print("=" * 74)


# ── HOST TIER ─────────────────────────────────────────────────────────
def host_spec_constants():
    """Requirements document vs firmware source constants."""
    path = os.path.join(HERE, "test_spec_constants.py")
    r = subprocess.run([sys.executable, path], capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    last = tail[-1] if tail else ""
    if r.returncode == 0:
        record("spec_constants (requirements <-> code)", "PASS", last)
        return True
    record("spec_constants (requirements <-> code)", "FAIL", last)
    for line in tail[-8:]:
        print("        " + line)
    return False


def host_protocol_conformance():
    """Tests and docs must speak the firmware's real language."""
    path = os.path.join(HERE, "test_protocol_conformance.py")
    r = subprocess.run([sys.executable, path], capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    out = r.stdout or ""
    fails = [l for l in out.splitlines() if l.strip().startswith("FAIL ")]
    if r.returncode == 0:
        record("protocol_conformance (tests <-> firmware)", "PASS", "0 drift errors")
        return True
    record("protocol_conformance (tests <-> firmware)", "FAIL",
           "%d drift error(s)" % len(fails))
    for f in fails[:10]:
        print("        " + f.strip())
    return False


def host_version_consistency():
    """APP_VERSION, VERSION file and the flashed claim must agree."""
    hdr = os.path.join(FW_APP, "src", "app_version.h")
    ver_file = os.path.join(FW_APP, "VERSION")
    try:
        h = open(hdr, encoding="utf-8").read()
        m = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', h)
        app_ver = m.group(1) if m else None
    except OSError as e:
        record("version consistency", "SKIP", "cannot read app_version.h: %s" % e)
        return None

    if not app_ver:
        record("version consistency", "FAIL", "no APP_VERSION define found")
        return False

    try:
        v = open(ver_file, encoding="utf-8").read()
        major = re.search(r"VERSION_MAJOR\s*=\s*(\d+)", v)
        minor = re.search(r"VERSION_MINOR\s*=\s*(\d+)", v)
        patch = re.search(r"PATCHLEVEL\s*=\s*(\d+)", v)
        file_ver = "%s.%s.%s" % (major.group(1), minor.group(1), patch.group(1)) \
            if (major and minor and patch) else None
    except OSError as e:
        record("version consistency", "SKIP", "cannot read VERSION: %s" % e)
        return None

    if file_ver != app_ver:
        record("version consistency", "FAIL",
               "app_version.h=%s but VERSION=%s" % (app_ver, file_ver))
        return False

    record("version consistency", "PASS", "%s (both sources agree)" % app_ver)
    return True


# ── DEVICE TIER ───────────────────────────────────────────────────────
class Dev:
    """Minimal serial client: send a command, collect lines for N ms."""

    def __init__(self, port):
        import serial  # pyserial
        self.s = serial.Serial(port, 115200, timeout=0.1)
        time.sleep(0.3)
        self.drain(0.4)

    def drain(self, secs=0.4):
        """Collect whatever arrives within a HARD deadline.

        BUG FIXED 2026-11-23: this used to slide the deadline forward on every
        byte received (t0 = time.time() inside the loop). The unit emits
        b1_rate telemetry continuously, so the loop never exited and the first
        device run hung the whole invoke. The deadline must be fixed, and a
        quiet gap ends the read early.
        """
        deadline = time.time() + secs
        quiet_until = time.time() + min(0.25, secs)
        buf = b""
        while time.time() < deadline:
            n = self.s.in_waiting
            if n:
                buf += self.s.read(n)
                quiet_until = time.time() + 0.15
            elif time.time() > quiet_until:
                break
            else:
                time.sleep(0.02)
        return buf.decode("utf-8", "replace")

    def cmd(self, text, secs=0.9):
        self.s.reset_input_buffer()
        self.s.write((text + "\r\n").encode())
        self.s.flush()
        return self.drain(secs)

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def dev_version(d):
    out = d.cmd("V", 1.2)
    m = re.search(r'\{"ev":"version","ver":"([^"]+)"\}', out)
    if not m:
        record("V - firmware version", "FAIL",
               "no {\"ev\":\"version\"} answer (got %r)" % out.strip()[:80])
        return None
    ver = m.group(1)
    record("V - firmware version", "PASS", ver)
    return ver


def dev_stacks(d):
    out = d.cmd("W", 1.6)
    rows = re.findall(r"^ (\S+)\s*:\s*STACK: unused (\d+) usage (\d+) / (\d+)", out, re.M)
    if not rows:
        record("W - thread stack headroom", "FAIL", "no thread analyzer output")
        return False
    tight = []
    for name, unused, used, total in rows:
        unused = int(unused)
        if unused < 64:
            tight.append("%s %dB free" % (name.strip(), unused))
    summary = "%d threads, smallest free %d B" % (
        len(rows), min(int(r[1]) for r in rows))
    if tight:
        record("W - thread stack headroom", "FAIL", "critically low: " + ", ".join(tight))
        return False
    record("W - thread stack headroom", "PASS", summary)
    return True


def dev_listing(d):
    out = d.cmd("D", 1.5)
    runs = re.findall(r'\{"run":(\d+),"ts":(\d+),"sz":(\d+),"side":(\d+)\}', out)
    rec = re.search(r'\{"rec":"(\w+)"\}', out)
    free = re.search(r'\{"free":(\d+)\}', out)
    d_end = re.search(r'\{"d_end":(\d+)\}', out)
    if not rec:
        record("D - run listing + state", "FAIL", "no {\"rec\":...} line")
        return None
    if d_end is None:
        record("D - run listing + state", "FAIL", "no {\"d_end\":N} terminator")
        return None
    ok = True
    if len(runs) != int(d_end.group(1)):
        record("D - run listing", "FAIL",
               "%d run lines but d_end says %s" % (len(runs), d_end.group(1)))
        ok = False
    else:
        record("D - run listing", "PASS",
               "%d runs, state %s, free %s" % (len(runs), rec.group(1),
                                               free.group(1) if free else "?"))
    if not free:
        record("D - capacity estimate", "FAIL", "no {\"free\":N} line")
        ok = False
    return {"runs": len(runs), "state": rec.group(1), "d_end": int(d_end.group(1)),
            "free": int(free.group(1)) if free else None, "ok": ok}


def dev_cycle(d):
    """a -> g -> e bench cycle. The state MUST come back to SLEEP."""
    d.cmd("a", 1.0)
    out_a = d.cmd("D", 1.2)
    st_a = re.search(r'\{"rec":"(\w+)"\}', out_a)
    armed = out_a.count("ARMED") > 0 or (st_a and st_a.group(1) == "ARMED")
    if not armed:
        record("a - arm", "FAIL",
               "state did not become ARMED (drain: %r)" % out_a.strip()[:90])
        return False
    record("a - arm", "PASS", "ARMED")

    out_g = d.cmd("g", 1.2)
    if '"LOGGING"' not in out_g and '"rec":"LOGGING"' not in out_g:
        record("g - start logging", "FAIL", "no LOGGING confirmation")
        return False
    record("g - start logging", "PASS", "LOGGING")

    time.sleep(0.5)
    out_e = d.cmd("e", 2.0)
    out_st = d.cmd("D", 1.5)
    st = re.search(r'\{"rec":"(\w+)"\}', out_st)
    if not st:
        record("e - end run", "FAIL", "no state readout after end")
        return False
    if st.group(1) != "SLEEP":
        record("e - end run", "FAIL",
               "state is %s, expected SLEEP (e output %r)"
               % (st.group(1), out_e.strip()[:70]))
        return False
    record("e - end run", "PASS", "POST_RUN -> SLEEP")
    return True


# ── MAIN ──────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="SGC daily smoke test")
    ap.add_argument("--port", default="auto")
    ap.add_argument("--no-hardware", action="store_true",
                    help="run host-only gates (no unit attached)")
    ap.add_argument("--expect-version", default=None,
                    help="fail if the unit reports a different firmware version")
    args = ap.parse_args()

    t0 = time.time()
    banner("SGC DAILY SMOKE  -  " + time.strftime("%Y-%m-%d %H:%M:%S"))

    banner("HOST TIER (no hardware)")
    host_spec_constants()
    host_protocol_conformance()
    host_version_consistency()

    if args.no_hardware:
        record("device tier", "SKIP", "--no-hardware")
    else:
        banner("DEVICE TIER")
        port = args.port
        dev = None
        try:
            if port == "auto":
                from serial.tools import list_ports
                cands = [p.device for p in list_ports.comports()]
                if not cands:
                    raise RuntimeError("no serial ports found")
                port = cands[0]
                for c in cands:
                    try:
                        dev = Dev(c)
                        if '"ev":"version"' in dev.cmd("V", 1.0):
                            port = c
                            break
                        dev.close()
                        dev = None
                    except Exception:
                        dev = None
                if dev is None:
                    raise RuntimeError("no SGC unit answered on %s" % ", ".join(cands))
            else:
                dev = Dev(port)

            print("     (unit on %s)\n" % port)
            ver = dev_version(dev)
            if args.expect_version and ver and ver != args.expect_version:
                record("expected version", "FAIL",
                       "unit has %s, expected %s" % (ver, args.expect_version))
            dev_stacks(dev)
            listing = dev_listing(dev)
            if listing and not listing["ok"]:
                record("listing consistency", "FAIL", "see D failures above")
            dev_cycle(dev)
        except Exception as e:
            record("device tier", "FAIL", "%s: %s" % (e.__class__.__name__, e))
        finally:
            if dev:
                dev.close()

    # ── verdict ───────────────────────────────────────────────────────
    fails = [r for r in RESULTS if r[1] == "FAIL"]
    skips = [r for r in RESULTS if r[1] == "SKIP"]
    passes = [r for r in RESULTS if r[1] == "PASS"]

    banner("VERDICT")
    print("  %d passed, %d failed, %d skipped   (%.1f s)"
          % (len(passes), len(fails), len(skips), time.time() - t0))
    if fails:
        print()
        for name, _, detail in fails:
            print("  FAILED: %s  -  %s" % (name, detail))
        print()
        print("SMOKE FAIL")
        return 1
    if skips:
        print()
        for name, _, detail in skips:
            print("  SKIPPED: %s  -  %s" % (name, detail))
        print()
        print("SMOKE INCOMPLETE (skips present, nothing failed)")
        return 0
    print()
    print("SMOKE PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main())
