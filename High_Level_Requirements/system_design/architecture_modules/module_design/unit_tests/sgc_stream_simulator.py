#!/usr/bin/env python3
"""
sgc_stream_simulator.py — Full test orchestrator: stream synthetic or replayed
sensor data to a real SGC device over serial.

ONE COMMAND does it all: wakeup → arm → stream → verify.

Usage:
  # Full auto: wake, arm, stream, verify
  python sgc_stream_simulator.py COM8

  # Deterministic run (same seed = same data every time)
  python sgc_stream_simulator.py COM8 --seed 42

  # Save generated frames for later replay
  python sgc_stream_simulator.py COM8 --save run_2026-07-08.ndjson

  # Replay pre-recorded frames (synthetic or real captured data)
  python sgc_stream_simulator.py COM8 --replay real_capture.ndjson

  # Custom run parameters
  python sgc_stream_simulator.py COM8 --duration 60 --gates 40 --seed 123

Binary frame format (little-endian, 38 bytes total, at 100 Hz):
  [0xAA 0x55] [uint32:frame_num] [float32:pressure_hPa] [float32:qw]
  [float32:qx] [float32:qy] [float32:qz]
  [float32:lax] [float32:lay] [float32:laz]
  End marker: [0xBB]

  2-byte sync word 0xAA 0x55 avoids false positives from float data
  (0xAA alone occurs ~1/256 per byte; 0xAA 0x55 ≈ 1/65536 per pair).
  Pressure is in hPa (hectopascals) — same unit as BHY2 SENSOR_ID_BARO
  on the Nicla Sense ME. At sea level: ~1013.25 hPa.

NDJSON replay format (one frame per line):
  {"fn":0,"p":797.25,"q":[0.985,0.0,0.174,0.0],"la":[0.0,0.0,-9810.0]}

Bandwidth: 38 bytes × 100 Hz = 3,800 B/s → 33% of 115,200 baud.

Author: ZioClaw — 2026-07-08
"""

SIM_VERSION = "2.19.0"

import argparse
import hashlib
import json
import math
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial required. Install: pip install pyserial")
    sys.exit(1)

# ═══════════════════════════════════════════════════════════════════
# Constants
# ═══════════════════════════════════════════════════════════════════

BAUD_RATE = 115200
FRAME_RATE_HZ = 100
FRAME_PERIOD_S = 1.0 / FRAME_RATE_HZ
SYNC_WORD = b'\xAA\x55'   # 2-byte sync: avoids false positives from float data
SYNC_LEN = 2
END_MARKER = 0xBB          # DEPRECATED (naked marker) — firmware V2.14+ ignores it
# V2.14: end-of-stream is now a FRAMED sentinel frame whose frame_num field is
# 0xFFFFFFFF, sent through the same 0xAA 0x55 sync + 36B payload discipline so
# payload data can never spuriously trigger end-of-stream.
STREAM_END_SENTINEL = 0xFFFFFFFF

# SGC start detector (from start_detector.h)
DROP_THRESHOLD_M = 2.0
PA_PER_M_HPA = 0.12     # ~12 Pa/m = 0.12 hPa/m (ISA sea-level gradient)
SEA_LEVEL_HPA = 1013.25 # standard sea-level pressure in hPa

# ═══════════════════════════════════════════════════════════════════
# Deterministic hash (replaces Python's non-deterministic hash())
# ═══════════════════════════════════════════════════════════════════


def _det_hash(seed: int, *args) -> int:
    """Deterministic hash — same inputs → same output across runs."""
    data = f"{seed}:{':'.join(str(a) for a in args)}".encode()
    return int.from_bytes(hashlib.md5(data).digest()[:4], 'little', signed=True)


# ═══════════════════════════════════════════════════════════════════
# GS Frame Data
# ═══════════════════════════════════════════════════════════════════


@dataclass
class GSFrame:
    """One frame of synthetic sensor data."""
    frame_num: int
    pressure_hpa: float     # barometric pressure (hPa) — matches BHY2 SENSOR_ID_BARO
    qw: float               # quaternion W
    qx: float               # quaternion X
    qy: float               # quaternion Y
    qz: float               # quaternion Z
    lax: float              # linear accel X (mm/s²)
    lay: float              # linear accel Y (mm/s²)
    laz: float              # linear accel Z (mm/s²)

    def to_dict(self) -> dict:
        return {
            "fn": self.frame_num,
            "p": round(self.pressure_hpa, 2),
            "q": [round(self.qw, 4), round(self.qx, 4),
                  round(self.qy, 4), round(self.qz, 4)],
            "la": [round(self.lax, 2), round(self.lay, 2), round(self.laz, 2)],
        }

    @classmethod
    def from_dict(cls, d: dict) -> "GSFrame":
        q = d["q"]
        la = d["la"]
        return cls(
            frame_num=d["fn"],
            pressure_hpa=d["p"],
            qw=q[0], qx=q[1], qy=q[2], qz=q[3],
            lax=la[0], lay=la[1], laz=la[2],
        )


# ═══════════════════════════════════════════════════════════════════
# Frame I/O — NDJSON save/load for replay
# ═══════════════════════════════════════════════════════════════════


