"""
Mock Serial Replay Harness (R7) — run tests without physical Nicla.

Captures a real test session's serial output to a JSONL file,
then replays it for fast iteration on test logic.

Usage:
    # 1. Capture a real session:
    python sgc_test_harness.py --port COM8 -l session.jsonl test_state_machine.py

    # 2. Replay it (no hardware needed):
    python sgc_mock_runner.py --replay session.jsonl test_state_machine.py

Architecture:
    The mock harness replaces serial.Serial with a MockSerial that
    reads pre-recorded JSONL lines. Commands are sent to /dev/null.
    State polling uses the recorded timeline.
"""

import os
import sys
import json
import time
import argparse
from typing import Optional, List, Dict, Any


class MockSerial:
    """Fake serial port that replays pre-recorded JSONL lines."""

    def __init__(self, lines: List[str], line_delay_ms: float = 5):
        self.lines = lines
        self.line_delay = line_delay_ms / 1000.0
        self.idx = 0
        self.is_open = True
        self.timeout = 1.0

    def reset_input_buffer(self): pass
    def reset_output_buffer(self): pass

    def write(self, data):
        """Commands are discarded in mock mode."""
        pass

    def flush(self): pass

    def readline(self):
        """Return next pre-recorded line, simulating serial timing."""
        if self.idx >= len(self.lines):
            return b''
        time.sleep(self.line_delay)
        line = (self.lines[self.idx] + '\n').encode('utf-8')
        self.idx += 1
        return line

    def close(self):
        self.is_open = False


def load_replay_file(replay_path: str) -> List[str]:
    """Load captured JSONL lines from a replay file."""
    lines = []
    with open(replay_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            # Skip metadata lines
            if line.startswith('===') or line.startswith('SGC Test:'):
                continue
            if line.startswith('SETUP:') or line.startswith('Step '):
                continue
            # Keep only JSON lines (and legacy status lines for backward compat)
            if line.startswith('{') or 'STATE:' in line or 'R:' in line:
                lines.append(line)
    return lines


def inject_mock_serial(harness_module, replay_path: str):
    """Monkey-patch the test harness to use mock serial."""
    lines = load_replay_file(replay_path)
    if not lines:
        print(f"ERROR: No replayable lines found in {replay_path}")
        sys.exit(1)
    print(f"Loaded {len(lines)} replay lines from {replay_path}")

    mock = MockSerial(lines)

    # Patch serial.Serial to return our mock
    import serial
    original_serial = serial.Serial

    class PatchedSerial:
        def __init__(self, *args, **kwargs):
            pass
        def __getattr__(self, name):
            return getattr(mock, name)

    serial.Serial = PatchedSerial
    serial.tools.list_ports.comports = lambda: [type('Port', (), {
        'device': 'MOCK',
        'description': 'Mock Nicla Sense ME (replay mode)',
        'manufacturer': 'Arduino'
    })()]

    return mock


def main():
    parser = argparse.ArgumentParser(description='SGC Mock Test Runner (no hardware needed)')
    parser.add_argument('scenario', help='Test scenario file to run')
    parser.add_argument('--replay', required=True, help='Captured JSONL replay file')
    parser.add_argument('-v', '--verbose', action='store_true', default=True)
    args = parser.parse_args()

    # Load the real harness module but inject mock serial
    import importlib.util

    # Patch before importing the harness
    inject_mock_serial(None, args.replay)

    # Import the scenario
    spec = importlib.util.spec_from_file_location("scenario", args.scenario)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    scenarios = getattr(mod, 'SCENARIOS', [])
    if not scenarios:
        print(f"ERROR: No SCENARIOS list in {args.scenario}")
        return

    # Create harness with mock port
    from sgc_test_harness import SGCTestHarness
    harness = SGCTestHarness('MOCK', verbose=args.verbose)
    if not harness.connect():
        return

    all_ok = True
    try:
        for s in scenarios:
            results = harness.run_scenario(s)
            ok = harness.all_passed()
            if not ok:
                all_ok = False
            print(f"  {s.name}: {'✅' if ok else '❌'} "
                  f"({sum(1 for r in results if r.passed)}/{len(results)})")

        overall = 'ALL TESTS PASSED ✅' if all_ok else 'SOME TESTS FAILED ❌'
        print(f"\n{'='*60}")
        print(f"MOCK OVERALL: {overall}")
        print("⚠️  Mock replay — results depend on captured session quality")
    finally:
        harness.disconnect()


if __name__ == '__main__':
    main()
