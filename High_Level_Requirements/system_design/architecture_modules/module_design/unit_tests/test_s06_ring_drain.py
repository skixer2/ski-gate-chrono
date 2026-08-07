"""
S06 — LOGGING pre-roll drain (pop2 + push1).

Harness loop:
  py sgc_test_harness.py --port COM8 test_s06_ring_drain.py --run-id $runId

Delegates to system_tests/test_ring_drain.py (~20–40 s).
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

TEST_NAME = "S06 — LOGGING pre-roll drain"

_SYSTEM = (
    Path(__file__).resolve().parents[3] / "system_tests" / "test_ring_drain.py"
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
        "--fill-s", "12",
    ]
    print("Delegating:", " ".join(cmd))
    r = subprocess.run(cmd)
    return r.returncode == 0