def save_frames_ndjson(frames: List[GSFrame], path: str) -> None:
    """Save frames as newline-delimited JSON (NDJSON) for replay."""
    with open(path, 'w') as f:
        for frame in frames:
            f.write(json.dumps(frame.to_dict()) + '\n')
    size_kb = Path(path).stat().st_size / 1024
    print(f"Saved {len(frames)} frames to {path} ({size_kb:.1f} KB)")


def load_frames_ndjson(path: str) -> List[GSFrame]:
    """Load frames from NDJSON file (generated or real captured data)."""
    frames = []
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            d = json.loads(line)
            frames.append(GSFrame.from_dict(d))
    print(f"Loaded {len(frames)} frames from {path}")
    return frames


# ═══════════════════════════════════════════════════════════════════
# GS Run Profile Generator (deterministic with seed)
# ═══════════════════════════════════════════════════════════════════


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def smoothstep(t: float) -> float:
    """Smoothstep (Hermite) — smooth transitions with zero initial velocity."""
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def generate_gs_run(duration_s: float = 50.0,
                    start_altitude_m: float = 1800.0,
                    finish_altitude_m: float = 1400.0,
                    gate_count: int = 30,
                    gate_impact_strength: float = 8000.0,
                    seed: int = 0) -> List[GSFrame]:
    """
    Generate a realistic GS run as sensor frames at 100 Hz.

    Phase timeline:
      t = 0.0 – 2.0s  : Still at start gate (preparation)
      t = 2.0 – 2.8s  : 4 pole pushes (acceleration spikes)
      t = 2.8 – 3.0s  : Brief pause
      t = 3.0 – 45.0s : Descent with gate impacts
      t = 45.0 – 50.0s: Finish flat — deceleration, baro levels off

    Start detector fires when descent reaches 2.0m from P₀ (~18.9 Pa at 1800m).
    With smoothstep, this occurs at ~t=4.7s, ~1.7s after descent begins.

    Args:
        duration_s: Total run duration
        start_altitude_m: Starting altitude (m)
        finish_altitude_m: Finish altitude (m)
        gate_count: Number of gates
        gate_impact_strength: Peak lateral accel (mm/s²)
        seed: Deterministic seed for noise (0 = use frame index as seed)

    Returns:
        List of GSFrame, one per 10ms at 100Hz.
    """
    num_frames = int(duration_s * FRAME_RATE_HZ)
    frames: List[GSFrame] = []

    # Phase boundaries
    push_start = 2.0
    descent_start = 3.0          # baro starts rising
    finish_start = 45.0

    total_descent = start_altitude_m - finish_altitude_m
    descent_duration = finish_start - descent_start

    # Gate timing: evenly spaced with deterministic jitter
    first_gate_t = 3.5
    last_gate_t = 44.5
    gate_times = []
    for i in range(gate_count):
        t_gate = first_gate_t + (last_gate_t - first_gate_t) * i / max(gate_count - 1, 1)
        if 0 < i < gate_count - 1:
            jitter = _det_hash(seed, f"gate_{i}") % 400 - 200
            t_gate += jitter / 1000.0  # ±0.2s jitter
        gate_times.append(t_gate)

    # Pole push times
    push_times = [push_start + 0.0, push_start + 0.2,
                  push_start + 0.4, push_start + 0.6]

    for i in range(num_frames):
        t = i * FRAME_PERIOD_S

        # ── Altitude → pressure ──────────────────────────────
        if t < descent_start:
            altitude = start_altitude_m
        elif t < finish_start:
            frac = (t - descent_start) / descent_duration
            sf = smoothstep(min(frac, 1.0))
            altitude = start_altitude_m - total_descent * sf
        else:
            altitude = finish_altitude_m

        # Linear barometric approximation: ~0.12 hPa/m (~12 Pa/m)
        pressure_hpa = SEA_LEVEL_HPA - altitude * PA_PER_M_HPA

        # Deterministic sensor noise (±0.01 hPa)
        pressure_hpa += _det_hash(seed, f"baro", i) % 201 / 10000.0 - 0.01

        # ── Quaternion (orientation) ─────────────────────────
        if t < push_start:
            # Still at gate — slight forward lean (~20° pitch)
            qw, qx, qy, qz = 0.985, 0.0, 0.174, 0.0
        else:
            speed_factor = 0.0
            if descent_start <= t < finish_start:
                speed_factor = min(1.0, (t - descent_start) / 3.0)

            pitch = 0.174 - 0.05 * speed_factor
            roll = math.sin(t * 2.5) * 0.05 * speed_factor

            cp2, sp2 = math.cos(pitch / 2), math.sin(pitch / 2)
            cr2, sr2 = math.cos(roll / 2), math.sin(roll / 2)

            qw = cp2 * cr2
            qx = sr2 * cp2
            qy = sp2 * cr2
            qz = -sp2 * sr2

            mag = math.sqrt(qw**2 + qx**2 + qy**2 + qz**2)
            if mag > 0:
                qw, qx, qy, qz = qw / mag, qx / mag, qy / mag, qz / mag

        # ── Linear acceleration ──────────────────────────────
        lax, lay, laz = 0.0, 0.0, -9810.0  # gravity baseline (mm/s²)

        # Pole pushes
        for pt in push_times:
            dt = t - pt
            if 0.0 <= dt < 0.08:
                push = math.exp(-((dt - 0.04) ** 2) / 0.0003)
                lax += 15000.0 * push
                laz += 5000.0 * push

        # Descent dynamics
        if descent_start <= t < finish_start:
            frac = (t - descent_start) / descent_duration
            carve_phase = t * 1.8
            turn_factor = 0.5 + 0.5 * math.cos(carve_phase)

            lax += 3500.0 * turn_factor
            lay += 5000.0 * math.sin(carve_phase) * min(1.0, frac * 3)
            laz += 1500.0 * abs(math.sin(carve_phase))

        # Gate impacts
        for gi, gt in enumerate(gate_times):
            dt = t - gt
            if 0.0 <= dt < 0.06:
                impact = math.exp(-((dt - 0.02) ** 2) / 0.00008)
                direction = 1.0 if gi % 2 == 0 else -1.0
                lay += direction * gate_impact_strength * impact
                lax -= gate_impact_strength * 0.3 * impact
                laz += gate_impact_strength * 0.2 * impact

        # Finish flatline deceleration
        if t >= finish_start:
            decel_frac = min(1.0, (t - finish_start) / 3.0)
            sf = smoothstep(decel_frac)
            lax *= (1.0 - sf * 0.9)
            laz = lerp(laz, -9810.0, sf * 0.3)

        # Deterministic accel noise (±100 mm/s²)
        lax += _det_hash(seed, f"accel_x", i) % 201 - 100
        lay += _det_hash(seed, f"accel_y", i) % 201 - 100
        laz += _det_hash(seed, f"accel_z", i) % 201 - 100

        frames.append(GSFrame(
            frame_num=i,
            pressure_hpa=pressure_hpa,
            qw=qw, qx=qx, qy=qy, qz=qz,
            lax=lax, lay=lay, laz=laz,
        ))

    return frames


