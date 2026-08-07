#!/usr/bin/env python3
"""
SGC device system suite — S04 + S05 + S06 (+ optional S03).

Usage:
  py run_device_suite.py COM8
  py run_device_suite.py COM8 -R
  py run_device_suite.py COM8 -R --with-s03
  py run_device_suite.py COM8 --skip-s05

Exit code: number of failed tests (0 = all passed).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run_one(script: str, args: list[str]) -> int:
    cmd = [sys.executable, str(HERE / script), *args]
    print("\n" + "#" * 60)
    print("#", " ".join(cmd))
    print("#" * 60 + "\n")
    r = subprocess.run(cmd, cwd=str(HERE))
    return r.returncode


def main() -> int:
    ap = argparse.ArgumentParser(description="SGC device suite S04–S06")
    ap.add_argument("port", help="Serial port, e.g. COM8")
    ap.add_argument("-R", "--factory-reset", action="store_true",
                    help="Pass -R to each test that supports it")
    ap.add_argument("--with-s03", action="store_true",
                    help="Also run stream integrity S03")
    ap.add_argument("--skip-s04", action="store_true")
    ap.add_argument("--skip-s05", action="store_true")
    ap.add_argument("--skip-s06", action="store_true")
    ap.add_argument("--duration", type=float, default=20.0, help="S04 duration")
    ap.add_argument("--fill-s", type=float, default=12.0, help="S06 fill seconds")
    args = ap.parse_args()

    R = ["-R"] if args.factory_reset else []
    failed = []
    passed = []

    plan = []
    if not args.skip_s04:
        plan.append(("S04", "test_bhy2_rate.py",
                     [args.port, "--duration", str(args.duration), *R]))
    if not args.skip_s05:
        plan.append(("S05", "test_ring_fill.py", [args.port, *R]))
    if not args.skip_s06:
        plan.append(("S06", "test_ring_drain.py",
                     [args.port, "--fill-s", str(args.fill_s), *R]))
    if args.with_s03:
        plan.append(("S03", "test_stream_run.py",
                     [args.port, "--duration", "25", "--gates", "10"]))

    print("=" * 60)
    print("SGC device suite")
    print(f"  port={args.port}  tests={[p[0] for p in plan]}")
    print("=" * 60)

    for name, script, a in plan:
        rc = run_one(script, a)
        if rc == 0:
            passed.append(name)
        else:
            failed.append(name)

    print("\n" + "=" * 60)
    print(f"PASSED: {', '.join(passed) or '(none)'}")
    print(f"FAILED: {', '.join(failed) or '(none)'}")
    print("=" * 60)
    return len(failed)


if __name__ == "__main__":
    sys.exit(main())
