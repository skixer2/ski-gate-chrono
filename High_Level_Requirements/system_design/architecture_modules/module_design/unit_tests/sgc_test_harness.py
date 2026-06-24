"""
SGC Unit Test Harness v2 — JSON-lines protocol + structured assertions.

Usage:
    python sgc_test_harness.py --port COM3 test_start_detector.py
    python sgc_test_harness.py --port auto test_bit_packer.py

Architecture:
    Python ──(serial USB)──▶ SGC Device in TEST_MODE + JSON mode
    Sends inject commands (B/Q/L), reads JSON-lines responses.

Protocol: see json_protocol.md for full spec.
"""

import os
import sys
import json
import serial
import serial.tools.list_ports
import time
import argparse
import traceback
from dataclasses import dataclass, field
from typing import Optional, Callable, List, Dict, Any, Union
from io import StringIO


class Tee:
    """Write to multiple streams simultaneously."""
    def __init__(self, *files):
        self.files = files
    def write(self, data):
        for f in self.files:
            f.write(data)
    def flush(self):
        for f in self.files:
            f.flush()


@dataclass
class TestStep:
    """A single test step.

    Three modes:
    1. Send+expect_json:  command → read JSON → validate with expect_json callback
    2. Poll state:        poll_state set → poll ? until target state appears
    3. Send+check_legacy: command → read → expect_contains (backward compat)
    """
    description: str
    command: Optional[str] = None
    wait_ms: int = 100
    timeout_ms: int = 5000

    # ── JSON assertion (preferred) ─────────────────────────────
    expect_json: Optional[Union[Dict[str, Any], Callable[[Dict], bool]]] = None

    # ── Legacy string matching (backward compat) ────────────────
    expect_contains: Optional[str] = None
    expect_not_contains: Optional[str] = None

    # ── State polling ──────────────────────────────────────────
    poll_state: Optional[str] = None
    poll_interval_ms: int = 300

    # ── Callback ───────────────────────────────────────────────
    on_response: Optional[Callable] = None


@dataclass
class TestResult:
    step: int
    description: str
    passed: bool
    output: str = ""
    json_data: Optional[Dict] = None


@dataclass
class TestScenario:
    name: str
    steps: List[TestStep] = field(default_factory=list)
    setup_commands: List[str] = field(default_factory=list)
    teardown_commands: List[str] = field(default_factory=list)  # NEW


