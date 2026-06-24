#!/usr/bin/env python3
"""
generate_phone_test_data.py — Deterministic test-data generator for SGC phone app.

Produces identical binary blobs to test/data/synthetic_data.dart.
This is the regeneration script — if you change the compression format,
update BOTH this script AND the Dart module.

Usage:
    python scripts/generate_phone_test_data.py

Output:
    test/data/generated/  — binary test fixtures
"""

import struct
import json
import os
import math
from pathlib import Path

# ═══════════════════════════════════════════════════════════════════
# Constants matching the Dart test_data module
# ═══════════════════════════════════════════════════════════════════

OUT_DIR = Path(__file__).parent.parent / "test" / "data" / "generated"
SEED = 42

# ═══════════════════════════════════════════════════════════════════
# Run header builder (16 bytes)
# ═══════════════════════════════════════════════════════════════════

def build_header(format_ver=2, arm_side=0, ts_utc=1719000000,
                 baro_temp_c=15.5, compressed_size=0, cal_accuracy=0):
    buf = bytearray(16)
    buf[0] = format_ver
    buf[1] = arm_side
    struct.pack_into('<I', buf, 2, ts_utc)
    struct.pack_into('<h', buf, 6, int(baro_temp_c * 10))
    struct.pack_into('<I', buf, 8, compressed_size)
    buf[12] = cal_accuracy
    return bytes(buf)

# ═══════════════════════════════════════════════════════════════════
# Frame encoders
# ═══════════════════════════════════════════════════════════════════

def _sign_ext_4(v):
    return v | 0xFFFFFFF0 if (v & 0x08) else v

def encode_type1(delta_ms, baro_div4, deltas_7):
    """Type 1: 4-bit deltas, 8 bytes total."""
    pkt_type = 0
    word0 = (pkt_type << 14) | (delta_ms & 0x03FF)
    buf = bytearray(8)
    struct.pack_into('<H', buf, 0, word0)
    struct.pack_into('<H', buf, 2, baro_div4 & 0xFFFF)
    # 7 × 4-bit deltas → 4 bytes
    buf[4] = ((deltas_7[0] & 0x0F) << 4) | (deltas_7[1] & 0x0F)
    buf[5] = ((deltas_7[2] & 0x0F) << 4) | (deltas_7[3] & 0x0F)
    buf[6] = ((deltas_7[4] & 0x0F) << 4) | (deltas_7[5] & 0x0F)
    buf[7] = ((deltas_7[6] & 0x0F) << 4)  # last nibble pad
    return bytes(buf)

def encode_type2(delta_ms, baro_div4, deltas_7):
    """Type 2: 8-bit deltas, 11 bytes total."""
    pkt_type = 1
    word0 = (pkt_type << 14) | (delta_ms & 0x03FF)
    buf = bytearray(11)
    struct.pack_into('<H', buf, 0, word0)
    struct.pack_into('<H', buf, 2, baro_div4 & 0xFFFF)
    for i, d in enumerate(deltas_7):
        buf[4 + i] = d & 0xFF
    return bytes(buf)

def encode_type3(delta_ms, baro_div4, abs_7):
    """Type 3: 16-bit absolute, 18 bytes total."""
    pkt_type = 3
    word0 = (pkt_type << 14) | (delta_ms & 0x03FF)
    buf = bytearray(18)
    struct.pack_into('<H', buf, 0, word0)
    struct.pack_into('<H', buf, 2, baro_div4 & 0xFFFF)
    for i, v in enumerate(abs_7):
        struct.pack_into('<h', buf, 4 + i * 2, v)
    return bytes(buf)

# ═══════════════════════════════════════════════════════════════════
# Fixture generators
# ═══════════════════════════════════════════════════════════════════

def generate_mixed_type_run():
    """10-frame run with all 3 packet types."""
    types = [1, 1, 1, 2, 2, 2, 3, 1, 2, 1]
    deltas = [10] * 10
    baros = [25000] * 10
    payloads = [
        [0, 0, 0, 0, 0, 0, 0],
        [1, 0, -1, 0, 0, 0, 0],
        [0, 1, 0, -1, 0, 0, 0],
        [0, 0, 0, 0, 2, -2, 1],
        [-1, 1, -1, 1, 0, 0, 0],
        [0, 0, 0, 0, 0, 0, 0],
        [100, 50, -30, 20, 500, -200, 300],
        [0, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 0, 0, 0],
    ]

    data = build_header(compressed_size=len(types) * 11)
    for i in range(10):
        t = types[i]
        if t == 2:
            data += encode_type2(deltas[i], baros[i], payloads[i])
        elif t == 3:
            data += encode_type3(deltas[i], baros[i], payloads[i])
        else:
            data += encode_type1(deltas[i], baros[i], payloads[i])

    return data, "10-frame run mixing types 1, 2, 3"

def generate_steady_descent_run():
    """100-frame run: pressure drops from 101325 to 98000 Pa."""
    n = 100
    types = [2] * n
    deltas = [10] * n
    # 25000 → 24500 in baro_div4 (100000 → 98000 Pa)
    baros = [25000 - i * 2 for i in range(n)]
    payloads = [[0, 0, 0, 0, 0, 0, 0] for _ in range(n)]

    data = build_header(compressed_size=n * 11)
    for i in range(n):
        data += encode_type2(deltas[i], baros[i], payloads[i])

    return data, "100-frame steady descent (200 Pa drop)"

