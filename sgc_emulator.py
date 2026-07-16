#!/usr/bin/env python3
"""
SGC Device Emulator — Fake GS Run Over Serial

Pretends to be an SGC Nicla Sense ME device. Talks the full JSON-lines protocol
over a PTY, including test-mode sensor injection (B/Q/L commands) and
auto-generated synthetic sensor data.

Usage:
    # Auto-create PTY (prints slave path for test harness):
    python sgc_emulator.py

    # Connect to existing serial port:
    python sgc_emulator.py --port /dev/ttyUSB0

    # Manual PTY pair (connect harness to the printed slave path):
    python sgc_emulator.py --pty

Then, in another terminal:
    python sgc_test_harness.py --port /dev/pts/<N> test_full_run.py

    # Or just talk manually:
    screen /dev/pts/<N> 115200

─────────────────────────────────────────────────────────────────────
GS Run Simulation Parameters (all configurable):
  --drop 150         Vertical drop in meters
  --duration 45      Run duration in seconds
  --gates 30         Number of gates
  --fps 100          Sensor sample rate (Hz)
  --cooldown 10      Post-run flatline duration before end detection
  --start-alt 2200   Starting altitude (m)
"""

import os
import sys
import pty
import math
import json
import time
import struct
import signal
import select
import termios
import argparse
import threading
from typing import Optional, Dict, Any, List
from dataclasses import dataclass, field
from datetime import datetime, timezone


# ═══════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════
@dataclass
class EmulatorConfig:
    drop_m: float = 150.0
    duration_s: float = 45.0
    num_gates: int = 30
    gate_spacing_m: float = 25.0
    fps: int = 100
    cooldown_s: float = 10.0
    start_altitude_m: float = 2200.0
    turn_g_force: float = 2.5
    ring_buffer_size: int = 500
    baud: int = 115200
    post_run_duration_s: float = 5.0
    arm_to_idle_timeout_s: float = 60.0
    postrun_cooldown_s: float = 15.0


# ═══════════════════════════════════════════════════════════════════
# Physics Helpers
# ═══════════════════════════════════════════════════════════════════
P0_SEA_LEVEL_PA = 101325.0


def altitude_to_pa(alt_m: float) -> float:
    return P0_SEA_LEVEL_PA * (1 - 2.25577e-5 * alt_m) ** 5.25588


def quat_from_euler(roll: float, pitch: float, yaw: float):
    cy = math.cos(yaw * 0.5)
    sy = math.sin(yaw * 0.5)
    cp = math.cos(pitch * 0.5)
    sp = math.sin(pitch * 0.5)
    cr = math.cos(roll * 0.5)
    sr = math.sin(roll * 0.5)
    w = cr * cp * cy + sr * sp * sy
    x = sr * cp * cy - cr * sp * sy
    y = cr * sp * cy + sr * cp * sy
    z = cr * cp * sy - sr * sp * cy
    return w, x, y, z


