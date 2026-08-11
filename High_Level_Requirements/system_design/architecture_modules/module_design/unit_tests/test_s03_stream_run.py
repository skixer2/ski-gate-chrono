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

import os
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
    # Capture child stdout/stderr so Tee/log files see S03 output (manual
    # run can pass while wrapper log looked empty + rc!=0).
    env = dict(os.environ)
    env['PYTHONIOENCODING'] = 'utf-8'
    env['PYTHONUTF8'] = '1'
    r = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
        env=env,
    )
    if r.stdout:
        print(r.stdout, end='' if r.stdout.endswith('\n') else '\n')
    print(f"S03 child exit={r.returncode}")
    return r.returncode == 0