# ═══════════════════════════════════════════════════════════════════
# Binary frame packing
# ═══════════════════════════════════════════════════════════════════


def pack_frame(frame: GSFrame) -> bytes:
    """Pack a GSFrame into 38-byte binary wire format (little-endian)."""
    payload = struct.pack(
        '<Iffffffff',   # uint32 + 8×float32 = 4 + 32 = 36 bytes
        frame.frame_num,
        frame.pressure_hpa,
        frame.qw, frame.qx, frame.qy, frame.qz,
        frame.lax, frame.lay, frame.laz,
    )
    return SYNC_WORD + payload  # 2 + 36 = 38 bytes


# ═══════════════════════════════════════════════════════════════════
# SGC Device Controller
# ═══════════════════════════════════════════════════════════════════


class SGCDevice:
    """Manages serial connection and command protocol with SGC device."""

    def __init__(self, port: str):
        self.port = port
        self.ser: Optional[serial.Serial] = None
        self.init_pressure: float = 0.0  # stored for diagnostics
        self.fw_version: str = "?"       # firmware version, filled by query_version()
        self.reset_event: Optional[dict] = None  # boot line seen mid-stream = device reset
        self.breadcrumbs: List[dict] = []         # {"ev":"bc",...} seen mid-stream
        self._rx_partial: bytes = b""             # partial line carried between reads

    def connect(self) -> None:
        print(f"Connecting to {self.port} at {BAUD_RATE} baud...")
        self.ser = serial.Serial(self.port, BAUD_RATE, timeout=1.0)
        time.sleep(0.5)  # let device settle after DTR

    def disconnect(self) -> None:
        if self.ser and self.ser.is_open:
            self.ser.close()
        print("Disconnected.")

    def send_cmd(self, cmd: str, wait_ms: int = 200) -> None:
        """Send a text command and wait briefly."""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + '\n').encode('ascii'))
        time.sleep(wait_ms / 1000.0)

    def read_json_lines(self, timeout_s: float = 2.0) -> List[dict]:
        """Read all available JSON-lines from serial."""
        lines = []
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.ser.in_waiting:
                try:
                    raw = self.ser.readline()
                    line = raw.decode('ascii', errors='replace').strip()
                    if line:
                        lines.append(json.loads(line))
                except (json.JSONDecodeError, UnicodeDecodeError):
                    continue
            else:
                time.sleep(0.01)
        return lines

    def drain_responses(self, timeout_s: float = 2.0) -> List[dict]:
        return self.read_json_lines(timeout_s)

    # ── Orchestration steps ──────────────────────────────────

    def wakeup(self) -> bool:
        """Wake device from SLEEP and bring to IDLE."""
        print("\n── Wakeup ──")
        # Send 'i' to force IDLE (works from any state)
        self.send_cmd('i', wait_ms=500)
        resp = self.drain_responses(1.0)
        for r in resp:
            ev = r.get("ev", "")
            if ev == "st":
                print(f"  State: {r.get('from', '?')} → {r.get('to', '?')}")
            elif ev == "status":
                print(f"  Status: state={r.get('st', '?')} batt={r.get('bat', '?')}% "
                      f"runs={r.get('runs', '?')} flash={r.get('wh', '?')}")

        # Verify in IDLE
        self.send_cmd('?', wait_ms=300)
        resp2 = self.drain_responses(1.0)
        for r in resp2:
            if r.get("ev") == "status":
                st = r.get("st", "")
                if st == "IDLE":
                    print(f"  Device in IDLE ✓ (batt={r.get('bat')}%, "
                          f"runs={r.get('runs')} flash_used={r.get('wh')})")
                    return True
                else:
                    print(f"  ⚠ Device in {st}, retrying...")
                    self.send_cmd('i', wait_ms=300)
        return False

    def query_version(self) -> str:
        """Ask the device for its firmware version (no reset). Returns the
        version string and caches it on self.fw_version."""
        print("\n── Firmware Version ──")
        self.send_cmd('V', wait_ms=300)
        resp = self.drain_responses(1.0)
        for r in resp:
            if r.get("ev") == "version":
                self.fw_version = str(r.get("ver", "?"))
                print(f"  Firmware: v{self.fw_version}")
                return self.fw_version
        print("  ⚠ No version response (device may be older than the 'V' command)")
        return self.fw_version

    def enter_test_mode(self) -> None:
        print("\n── Enter Test Mode ──")
        # 'T' toggles — send once, check state, toggle again if we landed OFF
        self.send_cmd('T', wait_ms=300)
        resp = self.drain_responses(1.0)
        tm = None
        for r in resp:
            if r.get("ev") == "cmd" and r.get("cmd") == "T":
                tm = r.get('tm', 0)
        if tm == 0:
            # Toggled OFF — send again to get ON
            self.send_cmd('T', wait_ms=300)
            resp2 = self.drain_responses(1.0)
            for r in resp2:
                if r.get("ev") == "cmd" and r.get("cmd") == "T":
                    tm = r.get('tm', 0)
            print(f"  Test mode: {tm} (toggled twice — was OFF)")
        else:
            print(f"  Test mode: {tm}")

    def enter_stream_mode(self) -> None:
        print("\n── Enter Stream Mode ──")
        self.send_cmd('S', wait_ms=300)
        resp = self.drain_responses(1.0)
        for r in resp:
            if r.get("ev") == "cmd" and r.get("cmd") == "S":
                print(f"  Stream mode: {r.get('strm')}")

    def arm(self) -> None:
        """Arm via serial command."""
        print("\n── Arm ──")
        self.send_cmd('a', wait_ms=300)
        resp = self.drain_responses(1.0)
        for r in resp:
            ev = r.get("ev", "")
            if ev == "st":
                print(f"  State: {r.get('from')} → {r.get('to')}")
            elif ev == "arm_refused":
                print(f"  ⚠ Arm refused: {r.get('reason')} (mag={r.get('mag')})")

    def set_pressure(self, pa: float) -> None:
        """Set initial test pressure to match first frame."""
        self.send_cmd(f"B {pa:.1f}", wait_ms=200)

    @staticmethod
    def decode_reset_reason(rr: int) -> str:
        """Decode nRF52 RESETREAS bitfield into a human string."""
        if rr == 0:
            return "power-on / brown-out (RESETREAS=0)"
        bits = []
        if rr & (1 << 0):  bits.append("RESETPIN")
        if rr & (1 << 1):  bits.append("DOG(watchdog)")
        if rr & (1 << 2):  bits.append("SREQ(soft-reset)")
        if rr & (1 << 3):  bits.append("LOCKUP(hardfault/CPU-lockup)")
        if rr & (1 << 16): bits.append("OFF")
        if rr & (1 << 17): bits.append("LPCOMP")
        if rr & (1 << 18): bits.append("DIF(debug)")
        if rr & (1 << 19): bits.append("NFC")
        if rr & (1 << 20): bits.append("VBUS")
        return " | ".join(bits) if bits else f"unknown (0x{rr:08X})"

    def poll_reset_during_stream(self) -> bool:
        """Non-blocking read of any pending serial while streaming.
        Detects a mid-stream device reset (a 'boot' JSON line) and captures
        the RESETREAS diagnostic + any breadcrumbs. Returns True if a reset
        was detected (caller should abort the stream)."""
        if not self.ser or self.ser.in_waiting == 0:
            return False
        try:
            self._rx_partial += self.ser.read(self.ser.in_waiting)
        except Exception:
            return False
        # Split complete lines; keep the trailing partial for next poll.
        *lines, self._rx_partial = self._rx_partial.split(b"\n")
        for raw in lines:
            line = raw.decode("ascii", errors="replace").strip()
            if not line.startswith("{"):
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            ev = obj.get("ev", "")
            if ev == "bc":
                self.breadcrumbs.append(obj)
                print(f"   ⤷ breadcrumb: {obj.get('at')}")
            elif ev == "boot":
                self.reset_event = obj
                rr = int(obj.get("rr", 0))
                print("\n" + "!" * 52)
                print("   💥 DEVICE RESET DETECTED MID-STREAM")
                print(f"      boot ver = v{obj.get('ver')}")
                print(f"      RESETREAS = {rr} (0x{rr:08X}) → {self.decode_reset_reason(rr)}")
                if self.breadcrumbs:
                    last = self.breadcrumbs[-1].get("at")
                    print(f"      last breadcrumb before reset = {last}")
                else:
                    print("      no breadcrumb captured before reset")
                print("!" * 52)
                return True
        return False

    def stream_frames(self, frames: List[GSFrame], pre_fill: int = 30) -> dict:
        """
        Stream frames at 100Hz with pre-fill buffer.

        Strategy: send pre_fill frames immediately to fill the device's
        serial buffer, then maintain exactly 100Hz for the rest.
        This prevents underruns if Python scheduling jitters.

        Returns:
            Dictionary with stream statistics.
        """
        FRAME_BYTES = 38  # 2-byte sync + 36 payload
        total = len(frames)

        print(f"\n── Stream {total} frames ({total / FRAME_RATE_HZ:.1f}s) ──")
        print(f"   BW: {FRAME_BYTES * FRAME_RATE_HZ} B/s "
              f"({FRAME_BYTES * FRAME_RATE_HZ / BAUD_RATE * 100:.1f}% of {BAUD_RATE} baud)")
        print(f"   Phase: prep 0-2s | pushes 2-2.8s | descent 3-45s | finish 45s+")
        print(f"   Start P₀: {frames[0].pressure_hpa:.2f} hPa")
        ds = int(3.1 * FRAME_RATE_HZ)
        dp = frames[min(ds, total - 1)].pressure_hpa - frames[0].pressure_hpa
        print(f"   Baro ramp at frame {int(3.0 * FRAME_RATE_HZ)} "
              f"(ΔP={dp:.2f} hPa, start detection needs "
              f"~{DROP_THRESHOLD_M * PA_PER_M_HPA:.2f} hPa)")

        # Pre-fill
        pre_data = b''.join(pack_frame(f) for f in frames[:pre_fill])
        self.ser.write(pre_data)
        self.ser.flush()

        # Stream at 100Hz
        t0 = time.perf_counter()
        sent = pre_fill
        last_report = 0

        for i in range(pre_fill, total):
            frame = frames[i]
            data = pack_frame(frame)
            target_time = t0 + (i - pre_fill) * FRAME_PERIOD_S
            sleep_t = target_time - time.perf_counter()

            if sleep_t > 0:
                time.sleep(sleep_t)

            self.ser.write(data)
            sent += 1

            # Poll for a mid-stream device reset frequently (cheap in_waiting
            # check). The device prints its boot line the instant it reboots;
            # catch it before the OS serial buffer overflows and drops it.
            if (sent % 10) == 0 and self.poll_reset_during_stream():
                print(f"   ⚠ Aborting stream at frame {sent}/{total} "
                      f"(device reset).")
                break

            # Progress every 100 frames
            if sent - last_report >= 100:
                last_report = sent
                elapsed = time.perf_counter() - t0
                rate = sent / elapsed if elapsed > 0 else 0
                delta_p = frames[min(i, total - 1)].pressure_hpa - frames[0].pressure_hpa
                print(f"   {sent}/{total} ({sent/total*100:.0f}%) "
                      f"@ {rate:.0f} fps, ΔP={delta_p:.2f} hPa")

        # End-of-stream: framed sentinel (V2.14). A full 38-byte frame with
        # frame_num=0xFFFFFFFF so it rides the same sync+payload discipline and
        # can't be faked by float payload data. (The old naked 0xBB byte had no
        # framing and was misread from payload data, exiting stream mode early.)
        sentinel_payload = struct.pack(
            '<Iffffffff',
            STREAM_END_SENTINEL,
            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        )
        # V2.17 harness: the sentinel is the LAST frame on the wire — it arrives
        # exactly when the device RX buffer is most congested, so it can be
        # dropped (v2.15/v2.16 runs showed NO stream_end at all → device stuck
        # in stream mode → '?' eaten by the frame parser). Send it, wait for the
        # stream_end ack, resend up to 3 times. Any JSON captured while waiting
        # is stashed in self.early_events for verify_run() to merge.
        self.early_events = getattr(self, "early_events", [])
        got_end = False
        for attempt in range(3):
            self.ser.write(SYNC_WORD + sentinel_payload)
            self.ser.flush()
            for r in self.read_json_lines(2.0):
                self.early_events.append(r)
                if r.get("ev") == "stream_end":
                    got_end = True
            if got_end:
                break
            print(f"   ⚠ no stream_end ack — resending sentinel "
                  f"(attempt {attempt + 2}/3)")
        if not got_end:
            print("   ⚠ stream_end NEVER acked — device may still be in stream mode")

        elapsed = time.perf_counter() - t0
        stats = {
            "frames_sent": sent,
            "elapsed_s": elapsed,
            "actual_fps": sent / elapsed if elapsed > 0 else 0,
            "target_fps": FRAME_RATE_HZ,
        }
        print(f"\n   Done: {sent} frames in {elapsed:.1f}s ({stats['actual_fps']:.0f} fps)")
        return stats

    def verify_run(self) -> Optional[dict]:
        """Check device state and verify a run was saved."""
        print("\n── Verify ──")
        time.sleep(2.0)  # wait for POST_RUN + flush

        responses = list(getattr(self, "early_events", [])) + self.drain_responses(5.0)
        self.early_events = []

        # V2.17 harness: close_run() does erase-heavy flash work and POST_RUN
        # fires ~5s after stream end (flatline detection), so the old fixed 5s
        # window cut off right after the START trace — the cbc trail / done /
        # run_saved were swallowed by later drains. If close_run started but
        # hasn't finished, KEEP listening (up to 30s) until done/run_saved.
        def _has(ev_name, key=None, val=None):
            return any(r.get("ev") == ev_name and (key is None or r.get(key) == val)
                       for r in responses)
        if _has("close_trace", "step", "start"):
            extend_deadline = time.time() + 30.0
            extended = False
            while (not _has("close_trace", "step", "done")
                   and not _has("run_saved")
                   and time.time() < extend_deadline):
                if not extended:
                    print("   … close_run() in progress — extending listen window "
                          "(up to 30s)")
                    extended = True
                responses.extend(self.drain_responses(2.0))
        state_events = []
        run_saved = []
        stream_end = []
        sd_events = []
        timeout_events = []
        close_trace = []
        close_bc = []  # {"ev":"cbc",...} close_run() step breadcrumbs (V2.13)
        all_events = []

        for r in responses:
            ev = r.get("ev", "")
            all_events.append(r)
            if ev == "st":
                state_events.append(r)
            elif ev == "run_saved":
                run_saved.append(r)
            elif ev == "stream_end":
                stream_end.append(r)
            elif ev == "timeout":
                timeout_events.append(r)
            elif ev == "close_trace":
                close_trace.append(r)
            elif ev == "cbc":
                close_bc.append(r)
            elif ev in ("sd", "start"):
                sd_events.append(r)
            elif ev == "bc":
                self.breadcrumbs.append(r)
            elif ev == "boot" and self.reset_event is None:
                # A boot line here (after streaming) also means the device reset.
                self.reset_event = r

        # Report a mid/post-stream reset up front — this is the key diagnostic.
        if self.reset_event:
            rr = int(self.reset_event.get("rr", 0))
            print("   💥 DEVICE RESET during run:")
            print(f"      boot ver = v{self.reset_event.get('ver')}")
            print(f"      RESETREAS = {rr} (0x{rr:08X}) → {self.decode_reset_reason(rr)}")
            if self.breadcrumbs:
                print(f"      breadcrumbs seen: "
                      f"{', '.join(b.get('at','?') for b in self.breadcrumbs)}")

        # Report
        print("   State transitions:")
        for se in state_events:
            print(f"     {se.get('from'):>10} → {se.get('to')}")

        if stream_end:
            se = stream_end[-1]
            print(f"   Stream end: {se.get('fn')} frames, {se.get('lost')} lost")

        if timeout_events:
            print("   Timeout events:")
            for te in timeout_events:
                elapsed = te.get('elapsed', '?')
                print(f"     {te.get('from'):>10} → {te.get('to')} (elapsed={elapsed}ms)")

        if close_bc:
            print("   ── close_run() step breadcrumbs (cbc) ──")
            for cb in close_bc:
                extra = ""
                if "rc" in cb:
                    extra = f" rc={cb.get('rc')}"
                elif "n" in cb:
                    extra = f" n={cb.get('n')}"
                print(f"     ⤷ {cb.get('at')}{extra}")
            # Only show stall warning if close_run did NOT finish.
            close_finished = any(ct.get("step") == "done" for ct in close_trace)
            run_saved_ok = any(rs.get("ok") for rs in run_saved)
            if not close_finished and not run_saved_ok:
                print(f"     → LAST step reached: '{close_bc[-1].get('at')}' "
                      f"(the step AFTER this is where it stalls/fails)")
            else:
                print(f"     → close_run() completed all steps \u2713")

        if close_trace:
            print("   ── close_run() trace ──")
            for ct in close_trace:
                step = ct.get('step', '?')
                if step == 'start':
                    print(f"     START: run_start={ct.get('run_start')} sz={ct.get('sz')} "
                          f"fr={ct.get('fr')} wh_before={ct.get('wh_before')}")
                elif step == 'hdr_write':
                    print(f"     {'✓' if ct.get('ok') else '✗'} Header write: ok={ct.get('ok')}")
                elif step == 'trailer_erase':
                    print(f"     {'✓' if ct.get('ok') else '✗'} Trailer erase: ok={ct.get('ok')} "
                          f"addr={ct.get('trailer_addr')}")
                elif step == 'write_head':
                    print(f"     Write head: {ct.get('wh_old')} → {ct.get('wh_new')}")
                elif step == 'done':
                    print(f"     DONE: entry_count={ct.get('entry_count')} "
                          f"run_count={ct.get('run_count')} final_wh={ct.get('final_wh')}")

        if sd_events:
            for sd in sd_events:
                if sd.get("ev") == "start":
                    print(f"   Start detected: mode={sd.get('mode')}, drop={sd.get('m')}m")
                elif sd.get("ev") == "sd":
                    pass  # diagnostic events, skip

        if run_saved:
            rs = run_saved[0]
            ok = rs.get('ok', 0)
            rid = rs.get('id', 0)
            status = "✓ SUCCESS" if ok else "✗ FAILED (returned 0xFFFF)"
            print(f"\n   {status}: id={rid} frames={rs.get('fr')} "
                  f"compressed={rs.get('sz')} bytes")
            print(f"     write_head={rs.get('wh')} entry_count={rs.get('ec')} "
                  f"total_count={rs.get('tc')}")
            if rs.get('fr', 0) > 0:
                ratio = rs['sz'] / rs['fr'] if rs['fr'] > 0 else 0
                print(f"     Compression: {rs['sz']} bytes / {rs['fr']} frames = "
                      f"{ratio:.1f} bytes/frame")

            # ── Verify run count persisted (mirrors BLE ABC8 read) ──
            print("\n   ── Run count check (BLE-equivalent) ──")
            self.send_cmd('?', wait_ms=300)
            status = self.drain_responses(1.0)
            count_ok = False
            for r in status:
                if r.get("ev") == "status":
                    runs = r.get("runs", 0)
                    total_runs = r.get("total_runs", 0)
                    oldest_age = r.get("oldest_age", 0)
                    write_head = r.get("wh", 0)
                    read_head = r.get("rh", 0)
                    print(f"     runs={runs} total_runs={total_runs} oldest_age={oldest_age}")
                    print(f"     write_head={write_head} read_head={read_head}")
                    if runs > 0 and total_runs > 0:
                        print(f"     ✓ Run persisted in RAM index")
                        count_ok = True
                    else:
                        print(f"     ✗ Run count is ZERO — flash persistence likely failed!")
                        print(f"       (runs={runs}, total_runs={total_runs})")
            if not count_ok:
                print("     ⚠ WARNING: BLE ABC8 would report 0 runs to the phone app.")
            return rs

        # Diagnostics if no run saved
        print("\n   ⚠ No run saved. Diagnostics:")
        self.send_cmd('?', wait_ms=300)
        status = self.drain_responses(1.0)
        got_status = False
        for r in status:
            if r.get("ev") == "status":
                got_status = True
                print(f"     Device: st={r.get('st')} batt={r.get('bat')}% "
                      f"runs={r.get('runs')} ring={r.get('r')}/{r.get('rm')}")
            else:
                # V2.17: do NOT swallow late events — close_run() output that
                # arrives after the verify window lands in THIS drain.
                print(f"     late event: {json.dumps(r)}")

        # Determine what actually happened, in order of evidence.
        reached_logging = any(
            se.get("to") == "LOGGING" or se.get("from") == "LOGGING"
            for se in state_events
        )
        close_started = len(close_trace) > 0
        close_finished = any(ct.get("step") == "done" for ct in close_trace)

        if close_started and not close_finished:
            # close_run() began (start trace) but never emitted 'done'.
            print("     ✗ close_run() STARTED but never finished — it is "
                  "hanging/failing mid-close.")
            print("       Look at the last {\"ev\":\"cbc\",\"at\":...} breadcrumb "
                  "above to see which step stalled.")
            if not got_status:
                print("       (No '?' status response — device is likely HUNG "
                      "inside close_run(), not just slow.)")
        elif reached_logging:
            print("     ✗ Reached LOGGING but no run_saved — close_run() did not "
                  "persist the run.")
        elif not any(e.get("ev") == "start" for e in sd_events):
            print("     ✗ Start detection never fired.")
            threshold_pa = DROP_THRESHOLD_M * PA_PER_M_HPA * self.init_pressure / SEA_LEVEL_HPA
            print(f"       Baro ramp ΔP must exceed {threshold_pa:.0f} Pa "
                  f"(2.0m drop at P₀={self.init_pressure:.0f} Pa)")

        return None