# ═══════════════════════════════════════════════════════════════════
# Synthetic Sensor Frame Generator
# ═══════════════════════════════════════════════════════════════════
class GSFrameGenerator:
    """Generates realistic sensor frames for a Giant Slalom run."""

    def __init__(self, cfg: EmulatorConfig):
        self.cfg = cfg
        self.num_frames = int(cfg.duration_s * cfg.fps)
        self.dt = 1.0 / cfg.fps
        self.end_alt = cfg.start_altitude_m - cfg.drop_m
        # Gate positions (evenly spaced in time)
        self.gate_frames = [
            int((i + 1) * self.num_frames / (cfg.num_gates + 1))
            for i in range(cfg.num_gates)
        ]
        self.frame_idx = 0

    def reset(self):
        self.frame_idx = 0

    def next_frame(self) -> Dict[str, Any]:
        """Generate one frame of synthetic sensor data."""
        i = self.frame_idx
        t = i * self.dt
        cfg = self.cfg

        # ── Altitude: smooth descent with micro-variation ─────
        fraction = i / self.num_frames if self.num_frames > 0 else 0
        terrain_variation = 0.5 * math.sin(fraction * 12 * math.pi)
        altitude = cfg.start_altitude_m - fraction * cfg.drop_m + terrain_variation

        # ── Pressure ─────────────────────────────────────────
        pa = altitude_to_pa(altitude)

        # ── Quaternion: slalom turns ─────────────────────────
        gate_idx = None
        for g, gf in enumerate(self.gate_frames):
            dist = abs(i - gf)
            if dist <= 15:
                gate_idx = g
                break

        if gate_idx is not None and gate_idx % 2 == 0:
            progress = (i - self.gate_frames[gate_idx]) / 20.0
            yaw = progress * 0.8
            roll = 0.3 * math.exp(-progress * progress * 3)
            pitch = -0.05
        elif gate_idx is not None:
            progress = (i - self.gate_frames[gate_idx]) / 20.0
            yaw = -progress * 0.8
            roll = -0.3 * math.exp(-progress * progress * 3)
            pitch = -0.05
        else:
            yaw = 0.0
            roll = 0.0
            pitch = -0.05

        qw, qx, qy, qz = quat_from_euler(roll, pitch, yaw)

        # ── Acceleration ─────────────────────────────────────
        g = 9.81
        grav_x = 2 * (qx * qz - qw * qy) * g
        grav_y = 2 * (qw * qx + qy * qz) * g
        grav_z = (qw * qw - qx * qx - qy * qy + qz * qz) * g

        cent_x = 0.0
        cent_y = 0.0
        if gate_idx is not None:
            progress = (i - self.gate_frames[gate_idx]) / 20.0
            intensity = math.exp(-progress * progress * 2)
            sign = 1 if gate_idx % 2 == 0 else -1
            cent_x = sign * cfg.turn_g_force * g * intensity
            cent_y = 0.3 * cfg.turn_g_force * g * intensity

        noise_scale = 2.0
        noise_x = noise_scale * (2 * ((i * 0.7) % 1 - 0.5))
        noise_y = noise_scale * (2 * ((i * 0.3 + 0.5) % 1 - 0.5))
        noise_z = noise_scale * 0.5 * (2 * ((i * 0.9 + 0.2) % 1 - 0.5))

        la_x = grav_x + cent_x + noise_x
        la_y = grav_y + cent_y + noise_y
        la_z = grav_z + noise_z

        self.frame_idx += 1
        return {
            "q": [round(qw, 6), round(qx, 6), round(qy, 6), round(qz, 6)],
            "la": [round(la_x, 4), round(la_y, 4), round(la_z, 4)],
            "p": round(pa, 2),
        }


class FlatlineGenerator:
    """Generates 10s of 'no movement' sensor data for end detection."""

    def __init__(self, last_frame: Dict[str, Any], cfg: EmulatorConfig):
        self.cfg = cfg
        self.num_frames = int(cfg.cooldown_s * cfg.fps)
        # Flat pressure at end altitude
        self.static_pa = last_frame["p"]
        self.static_q = [1.0, 0.0, 0.0, 0.0]  # neutral orientation
        self.static_la = [0.0, 0.0, 9.81]     # gravity only, upright
        self.frame_idx = 0

    def next_frame(self) -> Dict[str, Any]:
        i = self.frame_idx
        # Tiny noise so it's not perfectly flat
        noise = 0.05
        self.frame_idx += 1
        return {
            "q": self.static_q,
            "la": [0.0, 0.0, 9.81 + noise * (2 * ((i * 0.3) % 1 - 0.5))],
            "p": self.static_pa + noise * (2 * ((i * 0.5) % 1 - 0.5)),
        }

    @property
    def done(self) -> bool:
        return self.frame_idx >= self.num_frames


