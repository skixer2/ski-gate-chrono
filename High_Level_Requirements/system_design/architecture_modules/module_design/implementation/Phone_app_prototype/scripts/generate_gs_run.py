#!/usr/bin/env python3
"""
Synthetic GS Run Generator for SGC Phone App Testing.

Generates realistic sensor-frame data for a Giant Slalom run:
  150 m vertical drop, 45 s duration, ~25 m gate spacing, 30 gates.
  100 Hz sample rate → 4500 frames.

Outputs JSON with DecompressResult-compatible structure.
"""
import json
import math
import sys
from dataclasses import dataclass, asdict
from typing import List
from datetime import datetime, timezone

# ── Parameters ──────────────────────────────────────────────────────
DROP_M = 150.0           # vertical drop (m)
DURATION_S = 45.0        # total run time (s)
NUM_GATES = 30           # gate count
GATE_SPACING_M = 25.0    # average distance between gates (horizontal)
FPS = 100                # sensor sampling rate
TURN_G_FORCE = 2.5       # peak centripetal acceleration (G)
P0_SEA_LEVEL_PA = 101325.0  # pressure at sea level (Pa)

# ── Derived ─────────────────────────────────────────────────────────
NUM_FRAMES = int(DURATION_S * FPS)
DT_S = 1.0 / FPS
START_ALTITUDE_M = 2200.0  # typical GS start altitude
END_ALTITUDE_M = START_ALTITUDE_M - DROP_M

# Gate positions (evenly spaced in time)
gate_frames = [int((i + 1) * NUM_FRAMES / (NUM_GATES + 1)) for i in range(NUM_GATES)]

# Average vertical speed
avg_vspeed = DROP_M / DURATION_S  # m/s


def altitude_to_pa(alt_m: float) -> float:
    """ISA barometric formula: altitude (m) → pressure (Pa)."""
    return P0_SEA_LEVEL_PA * (1 - 2.25577e-5 * alt_m) ** 5.25588


def quat_from_euler(roll: float, pitch: float, yaw: float):
    """Euler angles (rad) → quaternion [w, x, y, z]."""
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


def generate_frames() -> list:
    """Generate 4500 frames of synthetic GS run data."""
    frames = []
    last_gate_frame = 0

    for i in range(NUM_FRAMES):
        t = i * DT_S  # seconds from start
        ms_from_start = i * 10  # 100 Hz → 10 ms per frame

        # ── Altitude: smooth descent with micro-variation ─────
        fraction = i / NUM_FRAMES
        # Add slight variation to make it realistic (terrain not perfectly linear)
        terrain_variation = 0.5 * math.sin(fraction * 12 * math.pi)
        altitude = START_ALTITUDE_M - fraction * DROP_M + terrain_variation

        # ── Pressure from altitude ───────────────────────────
        pa = altitude_to_pa(altitude)

        # ── Quaternion: simulated slalom turns ──────────────
        # Determine which gate we're near
        gate_idx = None
        for g, gf in enumerate(gate_frames):
            # Turn zone: ±15 frames around gate (300ms window)
            if abs(i - gf) <= 15:
                gate_idx = g
                break

        if gate_idx is not None and gate_idx % 2 == 0:
            # Right turn: yaw rotates positive, roll tips left
            progress = (i - gate_frames[gate_idx]) / 20.0  # -0.75 to +0.75
            yaw = progress * 0.8  # ~45° total rotation
            roll = 0.3 * math.exp(-progress * progress * 3)  # brief tip at apex
            pitch = -0.05  # slight forward lean
        elif gate_idx is not None and gate_idx % 2 == 1:
            # Left turn: yaw rotates negative, roll tips right
            progress = (i - gate_frames[gate_idx]) / 20.0
            yaw = -progress * 0.8
            roll = -0.3 * math.exp(-progress * progress * 3)
            pitch = -0.05
        else:
            # Between gates: gradual return to neutral
            yaw = 0.0
            roll = 0.0
            pitch = -0.05

        qw, qx, qy, qz = quat_from_euler(roll, pitch, yaw)

        # ── Linear acceleration ────────────────────────────
        # Gravity vector rotated by quaternion
        g = 9.81
        grav_x = 2 * (qx * qz - qw * qy) * g
        grav_y = 2 * (qw * qx + qy * qz) * g
        grav_z = (qw * qw - qx * qx - qy * qy + qz * qz) * g

        # Add centripetal acceleration during turns
        centripetal_x = 0.0
        centripetal_y = 0.0
        if gate_idx is not None:
            progress = (i - gate_frames[gate_idx]) / 20.0
            turn_intensity = math.exp(-progress * progress * 2)
            if gate_idx % 2 == 0:  # right turn
                centripetal_x = TURN_G_FORCE * g * turn_intensity
            else:  # left turn
                centripetal_x = -TURN_G_FORCE * g * turn_intensity
            centripetal_y = 0.3 * TURN_G_FORCE * g * turn_intensity

        # Add vibration noise
        vibration = 2.0  # m/s² RMS
        noise_x = vibration * (2 * ((i * 0.7) % 1 - 0.5))
        noise_y = vibration * (2 * ((i * 0.3 + 0.5) % 1 - 0.5))
        noise_z = vibration * 0.5 * (2 * ((i * 0.9 + 0.2) % 1 - 0.5))

        la_x = grav_x + centripetal_x + noise_x
        la_y = grav_y + centripetal_y + noise_y
        la_z = grav_z + noise_z

        # ── Vertical speed ─────────────────────────────────
        if i > 0:
            prev_alt = frames[-1]["baroAltitudeM"]
            vspeed = (altitude - prev_alt) / DT_S
        else:
            vspeed = -avg_vspeed

        frame = {
            "msFromStart": ms_from_start,
            "qW": round(qw, 6),
            "qX": round(qx, 6),
            "qY": round(qy, 6),
            "qZ": round(qz, 6),
            "laX": round(la_x, 4),
            "laY": round(la_y, 4),
            "laZ": round(la_z, 4),
            "baroPressurePa": round(pa, 2),
            "baroAltitudeM": round(altitude, 4),
            "verticalSpeedMs": round(vspeed, 2),
        }
        frames.append(frame)

    return frames


