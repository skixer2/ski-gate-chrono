"""
S04 — BHY2 real LOGGING rate (Opt-A RawRunStore).

Picked up by:
  Get-ChildItem test_*.py | ForEach-Object {
    py sgc_test_harness.py --port COM8 $_ --run-id $runId
  }

Delegates to system_tests/test_bhy2_rate.py (needs ~25–40 s).
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

TEST_NAME = "S04 — BHY2 LOGGING rate (Opt-A)"

# unit_tests/ → module_design/ → architecture_modules/ → system_design/system_tests/
_SYSTEM = (
    Path(__file__).resolve().parents[3] / "system_tests" / "test_bhy2_rate.py"
)


def run_device_test(port: str) -> bool:
    if not _SYSTEM.is_file():
        print(f"ERROR: missing {_SYSTEM}")
        return False
    cmd = [
        sys.executable,
        str(_SYSTEM),
        port,
        "--duration", "20",
        "-R",
    ]
    print("Delegating:", " ".join(cmd))
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
    print(f"child exit={r.returncode}")
    return r.returncode == 0
