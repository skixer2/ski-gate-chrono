"""
SGC Unit Test Harness v2.20 — JSON-lines protocol + structured assertions.

Usage:
    python sgc_test_harness.py --port COM3 test_start_detector.py
    python sgc_test_harness.py --port auto test_bit_packer.py

Architecture:
    Python ──(serial USB)──▶ SGC Device in TEST_MODE + JSON mode
    Sends inject commands (B/Q/L), reads JSON-lines responses.

Aligned with FW ≥4.80 / Opt-A linear pre-roll:
    rm = ARM_FILL_CAP = 3000 (~30 s @ 100 Hz)
    PREROLL_KEEP = 1000 (~10 s kept at LOGGING entry)
    ring_full fires only at r==3000 (races ARM_TIMEOUT) — prefer
    wait_for_ring_count() for unit tests.
    B / echo p are Pascals. Manual B/Q/L suppress ARM→stream.

Protocol: see json_protocol.md for full spec.
"""

HARNESS_VERSION = "2.22.0"
# Unit scenarios require FW ≥ this (manual_frame survives POST_RUN, stream cleared on IDLE).
MIN_FW_VERSION = (4, 81)

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
            print(f"SGC Test Harness v{HARNESS_VERSION}")
            print(f"Connected to {self.port} at {self.baud} baud")
            self._check_firmware_version()
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

    @staticmethod
    def _parse_ver_tuple(ver: str):
        """Parse '4.80' / '4.80-foo' → (4, 80). Returns None on failure."""
        if not ver:
            return None
        parts = []
        for tok in str(ver).replace('-', '.').split('.'):
            digits = ''
            for ch in tok:
                if ch.isdigit():
                    digits += ch
                else:
                    break
            if digits:
                parts.append(int(digits))
            if len(parts) >= 2:
                break
        if not parts:
            return None
        while len(parts) < 2:
            parts.append(0)
        return tuple(parts[:2])

    def _check_firmware_version(self):
        """Warn loudly if device FW is older than unit-test contract."""
        try:
            self.send('V')
            time.sleep(0.2)
            objs = self._read_json_lines(800)
            ver = None
            for o in objs:
                if o.get('ev') in ('version', 'boot', 'status') and o.get('ver'):
                    ver = o.get('ver')
                    break
            if ver is None:
                st = self.query_status_json()
                if st:
                    ver = st.get('ver')
            vt = self._parse_ver_tuple(ver) if ver else None
            if vt is None:
                print(f"WARNING: could not read FW version (got {ver!r})")
                return
            print(f"Device FW version: {ver}")
            if vt < MIN_FW_VERSION:
                need = '.'.join(str(x) for x in MIN_FW_VERSION)
                print('=' * 60)
                print(f"ERROR: unit tests need FW ≥ {need}, device reports {ver}")
                print("Flash Firmware_implementation (config.h FW_VERSION) first.")
                print("S03–S06 may still pass on older FW; unit scenarios will fail.")
                print('=' * 60)
        except Exception as e:
            print(f"WARNING: FW version check failed: {e}")

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
        """Read serial lines until overall deadline.

        Unlike a pure gap-break reader, keep waiting through quiet periods
        (prepare_preroll erase can silence USB for 0.5–2 s between st and
        preroll_prep). Stop early only after we already have data AND then
        see a short empty gap.
        """
        if not self.ser:
            return ""
        # Per-line timeout: short so we can poll; overall deadline is authoritative.
        self.ser.timeout = 0.2
        lines = []
        deadline = time.time() + timeout_ms / 1000.0
        empty_after_data = 0
        while time.time() < deadline:
            try:
                line = self.ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    lines.append(line)
                    empty_after_data = 0
                elif lines:
                    empty_after_data += 1
                    # ~3×200 ms quiet after first payload → burst finished
                    if empty_after_data >= 3:
                        break
                # else: still waiting for first byte — keep until deadline
            except Exception:
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
            # Numeric tolerance (Arduino floats + Pa/2 quantization on p)
            if isinstance(value, (int, float)) and isinstance(actual, (int, float)):
                # baro is stored as Pa/2 uint16 → echo/status p snaps to even Pa
                tol = 1.0 if key in ('p', 'pa', 'p0') else 0.001
                if abs(float(actual) - float(value)) > tol:
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
                # JSON bools often arrive as 0/1
                if isinstance(value, bool) and actual in (0, 1):
                    if bool(actual) != value:
                        return False
                else:
                    return False
        return True

    # ── Scenario runner ─────────────────────────────────────────

    def run_scenario(self, scenario: TestScenario) -> List[TestResult]:
        self.results = []
        print(f"\n{'='*60}")
        print(f"Scenario: {scenario.name}")
        print(f"{'='*60}")

        # ── Setup ───────────────────────────────────────────────
        # prepare_preroll erases 20 flash sectors on ARMED→IDLE (~0.5–2 s).
        # Short sleeps drop the following 'a' into the erase window.
        for cmd in scenario.setup_commands:
            if self.verbose:
                print(f"  SETUP: {cmd}")
            self.send(cmd)
            settle = 1.5 if str(cmd).strip().lower() in ('i', 'a', 'p', 'l') else 0.4
            time.sleep(settle)
            self.drain_serial(200)

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

                        # Determine read timeout. State transitions that may
                        # emit preroll_prep (ARMED→IDLE erase) need longer.
                        # flash self-test = 2× sector erase + program.
                        read_to = step.timeout_ms
                        cmd0 = (step.command or '').strip()[:1].lower()
                        if step.expect_json is not None and cmd0 in ('i', 'a', 'p'):
                            read_to = max(read_to, 4000)
                        if step.expect_json is not None and cmd0 == 'f':
                            read_to = max(read_to, 5000)
                        if step.command and not step.expect_contains and not step.expect_not_contains and not step.expect_json:
                            read_to = min(read_to, 400)

                        # ── JSON assertion mode ────────────────────
                        if step.expect_json is not None:
                            objs = self._read_json_lines(read_to)
                            # If expect matches a later line (st after preroll_prep),
                            # keep reading briefly for multi-event bursts.
                            if not any(self._check_json(o, step.expect_json) for o in objs):
                                extra = self._read_json_lines(1500)
                                if extra:
                                    objs.extend(extra)
                            output = '\n'.join(json.dumps(o) for o in objs) if objs else "(no JSON)"
                            if self.verbose:
                                for o in objs[:8]:
                                    print(f"    ← {json.dumps(o)}")
                            # Check all received JSON objects (first match wins)
                            for o in objs:
                                if self._check_json(o, step.expect_json):
                                    passed = True
                                    json_data = o
                                    break
                            # Fallback: ARMED→IDLE may drop st on USB during
                            # long erase; if we saw preroll_prep or device is
                            # already IDLE, accept the expected st transition.
                            if (not passed and cmd0 == 'i'
                                    and isinstance(step.expect_json, dict)
                                    and step.expect_json.get('ev') == 'st'
                                    and step.expect_json.get('to') == 'IDLE'):
                                if any(o.get('ev') == 'preroll_prep' for o in objs):
                                    passed = True
                                else:
                                    st_now = self.query_state()
                                    if st_now == 'IDLE':
                                        passed = True
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