class SGCTestHarness:
    def __init__(self, port: str, baud: int = 115200, verbose: bool = True):
        self.port = port
        self.baud = baud
        self.verbose = verbose
        self.ser: Optional[serial.Serial] = None
        self.results: List[TestResult] = []

    def find_port(self) -> Optional[str]:
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if any(k in p.description.lower() for k in ['nicla', 'arduino', 'mbed', 'nrf52']):
                return p.device
            if p.manufacturer and any(k in p.manufacturer.lower() for k in ['arduino', 'arm', 'mbed']):
                return p.device
        if ports:
            return ports[0].device
        return None

    def connect(self) -> bool:
        if self.port == 'auto':
            self.port = self.find_port()
            if not self.port:
                print("ERROR: No serial port found")
                return False
            print(f"Auto-detected port: {self.port}")
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            time.sleep(2)
            self._flush()
            print(f"Connected to {self.port} at {self.baud} baud")
            return True
        except serial.SerialException as e:
            print(f"ERROR: Cannot open {self.port}: {e}")
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _flush(self):
        if self.ser:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

    def drain_serial(self, timeout_ms: int = 500) -> str:
        """Read and discard all pending serial data. Returns drained content."""
        if not self.ser:
            return ""
        self.ser.timeout = 0.1
        drained = []
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            try:
                line = self.ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    drained.append(line)
                else:
                    break
            except:
                break
        return '\n'.join(drained)

    def _read_all(self, timeout_ms: int = 500) -> str:
        """Read all pending serial lines until a gap or deadline."""
        if not self.ser:
            return ""
        line_timeout_s = min(timeout_ms / 1000.0, 1.0)
        self.ser.timeout = line_timeout_s
        lines = []
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            try:
                line = self.ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    lines.append(line)
                else:
                    break
            except:
                break
        return '\n'.join(lines)

    def _read_json_lines(self, timeout_ms: int = 3000) -> List[Dict[str, Any]]:
        """Read all pending serial lines, parse as JSON. Returns list of dicts."""
        raw = self._read_all(timeout_ms)
        if not raw:
            return []
        objects = []
        for line in raw.split('\n'):
            line = line.strip()
            if not line:
                continue
            # Skip non-JSON lines (human-readable fallback)
            if not line.startswith('{'):
                continue
            try:
                obj = json.loads(line)
                objects.append(obj)
            except json.JSONDecodeError:
                # Keep non-JSON lines in raw output for debugging
                pass
        return objects

    def send(self, cmd: str):
        if not self.ser:
            return
        self.ser.write((cmd + '\n').encode('utf-8'))
        self.ser.flush()
        time.sleep(0.05)

    def query_state(self) -> str:
        """Send ? and return just the state name."""
        self._flush()
        self.send('?')
        time.sleep(0.15)
        resp = self._read_all(1000)
        # Try JSON first
        for line in resp.split('\n'):
            if line.strip().startswith('{'):
                try:
                    obj = json.loads(line.strip())
                    if obj.get('st'):
                        return obj['st']
                except:
                    pass
        # Fallback: legacy string parsing
        for part in resp.split():
            if part.startswith('STATE:'):
                return part.split(':')[1]
        return "UNKNOWN"

    def query_status_json(self) -> Optional[Dict[str, Any]]:
        """Send ? and return the parsed JSON status object."""
        self._flush()
        self.send('?')
        time.sleep(0.3)
        objs = self._read_json_lines(2000)
        for obj in objs:
            if obj.get('ev') == 'status':
                return obj
        return None

    # ── State polling ──────────────────────────────────────────

    def wait_for_state(self, target_state: str, timeout_ms: int = 15000,
                       poll_interval_ms: int = 300) -> bool:
        """Poll ? until STATE:<target_state> appears."""
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            state = self.query_state()
            if state == target_state:
                if self.verbose:
                    print(f"    ← STATE:{state}")
                return True
            if self.verbose:
                print(".", end="", flush=True)
            time.sleep(poll_interval_ms / 1000.0)
        if self.verbose:
            print(f"  ⏰ timeout")
        return False

    def wait_for_json_event(self, event_name: str, timeout_ms: int = 15000,
                            poll_interval_ms: int = 200) -> Optional[Dict[str, Any]]:
        """Poll serial until a JSON event with matching 'ev' field appears.
        Returns the full JSON dict or None on timeout."""
        deadline = time.time() + timeout_ms / 1000.0
        while time.time() < deadline:
            objs = self._read_json_lines(500)
            for obj in objs:
                if obj.get('ev') == event_name:
                    if self.verbose:
                        print(f"    ← JSON ev:{event_name}")
                    return obj
            time.sleep(poll_interval_ms / 1000.0)
        if self.verbose:
            print(f"  ⏰ timeout waiting for ev:{event_name}")
        return None

    # ── JSON assertion helper ──────────────────────────────────

    def _check_json(self, data: Dict[str, Any], expected: Union[Dict, Callable]) -> bool:
        """Check a JSON object against expected values or a predicate."""
        if callable(expected):
            return expected(data)
        for key, value in expected.items():
            if key not in data:
                return False
            actual = data[key]
            # Numeric tolerance (for floats from Arduino)
            if isinstance(value, (int, float)) and isinstance(actual, (int, float)):
                if abs(actual - value) > 0.001:
                    return False
            elif isinstance(value, list) and isinstance(actual, list):
                if len(actual) != len(value):
                    return False
                for a, e in zip(actual, value):
                    if isinstance(e, (int, float)) and isinstance(a, (int, float)):
                        if abs(a - e) > 0.01:
                            return False
                    elif a != e:
                        return False
            elif actual != value:
                return False
        return True

    # ── Scenario runner ─────────────────────────────────────────

    def run_scenario(self, scenario: TestScenario) -> List[TestResult]:
        self.results = []
        print(f"\n{'='*60}")
        print(f"Scenario: {scenario.name}")
        print(f"{'='*60}")

        # ── Setup ───────────────────────────────────────────────
        for cmd in scenario.setup_commands:
            if self.verbose:
                print(f"  SETUP: {cmd}")
            self.send(cmd)
            time.sleep(0.3)

        try:
            for i, step in enumerate(scenario.steps, 1):
                output = ""
                json_data = None
                passed = False
                try:
                    print(f"\n  Step {i}: {step.description}")

                    if step.poll_state:
                        # ── polling mode ──────────────────────────
                        if self.verbose:
                            print(f"    ⏳ polling for STATE:{step.poll_state}", end="", flush=True)
                        found = self.wait_for_state(
                            step.poll_state,
                            timeout_ms=step.timeout_ms,
                            poll_interval_ms=step.poll_interval_ms,
                        )
                        if self.verbose:
                            print()
                        if found:
                            output = f"STATE:{step.poll_state}"
                            passed = True
                        else:
                            current = self.query_state()
                            output = f"TIMEOUT: still STATE:{current}"

                    else:
                        # ── send+check mode ───────────────────────
                        if step.command:
                            if self.verbose:
                                print(f"    → {step.command}")
                            self._flush()
                            self.send(step.command)
                        if step.wait_ms > 0:
                            time.sleep(step.wait_ms / 1000.0)

                        # Determine read timeout
                        read_to = step.timeout_ms
                        if step.command and not step.expect_contains and not step.expect_not_contains and not step.expect_json:
                            read_to = min(read_to, 400)

                        # ── JSON assertion mode ────────────────────
                        if step.expect_json is not None:
                            objs = self._read_json_lines(read_to)
                            output = '\n'.join(json.dumps(o) for o in objs) if objs else "(no JSON)"
                            if self.verbose:
                                for o in objs[:3]:
                                    print(f"    ← {json.dumps(o)}")
                            # Check all received JSON objects (first match wins)
                            for o in objs:
                                if self._check_json(o, step.expect_json):
                                    passed = True
                                    json_data = o
                                    break
                        elif not step.on_response:
                            # ── legacy string matching ─────────────
                            # Skip this when step has only on_response — the callback
                            # does its own serial reading (e.g. wait_for_json_event).
                            output = self._read_all(read_to)
                            if self.verbose and output:
                                for line in output.split('\n')[:5]:
                                    print(f"    ← {line}")

                            if step.expect_contains:
                                passed = step.expect_contains in output
                            elif step.expect_not_contains:
                                passed = step.expect_not_contains not in output
                            else:
                                # Step without assertions: check for error markers
                                passed = 'ERROR' not in output.upper() and 'FAIL' not in output.upper()

                    if step.on_response:
                        try:
                            cb_result = step.on_response(self, output)
                            # If step has only on_response (no JSON/string assertion),
                            # callback return value determines pass/fail.
                            if not step.expect_json and not step.expect_contains and not step.expect_not_contains:
                                if cb_result is not None:
                                    passed = bool(cb_result)
                        except Exception as e:
                            output += f'\nCallback error: {e}'
                            passed = False
                            if self.verbose:
                                print(f"    ❌ {e}")

                except Exception as e:
                    output = f"EXCEPTION: {e}"
                    passed = False
                    print(f"    ❌ {e}")

                result = TestResult(step=i, description=step.description,
                                   passed=passed, output=output, json_data=json_data)
                self.results.append(result)
                print(f"    {'✅ PASS' if passed else '❌ FAIL'}")

        finally:
            # ── Teardown ────────────────────────────────────────
            if scenario.teardown_commands:
                if self.verbose:
                    print(f"\n  TEARDOWN:")
                for cmd in scenario.teardown_commands:
                    if self.verbose:
                        print(f"    → {cmd}")
                    self.send(cmd)
                    time.sleep(0.2)
            # Always drain serial buffer after scenario
            self.drain_serial(300)

        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)
        print(f"\n  Results: {passed}/{total}")
        return self.results

    def all_passed(self) -> bool:
        return all(r.passed for r in self.results)


