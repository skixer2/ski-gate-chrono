"""
S03 — Stream injection + data integrity.

Picked up by:
  Get-ChildItem test_*.py | ForEach-Object {
    py sgc_test_harness.py --port COM8 $_ --run-id $runId
  }

Delegates to system_tests/test_stream_run.py (~60–90 s with integrity dump).
Default: duration 25s, gates 10, seed 45, factory reset.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

TEST_NAME = "S03 — Stream injection + integrity"

# unit_tests/ → module_design/ → architecture_modules/ → system_design/system_tests/
_SYSTEM = (
    Path(__file__).resolve().parents[3] / "system_tests" / "test_stream_run.py"
)


def run_device_test(port: str) -> bool:
    if not _SYSTEM.is_file():
        print(f"ERROR: missing {_SYSTEM}")
        return False
    cmd = [
        sys.executable,
        str(_SYSTEM),
        port,
        "--duration", "25",
        "--gates", "10",
        "--seed", "45",
        "-R",
    ]
    print("Delegating:", " ".join(cmd))
    r = subprocess.run(cmd)
    return r.returncode == 0