# Opt-A linear pre-roll (must match flash_ring.h)
ARM_FILL_CAP = 3000   # status rm; ring_full at this count
PREROLL_KEEP = 1000   # frames kept at LOGGING entry / S05 default target
# Unit tests: enough pre-roll without racing ARM_TIMEOUT (30 s)
UNIT_RING_READY = 200


def force_state(h: SGCTestHarness, state: str):
    cmds = {'SLEEP': 's', 'IDLE': 'i', 'ARMED': 'a', 'LOGGING': 'l', 'POST_RUN': 'p'}
    cmd = cmds.get(state.upper(), 'i')
    h.send(cmd)
    # IDLE may run prepare_preroll (20 sector erase)
    time.sleep(1.5 if cmd == 'i' else 0.4)
    h.drain_serial(200)

def enable_test_mode(h: SGCTestHarness) -> bool:
    """Ensure test mode is ON and injected values are at clean defaults.

    Ends with B/Q/L so g_manual_frame=true → ARM does NOT open stream
    (unit tests inject; S03 stream path uses T without B/Q/L, or S).

    Always re-asserts B/Q/L even if tm was already on — critical after a
    prior run on FW≤4.80 where POST_RUN cleared g_manual_frame, and still
    good hygiene on 4.81+ so every scenario starts from known inject state.
    """
    # Return to IDLE first so any sticky stream is dropped (FW≥4.81).
    h.send('i')
    time.sleep(0.4)
    h.drain_serial(800)
    # Toggle test mode
    h.send('T'); time.sleep(0.15)
    objs = h._read_json_lines(500)
    tm_on = any(o.get('tm') for o in objs if o.get('ev') == 'cmd')
    if not tm_on:
        h.send('T'); time.sleep(0.15)
        objs = h._read_json_lines(500)
        tm_on = any(o.get('tm') for o in objs if o.get('ev') == 'cmd')
    # Reset injected values to safe defaults (Pa). Order: Q, L, B last
    # so manual_frame stays set and baro is valid for start det.
    h.send('Q 1 0 0 0'); time.sleep(0.05)
    h.send('L 0 0 0'); time.sleep(0.05)
    h.send('B 101325'); time.sleep(0.08)
    h.drain_serial(300)
    return tm_on