# ── Helpers ─────────────────────────────────────────────────────

def force_state(h: SGCTestHarness, state: str):
    cmds = {'SLEEP': 's', 'IDLE': 'i', 'ARMED': 'a', 'LOGGING': 'l', 'POST_RUN': 'p'}
    h.send(cmds.get(state.upper(), 'i'))
    time.sleep(0.3)

def enable_test_mode(h: SGCTestHarness) -> bool:
    """Ensure test mode is ON and injected values are at clean defaults.
    With JSON protocol, also waits for boot JSON to drain."""
    # Drain any boot JSON
    h.drain_serial(1000)
    # Toggle test mode
    h.send('T'); time.sleep(0.15)
    objs = h._read_json_lines(500)
    tm_on = any(o.get('tm') for o in objs if o.get('ev') == 'cmd')
    if not tm_on:
        h.send('T'); time.sleep(0.15)
        objs = h._read_json_lines(500)
        tm_on = any(o.get('tm') for o in objs if o.get('ev') == 'cmd')
    # Reset injected values to safe defaults
    h.send('L 0 0 0'); time.sleep(0.05)
    h.send('Q 1 0 0 0'); time.sleep(0.05)
    h.send('B 101325'); time.sleep(0.05)
    return tm_on

def wait_for_ring_full(h: SGCTestHarness, timeout_ms: int = 12000) -> bool:
    """Poll for ring_full JSON event.
    Returns True if ring_full event received, False on timeout.

    Usage in tests:
        TestStep("Wait for ring to fill", None, 100,
            on_response=lambda h, _: wait_for_ring_full(h)),
    """
    return h.wait_for_json_event("ring_full", timeout_ms=timeout_ms) is not None

def inject_pressure(h: SGCTestHarness, pa: float):
    h.send(f'B {pa}')

def inject_pressure_ramp(h: SGCTestHarness, start_pa: float, end_pa: float,
                          steps: int, step_delay_ms: int = 110) -> bool:
    delta = (end_pa - start_pa) / steps
    for i in range(steps + 1):
        h.send(f'B {start_pa + delta * i}')
        time.sleep(step_delay_ms / 1000.0)
    return True