# ═══════════════════════════════════════════════════════════════════
# SGC Device State Machine
# ═══════════════════════════════════════════════════════════════════
class SGCStateMachine:
    """Emulates the SGC firmware state machine."""
    STATES = ["SLEEP", "IDLE", "ARMED", "LOGGING", "POST_RUN"]

    def __init__(self, cfg: EmulatorConfig):
        self.cfg = cfg
        self.state = "IDLE"
        self.test_mode = False
        self.run_count = 3  # pretend we have some stored runs

        # Injected sensor values (test mode)
        self.inj_pressure = P0_SEA_LEVEL_PA
        self.inj_quat = [1.0, 0.0, 0.0, 0.0]
        self.inj_la = [0.0, 0.0, 9.81]

        # Ring buffer
        self.ring_count = 0
        self.ring_max = cfg.ring_buffer_size

        # Timing
        self.arm_timestamp: Optional[float] = None
        self.postrun_timestamp: Optional[float] = None
        self.logging_started_at: Optional[float] = None
        self.battery_pct = 85

        # Run data
        self.current_run_id = 0
        self.frame_count = 0
        self.pre_trigger_frames = 0
        self.start_triggered = False

        # Sensor generators
        self.gs_generator: Optional[GSFrameGenerator] = None
        self.flatline_generator: Optional[FlatlineGenerator] = None
        self.logging_phase = "none"  # none | gs_run | flatline
        self.logging_frame_count = 0
        self.flatline_frame_count = 0
        self.frame_interval = 1.0 / cfg.fps

    # ── State transitions ────────────────────────────────────────

    def transition(self, new_state: str) -> Optional[Dict]:
        old = self.state
        if old == new_state:
            return None
        if new_state not in self.STATES:
            return {"ev": "error", "reason": f"invalid_state:{new_state}"}

        self.state = new_state

        if new_state == "ARMED":
            self.arm_timestamp = time.time()
            self.ring_count = 0
            self.start_triggered = False

        elif new_state == "LOGGING":
            self.logging_started_at = time.time()
            self.current_run_id = self.run_count + 1
            self.run_count += 1
            self.frame_count = 0
            self.pre_trigger_frames = self.ring_count
            self.logging_phase = "gs_run"
            self.gs_generator = GSFrameGenerator(self.cfg)
            self.gs_generator.reset()
            self.logging_frame_count = 0

        elif new_state == "POST_RUN":
            self.postrun_timestamp = time.time()
            self.logging_phase = "none"

        elif new_state == "IDLE":
            self.arm_timestamp = None
            self.postrun_timestamp = None
            self.logging_phase = "none"
            self.gs_generator = None
            self.flatline_generator = None

        elif new_state == "SLEEP":
            self.arm_timestamp = None
            self.postrun_timestamp = None
            self.logging_phase = "none"

        return {"ev": "st", "from": old, "to": new_state}

    # ── Command processing ───────────────────────────────────────

    def process_command(self, cmd: str) -> List[Dict]:
        """Process a single serial command. Returns list of JSON responses."""
        cmd = cmd.strip()
        responses = []

        if not cmd:
            return responses

        # Split command and args
        parts = cmd.split()
        raw_op = parts[0]
        op = raw_op.lower()
        args = parts[1:]

        # ── Universal commands ──────────────────────────────────
        if op == '?':
            responses.append(self._status())
            return responses

        # ── Test mode toggle 'T' (always allowed) ───────────────
        if raw_op == 'T':
            self.test_mode = not self.test_mode
            responses.append({"ev": "cmd", "cmd": "T", "tm": self.test_mode,
                            "p": self.inj_pressure, "q": self.inj_quat, "la": self.inj_la})
            # Echo current values
            responses.append({"ev": "echo", "p": self.inj_pressure,
                            "q": self.inj_quat, "la": self.inj_la})
            return responses

        # ── Test-mode inject commands (uppercase, require test mode ON) ─
        elif raw_op in ('B', 'Q', 'L', 'Z'):
            if not self.test_mode:
                responses.append({"ev": "state_blocked",
                                "reason": "test_mode_required", "current": self.state})
                return responses

            if raw_op == 'B' and args:
                self.inj_pressure = float(args[0])
                responses.append({"ev": "cmd", "cmd": "B", "p": self.inj_pressure})
            elif raw_op == 'Q' and len(args) >= 4:
                self.inj_quat = [float(args[0]), float(args[1]),
                                float(args[2]), float(args[3])]
                responses.append({"ev": "cmd", "cmd": "Q", "q": self.inj_quat})
            elif raw_op == 'L' and len(args) >= 3:
                self.inj_la = [float(args[0]), float(args[1]), float(args[2])]
                responses.append({"ev": "cmd", "cmd": "L", "la": self.inj_la})
            elif raw_op == 'Z':
                self.inj_pressure = P0_SEA_LEVEL_PA
                self.inj_quat = [1.0, 0.0, 0.0, 0.0]
                self.inj_la = [0.0, 0.0, 9.81]
                responses.append({"ev": "cmd", "cmd": "Z"})
            return responses

        # ── State-changing commands (lowercase) ──────────────────

        elif op == 'a':  # arm
            if self.state == "POST_RUN":
                elapsed = time.time() - (self.postrun_timestamp or 0)
                if elapsed < self.cfg.postrun_cooldown_s:
                    responses.append({"ev": "arm_blocked", "reason": "cooldown"})
                    return responses
            if self.state != "IDLE":
                responses.append({"ev": "state_blocked",
                                "reason": "not_idle", "current": self.state})
                return responses
            tr = self.transition("ARMED")
            if tr:
                responses.append(tr)
            # Simulate ring buffer filling (happens in background on real device)
            # We'll fill it incrementally in the event loop
            return responses

        elif op == 'i':  # idle
            if self.state == "SLEEP":
                responses.append(self.transition("IDLE") or {"ev": "st", "from": "SLEEP", "to": "IDLE"})
            elif self.state != "IDLE":
                tr = self.transition("IDLE")
                if tr:
                    responses.append(tr)
            else:
                responses.append({"ev": "status", "st": "IDLE"})
            return responses

        elif op == 's':  # sleep
            tr = self.transition("SLEEP")
            if tr:
                responses.append(tr)
            return responses

        elif op == 'l' and self.state == "ARMED":  # explicit log start
            # In production this is detected automatically, but allow manual trigger
            self.start_triggered = True
            # Simulate start event
            delta_pa = P0_SEA_LEVEL_PA - self.inj_pressure
            responses.append({"ev": "start", "mode": "drop", "pa": round(delta_pa, 1)})
            tr = self.transition("LOGGING")
            if tr:
                responses.append(tr)
            self.pre_trigger_frames = self.ring_count
            responses.append({"ev": "log_start", "run": self.current_run_id,
                            "pre": self.pre_trigger_frames})
            return responses

        elif op == 'p':  # post-run
            if self.state == "LOGGING":
                tr = self.transition("POST_RUN")
                if tr:
                    responses.append(tr)
                responses.append({"ev": "end_detected", "fr": self.frame_count})
            return responses

        # ── Flash commands ───────────────────────────────────────

        elif op == 'f':
            responses.append({"ev": "flash", "ok": True})
            return responses

        elif op == 'r':
            responses.append({"ev": "factory_reset"})
            self.run_count = 0
            self.state = "IDLE"
            responses.append({"ev": "reboot"})
            time.sleep(0.5)
            responses.append({"ev": "boot", "ver": "2.3-emu"})
            responses.append({"ev": "init", "sub": "flash", "ok": True})
            responses.append({"ev": "index", "runs": 0, "next": 0})
            responses.append({"ev": "init", "sub": "bhy2", "ok": True})
            responses.append({"ev": "init", "sub": "ble", "ok": True})
            responses.append({"ev": "ready", "st": "IDLE", "runs": 0})
            return responses

        # ── Unknown ──────────────────────────────────────────────

        else:
            responses.append({"ev": "error", "reason": f"unknown_cmd:{op}"})
            return responses

    def _status(self) -> Dict:
        """Build a status JSON object."""
        return {
            "ev": "status",
            "st": self.state,
            "r": self.ring_count,
            "rm": self.ring_max,
            "p": round(self.inj_pressure, 2),
            "bat": self.battery_pct,
            "evc": 0,
            "qi": 0,
            "runs": self.run_count,
        }

    # ── Background update (called from main loop) ────────────────

    def update(self) -> List[Dict]:
        """Called every loop iteration. Handles background tasks:
        - Ring buffer filling in ARMED state
        - Synthetic frame generation in LOGGING state
        - Timeout checks
        - End detection (flatline completion → POST_RUN)
        """
        responses = []
        now = time.time()

        # ── ARMED: fill ring buffer ──────────────────────────────
        if self.state == "ARMED" and self.ring_count < self.ring_max:
            self.ring_count += 1
            if self.ring_count == self.ring_max:
                responses.append({"ev": "ring_full", "r": self.ring_count})

        # ── ARMED: auto-timeout ──────────────────────────────────
        if self.state == "ARMED" and self.arm_timestamp:
            if now - self.arm_timestamp > self.cfg.arm_to_idle_timeout_s:
                responses.append({"ev": "timeout", "from": "ARMED", "to": "IDLE"})
                self.transition("IDLE")

        # ── LOGGING: generate frames ─────────────────────────────
        if self.state == "LOGGING":
            if self.logging_phase == "gs_run" and self.gs_generator:
                frame = self.gs_generator.next_frame()
                self.logging_frame_count += 1
                self.frame_count += 1
                # Stream frame as JSON
                responses.append({"ev": "frame", "fr": self.frame_count,
                                "q": frame["q"], "la": frame["la"],
                                "p": frame["p"]})
                # Check if GS run is complete
                if self.logging_frame_count >= self.gs_generator.num_frames:
                    # Switch to flatline phase
                    self.logging_phase = "flatline"
                    self.flatline_generator = FlatlineGenerator(frame, self.cfg)
                    self.flatline_frame_count = 0

            elif self.logging_phase == "flatline" and self.flatline_generator:
                frame = self.flatline_generator.next_frame()
                self.flatline_frame_count += 1
                self.frame_count += 1
                responses.append({"ev": "frame", "fr": self.frame_count,
                                "q": frame["q"], "la": frame["la"],
                                "p": frame["p"]})
                # Check if flatline is complete → POST_RUN
                if self.flatline_generator.done:
                    tr = self.transition("POST_RUN")
                    if tr:
                        responses.append(tr)
                    responses.append({"ev": "end_detected", "fr": self.frame_count})
                    # Save run
                    compressed_size = self.frame_count * 24  # rough estimate
                    responses.append({"ev": "run_saved", "id": self.current_run_id,
                                    "fr": self.frame_count, "sz": compressed_size,
                                    "cal": 3})

        # ── POST_RUN: auto-cooldown to IDLE ──────────────────────
        if self.state == "POST_RUN" and self.postrun_timestamp:
            if now - self.postrun_timestamp > self.cfg.postrun_cooldown_s:
                responses.append({"ev": "cooldown", "from": "POST_RUN", "to": "IDLE"})
                self.transition("IDLE")

        return responses