def wait_for_ring_full(h: SGCTestHarness, timeout_ms: int = 35000) -> bool:
    """Poll for ring_full JSON event (r reaches ARM_FILL_CAP=3000).

    Note: full fill races ARM_TIMEOUT 30 s @ 100 Hz. Prefer
    wait_for_ring_count() for unit tests unless you truly need cap.
    """
    return h.wait_for_json_event("ring_full", timeout_ms=timeout_ms) is not None


def wait_for_ring_count(h: SGCTestHarness, min_r: int = UNIT_RING_READY,
                        timeout_ms: int = 15000,
                        require_armed: bool = True) -> bool:
    """Poll status until ring count >= min_r (and optionally still ARMED).

    Default min_r=UNIT_RING_READY (~2 s) — enough for start-det / force-l
    tests without waiting for full 3000 or ring_full.
    """
    deadline = time.time() + timeout_ms / 1000.0
    while time.time() < deadline:
        st = h.query_status_json()
        if st:
            r = int(st.get('r') or 0)
            state = st.get('st')
            if h.verbose:
                print(f"    … r={r}/{st.get('rm')} st={state}", flush=True)
            if require_armed and state != 'ARMED':
                if h.verbose:
                    print(f"  ⏰ left ARMED (st={state}) before r>={min_r}")
                return False
            if r >= min_r:
                return True
        time.sleep(0.25)
    if h.verbose:
        print(f"  ⏰ timeout waiting for r>={min_r}")
    return False


def expect_status(h: SGCTestHarness, **kwargs) -> bool:
    """Fetch status and check key==value (numeric tolerance 0.001)."""
    st = h.query_status_json()
    if not st:
        return False
    if h.verbose:
        print(f"    ← status {json.dumps(st)}")
    for k, v in kwargs.items():
        if k not in st:
            return False
        actual = st[k]
        if isinstance(v, (int, float)) and isinstance(actual, (int, float)):
            if abs(actual - v) > 0.001:
                return False
        elif actual != v:
            return False
    return True


def inject_pressure(h: SGCTestHarness, pa: float):
    """Inject barometric pressure in Pascals."""
    h.send(f'B {pa}')

def inject_pressure_ramp(h: SGCTestHarness, start_pa: float, end_pa: float,
                          steps: int, step_delay_ms: int = 110) -> bool:
    """Ramp pressure in Pascals (start detector / end detector stimulus)."""
    delta = (end_pa - start_pa) / steps
    for i in range(steps + 1):
        h.send(f'B {start_pa + delta * i}')
        time.sleep(step_delay_ms / 1000.0)
    return True


def enter_logging_via_start(h: SGCTestHarness,
                            p0: float = 101325.0,
                            drop_pa: float = 36.0) -> bool:
    """Natural ARMED→LOGGING via start detector (keeps end det active).

    force-'l' sets g_force_logging and SKIPS end detector (S04). Tests that
    need end detection must enter LOGGING this way, not via 'l'.
    drop_pa default +36 Pa ≈ 3 m > 2 m threshold.
    """
    if not wait_for_ring_count(h, min_r=50, timeout_ms=8000):
        return False
    inject_pressure_ramp(h, p0, p0 + drop_pa, 12, 100)
    return h.wait_for_state('LOGGING', timeout_ms=8000)


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
    # Device system tests (S04–S06): define run_device_test(h) -> bool instead of SCENARIOS
    run_device_test = getattr(mod, 'run_device_test', None)
    if not scenarios and run_device_test is None:
        print(f"ERROR: No SCENARIOS list or run_device_test() in {args.scenario}")
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
        if run_device_test is not None and not scenarios:
            # Standalone device system test (S04/S05/S06 wrappers).
            # run_device_test owns the port for the duration — release harness first.
            port = harness.port
            harness.disconnect()
            print(f"\n{'='*60}")
            print(f"DEVICE SYSTEM TEST: {os.path.basename(args.scenario)}")
            print(f"{'='*60}")
            try:
                ok = bool(run_device_test(port))
            except Exception as e:
                print(f"EXCEPTION in run_device_test: {e}")
                traceback.print_exc()
                ok = False
            all_ok = ok
            name = getattr(mod, 'TEST_NAME', os.path.basename(args.scenario))
            scenario_results.append((name, 1 if ok else 0, 1, ok))
            # Reconnect only if needed for clean exit messaging
            try:
                harness.connect()
            except Exception:
                pass
        else:
            for s in scenarios:
                results = harness.run_scenario(s)
                ok = harness.all_passed()
                if not ok:
                    all_ok = False
                scenario_results.append(
                    (s.name, sum(1 for r in results if r.passed), len(results), ok))

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
            verb = 'Saved to' if (args.ts and not args.run_id) else (
                'appended to' if not is_new else 'saved to')
            print(f"Results {verb} {out_path}")

        sys.exit(0 if all_ok else 1)
    finally:
        if log_file:
            sys.stdout = original_stdout
            log_file.close()
        try:
            harness.disconnect()
        except Exception:
            pass


if __name__ == '__main__':
    main()
