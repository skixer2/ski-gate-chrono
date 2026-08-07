"""
S05 — ARMED linear pre-roll fill rate.

Harness loop:
  py sgc_test_harness.py --port COM8 test_s05_ring_fill.py --run-id $runId

Delegates to system_tests/test_ring_fill.py (default target 1000 ≈ 10–15 s).
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

TEST_NAME = "S05 — ARMED pre-roll fill rate"

_SYSTEM = (
    Path(__file__).resolve().parents[3] / "system_tests" / "test_ring_fill.py"
)


def run_device_test(port: str) -> bool:
    if not _SYSTEM.is_file():
        print(f"ERROR: missing {_SYSTEM}")
        return False
    cmd = [
        sys.executable,
        str(_SYSTEM),
        port,
        "-R",
        "--target", "1000",
    ]
    print("Delegating:", " ".join(cmd))
    r = subprocess.run(cmd)
    return r.returncode == 0