def main():
    parser = argparse.ArgumentParser(description='SGC Test Harness v2 (JSON protocol)')
    parser.add_argument('scenario', nargs='?', help='Test scenario file')
    parser.add_argument('--port', default='auto', help='Serial port')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--list', action='store_true', help='List serial ports')
    parser.add_argument('-v', '--verbose', action='store_true', default=True)
    parser.add_argument('-o', '--output', default=None, help='Save summary to file (.txt or .md)')
    parser.add_argument('-l', '--log', default=None, help='Save full console output to log file')
    parser.add_argument('--ts', action='store_true',
        help='Timestamp output files: one file per run (e.g. results_20260622_1703.md)')
    parser.add_argument('--run-id', default=None, metavar='NAME',
        help='Run ID for multi-invocation aggregation. Summary .md appends across '
             'invocations; each test gets NAME_<test>.log.')
    args = parser.parse_args()

    if args.list:
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description} [{p.manufacturer}]")
        return

    if not args.scenario:
        print("Usage: python sgc_test_harness.py [--port COM3] <test_scenario.py>")
        return

    import importlib.util
    spec = importlib.util.spec_from_file_location("scenario", args.scenario)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    scenarios = getattr(mod, 'SCENARIOS', [])
    if not scenarios:
        print(f"ERROR: No SCENARIOS list found in {args.scenario}")
        return

    harness = SGCTestHarness(args.port, args.baud, args.verbose)
    if not harness.connect():
        return

    import datetime

    # ── Output naming ──────────────────────────────────────────
    run_stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M')
    if args.run_id:
        # Multi-invocation mode: summary .md appends, per-test .log files
        test_tag = os.path.splitext(os.path.basename(args.scenario))[0]
        if not args.output:
            args.output = f"{args.run_id}.md"
        if not args.log:
            args.log = f"{args.run_id}_{test_tag}.log"
    elif args.ts:
        # Single-invocation timestamped mode
        def _ts_name(base):
            name, ext = os.path.splitext(base)
            return f"{name}_{run_stamp}{ext}"
        if not args.output:
            args.output = f"results_{run_stamp}.md"
        else:
            args.output = _ts_name(args.output)
        if not args.log:
            args.log = f"results_{run_stamp}.log"
        else:
            args.log = _ts_name(args.log)

    # Determine output modes
    if args.run_id:
        out_mode = 'a'   # summary .md appends across invocations
        log_mode = 'w'   # per-test .log overwrite (idempotent re-run)
    elif args.ts:
        out_mode = 'w'
        log_mode = 'w'
    else:
        out_mode = 'a'   # legacy append
        log_mode = 'a'

    log_file = None
    if args.log:
        log_file = open(args.log, log_mode, encoding='utf-8')
        ts = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        log_file.write(f"{'='*60}\n")
        log_file.write(f"SGC Test: {args.scenario} — {ts}\n")
        log_file.write(f"{'='*60}\n\n")
        log_file.flush()
        original_stdout = sys.stdout
        sys.stdout = Tee(original_stdout, log_file)

    all_ok = True
    scenario_results = []

    try:
        for s in scenarios:
            results = harness.run_scenario(s)
            ok = harness.all_passed()
            if not ok:
                all_ok = False
            scenario_results.append((s.name, sum(1 for r in results if r.passed), len(results), ok))

        overall_msg = 'ALL TESTS PASSED ✅' if all_ok else 'SOME TESTS FAILED ❌'
        print(f"\n{'='*60}")
        print(f"OVERALL: {overall_msg}")

        if args.output:
            out_path = args.output
            if not out_path.endswith(('.txt', '.md')):
                out_path += '.md'
            ts = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            is_new = not os.path.exists(out_path) or (args.ts and not args.run_id)
            with open(out_path, out_mode, encoding='utf-8') as f:
                if is_new:
                    f.write(f"# SGC Test Results — {ts}\n\n")
                    f.write(f"**Port:** {harness.port}  \n\n")
                    f.write("| Test file | Result |\n")
                    f.write("|-----------|--------|\n")
                test_label = os.path.basename(args.scenario)
                if args.run_id and args.log:
                    log_name = os.path.basename(args.log)
                    test_label = f"[{test_label}]({log_name})"
                icon = '✅' if all_ok else '❌'
                total_p = sum(p for _, p, _, _ in scenario_results)
                total_t = sum(t for _, _, t, _ in scenario_results)
                f.write(f"| {test_label} | {icon} {total_p}/{total_t} |\n")
            verb = 'Saved to' if (args.ts and not args.run_id) else ('appended to' if not is_new else 'saved to')
            print(f"Results {verb} {out_path}")

        sys.exit(0 if all_ok else 1)
    finally:
        if log_file:
            sys.stdout = original_stdout
            log_file.close()
        harness.disconnect()


if __name__ == '__main__':
    main()