# ═══════════════════════════════════════════════════════════════════
# Main test scenario
# ═══════════════════════════════════════════════════════════════════


def run_full_test(port: str,
                  duration_s: float = 50.0,
                  gate_count: int = 30,
                  gate_strength: float = 8000.0,
                  seed: int = 0,
                  pre_fill: int = 30,
                  save_path: Optional[str] = None,
                  replay_path: Optional[str] = None,
                  factory_reset: bool = False,
                  interactive: bool = True) -> int:
    """
    Full test orchestration: wakeup → [factory reset] → arm → stream → verify.

    factory_reset=True (CLI: -R/--reset) erases the flash filesystem before the
    run for a clean state. Off by default so a crash doesn't wipe the evidence
    (existing runs) and so the test starts faster.

    Returns 0 on success, 1 on failure.
    """
    device = SGCDevice(port)

    try:
        device.connect()

        # ── Step 1: Wakeup ─────────────────────────────────
        if not device.wakeup():
            print("⚠ Could not confirm IDLE state, proceeding anyway...")

        # ── Step 2: Factory reset filesystem (opt-in via -R) ──
        if factory_reset:
            print("\n── Factory Reset (-R) ──")
            device.send_cmd('R', wait_ms=500)
            time.sleep(4.0)  # wait for reboot + COM port re-enumeration
            device.ser.close()
            time.sleep(1.0)
            device.connect()  # reconnect after reboot
            if not device.wakeup():
                print("⚠ Could not confirm IDLE after reset, proceeding...")
        else:
            print("\n── Factory Reset: SKIPPED (pass -R to erase flash first) ──")

        # ── Query firmware version (no reset) — reported in results ──
        device.query_version()

        # ── Step 3: Test mode ─────────────────────────────
        device.enter_test_mode()

        # ── Step 4: Get frames (generate or replay) ────────
        if replay_path:
            print(f"\n── Replay: {replay_path} ──")
            frames = load_frames_ndjson(replay_path)
            if not frames:
                print("ERROR: No frames loaded from replay file.")
                return 1
        else:
            print(f"\n── Generate GS run (seed={seed}) ──")
            frames = generate_gs_run(
                duration_s=duration_s,
                gate_count=gate_count,
                gate_impact_strength=gate_strength,
                seed=seed,
            )
            print(f"   Generated {len(frames)} frames "
                  f"(P₀={frames[0].pressure_hpa:.2f} hPa "
                  f"~{(SEA_LEVEL_HPA - frames[0].pressure_hpa) * (1/PA_PER_M_HPA):.0f}m altitude)")

            if save_path:
                save_frames_ndjson(frames, save_path)

        # ── Step 4: Set initial pressure + arm ─────────────
        init_pa = frames[0].pressure_hpa
        device.init_pressure = init_pa  # for accurate diagnostics
        device.set_pressure(init_pa)
        device.arm()

        # ── Step 5: Enter stream mode + Stream ────────────
        device.enter_stream_mode()
        if interactive:
            input("\nPress ENTER to start streaming...")
        else:
            print("\nStarting stream (non-interactive)...")
            time.sleep(0.5)  # let device settle in stream mode
        device.ser.reset_input_buffer()
        device.ser.reset_output_buffer()

        device.stream_frames(frames, pre_fill=pre_fill)

        # ── Step 6: Verify ─────────────────────────────────
        result = device.verify_run()

        if result:
            print("\n" + "=" * 50)
            print("✓ TEST PASSED")
            print(f"  Firmware version: v{device.fw_version}")
            print("=" * 50)
            return 0
        else:
            print("\n" + "=" * 50)
            if device.reset_event:
                rr = int(device.reset_event.get("rr", 0))
                print("✗ TEST FAILED — DEVICE RESET")
                print(f"  RESETREAS = {rr} (0x{rr:08X}) → "
                      f"{device.decode_reset_reason(rr)}")
                if device.breadcrumbs:
                    print(f"  Last breadcrumb: {device.breadcrumbs[-1].get('at')}")
            else:
                print("⚠ TEST: no run saved — check diagnostics above")
            print(f"  Firmware version: v{device.fw_version}")
            print("=" * 50)
            return 1

    except serial.SerialException as e:
        print(f"\nERROR: Serial failure: {e}")
        return 1
    except KeyboardInterrupt:
        print("\n\nInterrupted by user.")
        return 1
    finally:
        device.disconnect()


