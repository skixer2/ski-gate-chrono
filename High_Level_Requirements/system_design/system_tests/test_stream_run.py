"""
System test: S03 — Full stream-injection run (binary sensor streaming)

Runs the complete pipeline on real hardware:
  wakeup → test mode → set pressure → arm → stream 5000 frames
  → start detection → LOGGING → gate impacts → end detection
  → POST_RUN → run saved to flash

Uses sgc_stream_simulator.py in non-interactive mode.
Takes ~60 seconds. Requires real device connected via serial.

Usage:
  python test_stream_run.py COM8                    # Full 50s run
  python test_stream_run.py COM8 --duration 20      # Quick 20s test
  python test_stream_run.py COM8 --replay run.ndjson  # Replay recorded data

Returns exit code 0 on success, 1 on failure — CI-friendly.
"""

import sys
import os

# Add unit_tests to path for the simulator import
_this_dir = os.path.dirname(os.path.abspath(__file__))
_unit_dir = os.path.join(_this_dir, '..', 'architecture_modules',
                         'module_design', 'unit_tests')
sys.path.insert(0, _unit_dir)

from sgc_stream_simulator import run_full_test


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="S03 — Stream injection system test",
    )
    parser.add_argument("port", help="Serial port (e.g., COM8, /dev/ttyACM0)")
    parser.add_argument("--duration", type=float, default=50.0,
                        help="Run duration (default: 50s)")
    parser.add_argument("--gates", type=int, default=30,
                        help="Number of gates (default: 30)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Deterministic seed (default: 42)")
    parser.add_argument("--replay", type=str, default=None,
                        help="Replay from NDJSON file")
    parser.add_argument("--save", type=str, default=None,
                        help="Save frames to NDJSON for later replay")
    parser.add_argument("-R", "--reset", action="store_true",
                        help="Factory-reset (erase flash) before the run "
                             "(default: OFF, preserves stored runs)")

    args = parser.parse_args()

    print("=" * 50)
    print("S03 — Stream Injection System Test")
    print(f"  Port:     {args.port}")
    print(f"  Duration: {args.duration}s")
    print(f"  Gates:    {args.gates}")
    print(f"  Seed:     {args.seed}")
    if args.replay:
        print(f"  Replay:   {args.replay}")
    print("=" * 50)

    exit_code = run_full_test(
        port=args.port,
        duration_s=args.duration,
        gate_count=args.gates,
        seed=args.seed,
        save_path=args.save,
        replay_path=args.replay,
        factory_reset=args.reset,
        interactive=False,
    )

    if exit_code == 0:
        print("\n[S03] ✓ PASSED")
    else:
        print("\n[S03] ✗ FAILED")

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