def generate_impact_frames(total=200, impact_frame=150, force_g=5.0, baseline_g=1.0):
    """JSON test data for impact detector."""
    frames = []
    for i in range(total):
        d = i - impact_frame
        spike = force_g * math.exp(-d * d / 4.0)
        noise = math.sin(i * 0.7) * 0.2 + baseline_g
        mag_g = noise + spike
        la = mag_g * 9.81
        frames.append({
            "msFromStart": i * 10,
            "laX": la * 0.5, "laY": la * 0.7, "laZ": la * 0.3,
        })
    return frames

def generate_slalom_impact_frames():
    """6 impacts at known frames."""
    total = 600
    impacts = [50, 140, 230, 320, 410, 500]
    frames = []
    for i in range(total):
        spike = sum(3.5 * math.exp(-(i - imp) ** 2 / 4.0) for imp in impacts)
        mag = 1.0 + spike + math.sin(i * 0.3) * 0.15
        frames.append({
            "msFromStart": i * 10,
            "laX": mag * 0.5 * 9.81,
            "laY": mag * 0.7 * 9.81,
            "laZ": mag * 0.3 * 9.81,
        })
    return frames

def generate_correlated_frames(total=500, offset_ms=150, noise_scale=0.02):
    """Left/right frame pairs with known offset."""
    import random
    rng = random.Random(42)
    offset_frames = offset_ms // 10

    def build(right_shifted):
        frames = []
        for i in range(total):
            t = (i - (offset_frames if right_shifted else 0)) / 150.0
            omega = math.sin(2 * math.pi * t)
            mag = 0.7 + omega * 0.3
            qw = math.cos(mag / 2)
            axis = math.sin(mag / 2)
            frames.append({
                "msFromStart": i * 10,
                "qW": qw,
                "qX": axis * 0.8 + rng.random() * noise_scale,
                "qY": axis * 0.3 + rng.random() * noise_scale,
                "qZ": axis * 0.5 + rng.random() * noise_scale,
                "laX": 0.0, "laY": 0.0, "laZ": 0.0,
            })
        return frames

    return {"left": build(False), "right": build(True)}

def generate_slalom_run_frames(num_gates=10, frames_per_gate=100):
    """Full slalom run with alternating turns and impacts."""
    total = num_gates * frames_per_gate
    rng = random.Random(123)
    pressure = 101325.0
    quat_mag = 0.7
    frames = []

    for i in range(total):
        gate_idx = i // frames_per_gate
        pos_in_gate = i % frames_per_gate
        side = 1.0 if gate_idx % 2 == 0 else -1.0
        turn_progress = pos_in_gate / frames_per_gate

        angle = side * turn_progress * math.pi
        qw = math.cos(angle / 2)
        qx = 0.0
        qy = math.sin(angle / 2) * quat_mag
        qz = 0.0

        dist_from_gate = abs(pos_in_gate - frames_per_gate / 2)
        impact = 4.0 * math.exp(-dist_from_gate ** 2 / 2.0) if dist_from_gate < 3 else 0.0

        la = 1.0 + impact + rng.random() * 0.2
        la_x = la * 0.4 * side * 9.81
        la_y = la * 0.8 * 9.81
        la_z = la * 0.3 * 9.81

        pressure -= 2.0 + rng.random() * 0.5

        frames.append({
            "msFromStart": i * 10,
            "qW": qw, "qX": qx, "qY": qy, "qZ": qz,
            "laX": la_x, "laY": la_y, "laZ": la_z,
            "baroPressurePa": pressure,
        })

    return frames

# ═══════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════

import random

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    fixtures = {}

    # Binary fixtures (for decompressor)
    mixed, mixed_desc = generate_mixed_type_run()
    descent, descent_desc = generate_steady_descent_run()

    with open(OUT_DIR / "mixed_type_run.bin", "wb") as f:
        f.write(mixed)
    fixtures["mixed_type_run.bin"] = mixed_desc

    with open(OUT_DIR / "steady_descent_run.bin", "wb") as f:
        f.write(descent)
    fixtures["steady_descent_run.bin"] = descent_desc

    # JSON fixtures (for impact, correlator, gate estimator)
    impact_frames = generate_impact_frames()
    slalom_impacts = generate_slalom_impact_frames()
    correlated = generate_correlated_frames()
    slalom_run = generate_slalom_run_frames()

    json_fixtures = {
        "impact_frames.json": impact_frames,
        "slalom_impact_frames.json": slalom_impacts,
        "correlated_frames.json": correlated,
        "slalom_run_frames.json": slalom_run,
    }
    for name, data in json_fixtures.items():
        with open(OUT_DIR / name, "w") as f:
            json.dump(data, f, indent=2)

    # Manifest
    print(f"Generated {len(fixtures) + len(json_fixtures)} fixtures in {OUT_DIR}:")
    for name, desc in fixtures.items():
        size = os.path.getsize(OUT_DIR / name)
        print(f"  {name:35s} {size:6d} B  — {desc}")
    for name in json_fixtures:
        size = os.path.getsize(OUT_DIR / name)
        print(f"  {name:35s} {size:6d} B  — JSON fixture")
    print("\nDone. These match test/data/synthetic_data.dart constants.")

if __name__ == "__main__":
    main()