def list_ports() -> None:
    """List available serial ports."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
        return
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device} — {p.description}")


# ═══════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════


def main():
    parser = argparse.ArgumentParser(
        description="SGC Stream Simulator — wakeup → arm → stream → verify",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s COM8                      One-command full auto test
  %(prog)s COM8 --seed 42            Deterministic run (same every time)
  %(prog)s COM8 --save run_001.ndjson  Save frames for later replay
  %(prog)s COM8 --replay real_cap.ndjson  Replay captured/recorded data
  %(prog)s COM8 --duration 60 --gates 40  Custom run
  %(prog)s --list                    List available ports
        """,
    )
    parser.add_argument("port", nargs="?", help="Serial port (e.g., COM8, /dev/ttyACM0)")
    parser.add_argument("--list", action="store_true", help="List available serial ports")
    parser.add_argument("--duration", type=float, default=50.0,
                        help="Run duration in seconds (default: 50)")
    parser.add_argument("--gates", type=int, default=30,
                        help="Number of gates (default: 30)")
    parser.add_argument("--gate-strength", type=float, default=8000.0,
                        help="Gate impact strength mm/s² (default: 8000)")
    parser.add_argument("--seed", type=int, default=0,
                        help="Deterministic seed for noise (0=use frame index, "
                             "same seed=same data)")
    parser.add_argument("--pre-fill", type=int, default=30,
                        help="Frames to pre-fill in device buffer (default: 30)")
    parser.add_argument("--save", type=str, default=None,
                        help="Save generated frames to NDJSON file for replay")
    parser.add_argument("--replay", type=str, default=None,
                        help="Replay frames from NDJSON file (instead of generating)")
    parser.add_argument("-R", "--reset", action="store_true",
                        help="Factory-reset (erase flash) before the run "
                             "(default: OFF, preserves stored runs)")
    parser.add_argument("--no-interactive", action="store_true",
                        help="Run without user prompts (for CI / test suites)")

    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    if not args.port:
        parser.print_help()
        print("\nERROR: port is required (or use --list)")
        sys.exit(1)

    exit_code = run_full_test(
        port=args.port,
        duration_s=args.duration,
        gate_count=args.gates,
        gate_strength=args.gate_strength,
        seed=args.seed,
        pre_fill=args.pre_fill,
        save_path=args.save,
        replay_path=args.replay,
        factory_reset=args.reset,
        interactive=not args.no_interactive,
    )
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