def main():
    output_file = sys.argv[1] if len(sys.argv) > 1 else "gs_run_synthetic.json"

    frames = generate_frames()

    # Impact events (at gate frames, with realistic force)
    impacts = []
    for g, gf in enumerate(gate_frames):
        # Force varies by gate (mostly 1.5-4G)
        force = 1.5 + (g % 5) * 0.5 + 0.3 * (2 * ((g * 0.73) % 1) - 1)
        impacts.append({
            "msFromStart": gf * 10,  # frame → ms
            "gateNumber": g + 1,
            "force": round(force, 1),
        })

    now = int(datetime.now(timezone.utc).timestamp())

    result = {
        "header": {
            "formatVersion": 2,
            "armSide": 1,  # right arm
            "startTimestamp": now - 60,  # 1 minute ago
            "baroTempC": -5.0,  # cold mountain day
            "compressedSize": 0,  # raw frames, not compressed
            "calAccuracy": 3,  # high accuracy
        },
        "frames": frames,
        "frameCount": NUM_FRAMES,
        "totalDurationSec": DURATION_S,
        "impactEvents": impacts,
        "_meta": {
            "description": "Synthetic GS run: 150m drop, 45s, 30 gates @ ~25m spacing",
            "sampleRateHz": FPS,
            "startAltitudeM": START_ALTITUDE_M,
            "endAltitudeM": END_ALTITUDE_M,
            "numGates": NUM_GATES,
            "estimatedGateTimesMs": [gf * 10 for gf in gate_frames],
        },
    }

    with open(output_file, 'w') as f:
        json.dump(result, f, indent=2)

    print(f"✅ Generated {NUM_FRAMES} frames, {NUM_GATES} gates, {DURATION_S}s run")
    print(f"   Saved to: {output_file}")
    print(f"   Altitude: {START_ALTITUDE_M:.0f}m → {END_ALTITUDE_M:.0f}m")
    print(f"   Avg speed: {avg_vspeed:.1f} m/s vertical")
    for i, gf in enumerate(gate_frames):
        t = gf * 10 / 1000.0
        print(f"   Gate {i+1:2d}: {t:5.2f}s ({int(gf*10)}ms) force={impacts[i]['force']}G")


if __name__ == "__main__":
    main()