# ═══════════════════════════════════════════════════════════════════
# Boot Sequence
# ═══════════════════════════════════════════════════════════════════
BOOT_SEQUENCE = [
    {"ev": "boot", "ver": "2.3-emu"},
    {"ev": "init", "sub": "flash", "ok": True},
    {"ev": "index", "runs": 3, "next": 13946880},
    {"ev": "init", "sub": "bhy2", "ok": True},
    {"ev": "init", "sub": "ble", "ok": True},
    {"ev": "ready", "st": "IDLE", "runs": 3},
]


# ═══════════════════════════════════════════════════════════════════
# PTY Serial Emulator Core
# ═══════════════════════════════════════════════════════════════════

class PTYSerialEmulator:
    """Creates a PTY pair, implements the SGC protocol on the master side.
    The slave side can be opened by any serial terminal or test harness."""

    def __init__(self, cfg: EmulatorConfig):
        self.cfg = cfg
        self.sm = SGCStateMachine(cfg)
        self.master_fd: Optional[int] = None
        self.slave_name: Optional[str] = None
        self.running = False
        self.buffer = b""
        self.verbose = True
        self.realtime = False
        # For throttling the main loop
        self.last_loop = 0.0
        self.loop_interval = 0.001  # 1ms base tick

    def start(self, slave_name: Optional[str] = None) -> str:
        """Open the PTY. Returns the slave device path."""
        if slave_name:
            # Connect to existing PTY (given as --port)
            self.master_fd = os.open(slave_name, os.O_RDWR | os.O_NOCTTY)
            self.slave_name = slave_name
            self._slave_fd = None
        else:
            # Create new PTY pair
            master_fd, slave_fd = pty.openpty()
            self.master_fd = master_fd
            self.slave_name = os.ttyname(slave_fd)
            # Keep slave fd open — closing it removes /dev/pts/N on some systems
            self._slave_fd = slave_fd

        # Try to increase PTY buffer size to avoid overflow during burst writes
        try:
            import fcntl
            F_SETPIPE_SZ = 1031  # fcntl constant
            # Max allowed pipe size
            max_pipe = 1048576  # 1MB
            fcntl.fcntl(self.master_fd, F_SETPIPE_SZ, max_pipe)
        except Exception:
            pass  # not all systems support this

        # Configure terminal attributes on master
        attrs = termios.tcgetattr(self.master_fd)
        attrs[3] = attrs[3] & ~termios.ECHO   # no echo
        attrs[6][termios.VMIN] = 1            # read at least 1 char
        attrs[6][termios.VTIME] = 0           # no timeout
        termios.tcsetattr(self.master_fd, termios.TCSANOW, attrs)

        # Set baud-like speed (cosmetic for PTY, but some tools check)
        try:
            termios.cfsetspeed(attrs, self.cfg.baud)
            termios.tcsetattr(self.master_fd, termios.TCSANOW, attrs)
        except Exception:
            pass

        return self.slave_name

    def send_boot_sequence(self):
        """Send the boot sequence JSON lines."""
        for obj in BOOT_SEQUENCE:
            self._write_json(obj)
            time.sleep(0.05)

    def _write_json(self, obj: Dict):
        """Write a single JSON line to the PTY."""
        line = json.dumps(obj, separators=(',', ':')) + '\n'
        self._write_raw(line)

    def _write_raw(self, data: str):
        """Write raw bytes to the PTY."""
        if self.master_fd is not None:
            try:
                os.write(self.master_fd, data.encode('utf-8'))
            except (OSError, BrokenPipeError):
                pass

    def _read_available(self) -> str:
        """Read any available data from the PTY without blocking."""
        if self.master_fd is None:
            return ""
        try:
            r, _, _ = select.select([self.master_fd], [], [], 0)
            if r:
                data = os.read(self.master_fd, 4096)
                return data.decode('utf-8', errors='replace')
        except (OSError, BrokenPipeError):
            self.running = False
        return ""

    def _process_buffer(self) -> List[str]:
        """Extract complete lines from the input buffer."""
        lines = []
        while b'\n' in self.buffer or b'\r' in self.buffer:
            # Split on newline or carriage return
            idx = -1
            nl = self.buffer.find(b'\n')
            cr = self.buffer.find(b'\r')
            if nl >= 0 and (cr < 0 or nl < cr):
                idx = nl
            else:
                idx = cr

            line = self.buffer[:idx].decode('utf-8', errors='replace').strip()
            self.buffer = self.buffer[idx + 1:]
            if line:
                lines.append(line)
        return lines

    def run(self):
        """Main event loop."""
        self.running = True
        self.send_boot_sequence()

        # Timers for frame generation
        last_frame_time = time.time()

        print(f"\n{'='*60}")
        print(f"  SGC Emulator running")
        print(f"  Slave port: {self.slave_name}")
        print(f"  Connect with: screen {self.slave_name} {self.cfg.baud}")
        print(f"  Or test harness: python sgc_test_harness.py --port {self.slave_name} ...")
        print(f"{'='*60}\n", flush=True)

        try:
            while self.running:
                now = time.time()

                # ── Handle incoming serial data ──────────────────
                data = self._read_available()
                if data:
                    self.buffer += data.encode('utf-8')
                    lines = self._process_buffer()
                    for line in lines:
                        if self.verbose:
                            print(f"  → {line}")
                        responses = self.sm.process_command(line)
                        for resp in responses:
                            self._write_json(resp)
                            if self.verbose:
                                print(f"  ← {json.dumps(resp)}")

                # ── Background updates ───────────────────────────
                if self.sm.state == "LOGGING" and not self.realtime:
                    # Burst mode: generate frames fast, batch-write with flow control
                    batch = []
                    while True:
                        responses = self.sm.update()
                        if not responses:
                            break
                        for resp in responses:
                            line = json.dumps(resp, separators=(',', ':')) + '\n'
                            batch.append(line)
                            if resp.get("ev") != "frame" and self.verbose:
                                print(f"  ← {json.dumps(resp)}")
                        # Write in chunks of 50 to avoid PTY buffer overflow
                        if len(batch) >= 50:
                            self._write_raw(''.join(batch))
                            batch.clear()
                            time.sleep(0.001)  # tiny yield to let client read
                    if batch:
                        self._write_raw(''.join(batch))
                else:
                    responses = self.sm.update()
                    for resp in responses:
                        self._write_json(resp)
                        if self.verbose:
                            if resp.get("ev") == "frame":
                                fr = resp.get("fr", 0)
                                if fr % 500 == 0 or fr == 1:
                                    print(f"  ← frame {fr}")
                            else:
                                print(f"  ← {json.dumps(resp)}")

                # ── Throttle (only in realtime mode, or non-LOGGING states) ──
                if self.realtime or self.sm.state != "LOGGING":
                    elapsed = now - last_frame_time
                    tick = 0.01 if self.sm.state == "LOGGING" else 0.002
                    if elapsed < tick:
                        time.sleep(tick - elapsed)
                last_frame_time = time.time()

        except KeyboardInterrupt:
            print("\n  Shutting down...")
        finally:
            self.running = False
            if self.master_fd is not None:
                try:
                    os.close(self.master_fd)
                except OSError:
                    pass
            if self._slave_fd is not None:
                try:
                    os.close(self._slave_fd)
                except OSError:
                    pass

        print("  Emulator stopped.")


# ═══════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description='SGC Device Emulator — Fake GS Run Over Serial',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-create PTY and print slave path:
  python sgc_emulator.py

  # Custom run parameters:
  python sgc_emulator.py --drop 180 --duration 50 --gates 35

  # Connect to existing serial port (for hardware loopback testing):
  python sgc_emulator.py --port /dev/ttyUSB0

  # Quiet mode (only JSON on serial, no console output):
  python sgc_emulator.py -q

In another terminal, connect with:
  screen /dev/pts/<N> 115200
  python sgc_test_harness.py --port /dev/pts/<N> test_full_run.py
        """)

    parser.add_argument('--port', '-p', default=None,
                        help='Existing serial port to use (instead of creating PTY)')
    parser.add_argument('--drop', type=float, default=150.0,
                        help='Vertical drop in meters (default: 150)')
    parser.add_argument('--duration', type=float, default=45.0,
                        help='Run duration in seconds (default: 45)')
    parser.add_argument('--gates', type=int, default=30,
                        help='Number of gates (default: 30)')
    parser.add_argument('--fps', type=int, default=100,
                        help='Sensor sample rate Hz (default: 100)')
    parser.add_argument('--cooldown', type=float, default=10.0,
                        help='Post-run flatline seconds (default: 10)')
    parser.add_argument('--start-alt', type=float, default=2200.0,
                        help='Starting altitude in meters (default: 2200)')
    parser.add_argument('--baud', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('--ring-size', type=int, default=500,
                        help='Ring buffer size (default: 500)')
    parser.add_argument('--arm-timeout', type=float, default=60.0,
                        help='ARMED→IDLE timeout in seconds (default: 60)')
    parser.add_argument('--postrun-cooldown', type=float, default=15.0,
                        help='POST_RUN→IDLE cooldown seconds (default: 15)')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='Suppress console output (only JSON on serial)')
    parser.add_argument('--realtime', action='store_true',
                        help='Run at real-time speed (default: generate as fast as possible)')

    args = parser.parse_args()

    cfg = EmulatorConfig(
        drop_m=args.drop,
        duration_s=args.duration,
        num_gates=args.gates,
        fps=args.fps,
        cooldown_s=args.cooldown,
        start_altitude_m=args.start_alt,
        baud=args.baud,
        ring_buffer_size=args.ring_size,
        arm_to_idle_timeout_s=args.arm_timeout,
        postrun_cooldown_s=args.postrun_cooldown,
    )

    emu = PTYSerialEmulator(cfg)
    emu.verbose = not args.quiet
    emu.realtime = args.realtime

    slave = emu.start(slave_name=args.port)
    print(f"PTY slave: {slave}", flush=True)

    # Handle signals gracefully
    signal.signal(signal.SIGINT, lambda s, f: setattr(emu, 'running', False))
    signal.signal(signal.SIGTERM, lambda s, f: setattr(emu, 'running', False))

    emu.run()


if __name__ == '__main__':
    main()
