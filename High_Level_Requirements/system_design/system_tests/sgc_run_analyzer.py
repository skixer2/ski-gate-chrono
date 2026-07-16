#!/usr/bin/env python3
"""
SGC Run Analyzer — validates raw binary run data using the EXACT same
decompression and processing as the Flutter phone app (decompressor.dart).

Usage:
    # From a binary run file (already downloaded by the app or dumped via serial):
    python sgc_run_analyzer.py run_0.bin

    # From serial port (dumps run N and analyzes):
    python sgc_run_analyzer.py --port COM8 --dump 0

    # Compare with synthetic data:
    python sgc_run_analyzer.py --synthetic

Input format: Raw binary as stored on the MX25R1635F flash.
  [16-byte RunHeader] [compressed frames...] [0xC3] [0x32] [CRC32_LE:4B]
"""

import struct
import sys
import math
import io
import json
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


# ═══════════════════════════════════════════════════════════════════
# RunHeader (matches decompressor.dart RunHeader + flash_manager.h)
# ═══════════════════════════════════════════════════════════════════

RUN_HEADER_FMT = "<B B I h I H B B"  # 16 bytes packed, little-endian
RUN_HEADER_SIZE = 16


@dataclass
class RunHeader:
    format_version: int
    arm_side: int        # 0=left, 1=right
    start_timestamp: int  # UTC unixtime
    baro_temp_c: float    # tenths °C
    compressed_size: int  # bytes of compressed frame data (excl. header)
    frame_count: int      # number of compressed frames
    cal_accuracy: int     # 0-3

    @classmethod
    def parse(cls, data: bytes) -> "RunHeader":
        if len(data) < RUN_HEADER_SIZE:
            raise ValueError(f"Run file too short for header: {len(data)} bytes")
        (
            fmt_ver, arm, ts, baro_temp_raw, comp_size, fcount, cal, _pad
        ) = struct.unpack_from(RUN_HEADER_FMT, data, 0)
        # baro_temp_raw is signed int16 in tenths °C
        if baro_temp_raw > 32767:
            baro_temp_raw -= 65536
        return cls(
            format_version=fmt_ver,
            arm_side=arm,
            start_timestamp=ts,
            baro_temp_c=baro_temp_raw / 10.0,
            compressed_size=comp_size,
            frame_count=fcount,
            cal_accuracy=cal,
        )


# ═══════════════════════════════════════════════════════════════════
# SensorFrame (matches sensor_frame.dart)
# ═══════════════════════════════════════════════════════════════════

@dataclass
class SensorFrame:
    ms_from_start: int
    q_w: float; q_x: float; q_y: float; q_z: float
    la_x: float; la_y: float; la_z: float
    baro_pressure_pa: float
    baro_altitude_m: float
    vertical_speed_ms: float = 0.0


# ═══════════════════════════════════════════════════════════════════
# BarometricPoint (matches barometric_point.dart)
# ═══════════════════════════════════════════════════════════════════

@dataclass
class BarometricPoint:
    ms_from_start: int
    altitude_m: float
    vertical_speed_ms: float


# ═══════════════════════════════════════════════════════════════════
# Decompressor — EXACT mirror of decompressor.dart
# ═══════════════════════════════════════════════════════════════════

def sign_extend_4(v: int) -> int:
    """4-bit sign extension (matches _signExt4)."""
    return v - 16 if (v & 0x08) else (v & 0x0F)


def baro_altitude_m(baro_pa: float) -> float:
    """
    ISA barometric altitude formula.
    EXACT match for decompressor.dart:
      44330 * (1 - pow(baroPa / 101325, 0.1903))
    """
    if baro_pa <= 0:
        return 44330.0  # _pow(b<=0) returns 0 → altitude = 44330
    return 44330.0 * (1.0 - math.pow(baro_pa / 101325.0, 0.1903))


def decompress(data: bytes) -> Tuple[RunHeader, List[SensorFrame]]:
    """
    Decompress binary run data. EXACT mirror of Decompressor.decompress().

    Returns (RunHeader, list of SensorFrame).
    """
    if len(data) < RUN_HEADER_SIZE:
        raise ValueError(f"Data too short: {len(data)} bytes (need ≥{RUN_HEADER_SIZE})")

    header = RunHeader.parse(data)

    frames: List[SensorFrame] = []
    offset = RUN_HEADER_SIZE  # skip 16-byte run header

    # Accumulators (exact same types as Dart decompressor)
    q_w = 0.0; q_x = 0.0; q_y = 0.0; q_z = 0.0
    la_x = 0.0; la_y = 0.0; la_z = 0.0
    baro_pa_div2 = 0  # cumulative baro accumulator (Pa/2)
    acc_ms = 0

    pkt_stats = {0: 0, 1: 0, 2: 0, 3: 0}

    while offset + 2 <= len(data):
        # Header: 2 bytes (little-endian uint16)
        delta_ms_full = data[offset] | (data[offset + 1] << 8)
        pkt_type = (delta_ms_full >> 14) & 0x03
        offset += 2

        if pkt_type == 2:
            # ── Type 3 (anchor): 7×int16 IMU + 1×uint16 baro = 16 bytes payload ──
            if offset + 16 > len(data):
                break

            def i16(buf, off):
                val = buf[off] | (buf[off + 1] << 8)
                return val - 65536 if val >= 32768 else val

            def u16(buf, off):
                return buf[off] | (buf[off + 1] << 8)

            q_w = float(i16(data, offset)) / 16384.0
            q_x = float(i16(data, offset + 2)) / 16384.0
            q_y = float(i16(data, offset + 4)) / 16384.0
            q_z = float(i16(data, offset + 6)) / 16384.0
            la_x = float(i16(data, offset + 8))
            la_y = float(i16(data, offset + 10))
            la_z = float(i16(data, offset + 12))
            baro_pa_div2 = u16(data, offset + 14)
            offset += 16

        elif pkt_type == 1:
            # ── Type 2: 7×int8 IMU deltas + 1×int8 baro_delta = 8 bytes payload ──
            if offset + 8 > len(data):
                break

            def i8(b):
                return b - 256 if b >= 128 else b

            q_w += float(i8(data[offset])) / 16384.0
            q_x += float(i8(data[offset + 1])) / 16384.0
            q_y += float(i8(data[offset + 2])) / 16384.0
            q_z += float(i8(data[offset + 3])) / 16384.0
            la_x += float(i8(data[offset + 4]))
            la_y += float(i8(data[offset + 5]))
            la_z += float(i8(data[offset + 6]))
            baro_pa_div2 += i8(data[offset + 7])
            offset += 8

        else:
            # ── Type 1: 8×int4 (7 IMU + 1 baro) = 4 bytes payload ──
            if offset + 4 > len(data):
                break

            b0, b1, b2, b3 = data[offset:offset + 4]
            q_w += float(sign_extend_4(b0 >> 4)) / 16384.0
            q_x += float(sign_extend_4(b0 & 0x0F)) / 16384.0
            q_y += float(sign_extend_4(b1 >> 4)) / 16384.0
            q_z += float(sign_extend_4(b1 & 0x0F)) / 16384.0
            la_x += float(sign_extend_4(b2 >> 4))
            la_y += float(sign_extend_4(b2 & 0x0F))
            la_z += float(sign_extend_4(b3 >> 4))
            baro_pa_div2 += sign_extend_4(b3 & 0x0F)
            offset += 4

        pkt_stats[pkt_type] = pkt_stats.get(pkt_type, 0) + 1
        acc_ms += (delta_ms_full & 0x03FF)
        baro_pa = baro_pa_div2 * 2.0  # Pa/2 → Pa
        alt_m = baro_altitude_m(baro_pa)

        frames.append(SensorFrame(
            ms_from_start=acc_ms,
            q_w=q_w, q_x=q_x, q_y=q_y, q_z=q_z,
            la_x=la_x, la_y=la_y, la_z=la_z,
            baro_pressure_pa=baro_pa,
            baro_altitude_m=alt_m,
        ))

    return header, frames, pkt_stats


# ═══════════════════════════════════════════════════════════════════
# Run processor — mirrors run_detail_screen.dart _processRun()
# ═══════════════════════════════════════════════════════════════════

def compute_baro_data(frames: List[SensorFrame]) -> List[BarometricPoint]:
    """
    Decimate 100 Hz frames to ~10 Hz barometric points with vertical speed.
    EXACT mirror of RunDetailScreen._computeBaroData().
    """
    result = []
    for i in range(0, len(frames), 10):
        result.append(BarometricPoint(
            ms_from_start=frames[i].ms_from_start,
            altitude_m=frames[i].baro_altitude_m,
            vertical_speed_ms=0.0,
        ))

    # Compute vertical speed as altitude derivative
    for i in range(1, len(result)):
        dt = (result[i].ms_from_start - result[i - 1].ms_from_start) / 1000.0
        if dt > 0:
            speed = (result[i].altitude_m - result[i - 1].altitude_m) / dt
            result[i - 1] = BarometricPoint(
                ms_from_start=result[i - 1].ms_from_start,
                altitude_m=result[i - 1].altitude_m,
                vertical_speed_ms=speed,
            )

    # Last point: copy penultimate speed
    if len(result) >= 2:
        result[-1] = BarometricPoint(
            ms_from_start=result[-1].ms_from_start,
            altitude_m=result[-1].altitude_m,
            vertical_speed_ms=result[-2].vertical_speed_ms,
        )

    return result


def compute_stats(header: RunHeader, frames: List[SensorFrame],
                  baro_data: List[BarometricPoint]) -> dict:
    """Compute diagnostic statistics for a run."""
    if not frames:
        return {"error": "No frames"}

    alts = [f.baro_altitude_m for f in frames]
    pressures = [f.baro_pressure_pa for f in frames]

    # Baro pressure stats
    pa_min = min(pressures)
    pa_max = max(pressures)
    pa_mean = sum(pressures) / len(pressures)
    pa_std = math.sqrt(sum((p - pa_mean) ** 2 for p in pressures) / len(pressures))

    # BaroPaDiv2 extremes
    # Reconstruct baroPaDiv2 from pressure
    baro_div2_vals = [p / 2.0 for p in pressures]
    bd2_min = min(baro_div2_vals)
    bd2_max = max(baro_div2_vals)

    # Altitude stats
    alt_min = min(alts)
    alt_max = max(alts)
    alt_mean = sum(alts) / len(alts)
    alt_span = alt_max - alt_min

    # Anomaly detection: count frames with altitude > 8000m or < -500m
    alt_high_count = sum(1 for a in alts if a > 8000)
    alt_low_count = sum(1 for a in alts if a < -500)
    alt_extreme_count = alt_high_count + alt_low_count

    # Vertical speed stats (from baro data)
    if baro_data:
        vspeeds = [b.vertical_speed_ms for b in baro_data]
        vs_max = max(vspeeds)
        vs_min = min(vspeeds)
        vs_abs_mean = sum(abs(v) for v in vspeeds) / len(vspeeds)  # "Avg Speed" in app
        vs_extreme_count = sum(1 for v in vspeeds if abs(v) > 20)
    else:
        vs_max = vs_min = vs_abs_mean = vs_extreme_count = 0

    # Frame timing
    duration_s = frames[-1].ms_from_start / 1000.0 if frames else 0
    gaps = [frames[i].ms_from_start - frames[i - 1].ms_from_start
            for i in range(1, len(frames))]
    max_gap_ms = max(gaps) if gaps else 0
    gap_issues = sum(1 for g in gaps if g > 20)  # gaps > 20ms at 100Hz nominal

    # IMU quaternion magnitude (should be Q14 ~16384 for unit quat)
    q_mags = [math.sqrt(f.q_w**2 + f.q_x**2 + f.q_y**2 + f.q_z**2) for f in frames]
    q_mag_mean = sum(q_mags) / len(q_mags)
    q_mag_std = math.sqrt(sum((m - q_mag_mean)**2 for m in q_mags) / len(q_mags))

    # IMU linear accel stats
    la_mags = [math.sqrt(f.la_x**2 + f.la_y**2 + f.la_z**2) for f in frames]
    la_mag_mean = sum(la_mags) / len(la_mags)

    return {
        "header": {
            "format_version": header.format_version,
            "arm_side": "left" if header.arm_side == 0 else "right",
            "start_timestamp": header.start_timestamp,
            "baro_temp_c": header.baro_temp_c,
            "compressed_size": header.compressed_size,
            "frame_count": header.frame_count,
            "cal_accuracy": header.cal_accuracy,
        },
        "frames": {
            "decompressed": len(frames),
            "expected": header.frame_count,
            "match": len(frames) == header.frame_count,
            "duration_s": round(duration_s, 2),
            "max_gap_ms": max_gap_ms,
            "gap_issues": gap_issues,
        },
        "imu": {
            "quat_magnitude_mean": round(q_mag_mean, 4),
            "quat_magnitude_std": round(q_mag_std, 4),
            "la_magnitude_mean": round(la_mag_mean, 1),
            "quat_is_unit": 0.9 < q_mag_mean < 1.1,  # should be ~1.0 for unit quat
        },
        "baro_pressure": {
            "min_pa": round(pa_min, 1),
            "max_pa": round(pa_max, 1),
            "mean_pa": round(pa_mean, 1),
            "std_pa": round(pa_std, 1),
            "div2_min": round(bd2_min, 1),
            "div2_max": round(bd2_max, 1),
        },
        "altitude": {
            "min_m": round(alt_min, 1),
            "max_m": round(alt_max, 1),
            "mean_m": round(alt_mean, 1),
            "span_m": round(alt_span, 1),
            "extreme_count": alt_extreme_count,
            "high_anomalies": alt_high_count,
            "low_anomalies": alt_low_count,
        },
        "vertical_speed": {
            "max_ms": round(vs_max, 2),
            "min_ms": round(vs_min, 2),
            "abs_mean_ms": round(vs_abs_mean, 2),
            "extreme_count": vs_extreme_count,
        },
    }


# ═══════════════════════════════════════════════════════════════════
# Plotting
# ═══════════════════════════════════════════════════════════════════

def plot_run(header: RunHeader, frames: List[SensorFrame],
             baro_data: List[BarometricPoint],
             stats: dict, output_path: Optional[str] = None):
    """Generate altitude and speed plots using matplotlib."""
    try:
        import matplotlib
        matplotlib.use('Agg')  # headless
        import matplotlib.pyplot as plt
        import matplotlib.dates as mdates
    except ImportError:
        print("\n⚠ matplotlib not installed. Skipping plots.")
        print("  Install: pip install matplotlib")
        return

    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

    # ── Altitude plot ──
    ax = axes[0]
    times_s = [f.ms_from_start / 1000.0 for f in frames]
    alts = [f.baro_altitude_m for f in frames]
    ax.plot(times_s, alts, linewidth=0.5, color='steelblue', alpha=0.7)
    ax.set_ylabel('Altitude (m)')
    ax.set_title(f'Run — {stats["frames"]["duration_s"]}s, '
                 f'{len(frames)} frames, '
                 f'alt range: [{stats["altitude"]["min_m"]}, {stats["altitude"]["max_m"]}]m')
    ax.grid(True, alpha=0.3)

    # Add baro data overlay (decimated)
    if baro_data:
        baro_t = [b.ms_from_start / 1000.0 for b in baro_data]
        baro_a = [b.altitude_m for b in baro_data]
        ax.plot(baro_t, baro_a, linewidth=1.0, color='darkblue', alpha=1.0,
                label='10 Hz decimated')
        ax.legend(fontsize=8)

    # Mark anomalies
    anomaly_times = [t for t, a in zip(times_s, alts) if a > 8000 or a < -500]
    anomaly_alts = [a for a in alts if a > 8000 or a < -500]
    if anomaly_times:
        ax.scatter(anomaly_times, anomaly_alts, s=4, color='red', alpha=0.5,
                   label=f'{len(anomaly_times)} anomalies')

    # ── Vertical speed plot ──
    ax = axes[1]
    if baro_data:
        baro_t = [b.ms_from_start / 1000.0 for b in baro_data]
        vspeeds = [b.vertical_speed_ms for b in baro_data]
        ax.plot(baro_t, vspeeds, linewidth=0.8, color='darkorange', alpha=0.8)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Vertical Speed (m/s)')
    ax.grid(True, alpha=0.3)
    ax.axhline(y=0, color='gray', linewidth=0.5, linestyle='--')

    # Mark extreme speeds
    if baro_data:
        extreme_t = [t for t, v in zip(baro_t, vspeeds) if abs(v) > 20]
        extreme_v = [v for v in vspeeds if abs(v) > 20]
        if extreme_t:
            ax.scatter(extreme_t, extreme_v, s=4, color='red', alpha=0.5)

    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"\n📊 Plot saved to: {output_path}")
    else:
        # Show interactively if possible
        plt.show()
    plt.close()


# ═══════════════════════════════════════════════════════════════════
# Synthetic run generator (for testing the decompressor in isolation)
# ═══════════════════════════════════════════════════════════════════

def generate_synthetic_run(output_path: str = "synthetic_run.bin"):
    """
    Generate a synthetic compressed run file that exercises all 3 packet types.
    This produces the same binary format the firmware writes to flash.
    """
    import struct

    buf = io.BytesIO()

    # ── Run header (16 bytes) ──
    # format_ver=2, arm_side=1 (right), ts=0, baro_temp=-50 (-5.0°C),
    # data_size=0 (placeholder), frame_count=0 (placeholder), cal=3
    header_data = struct.pack("<B B I h I H B B",
                              2,    # format_ver
                              1,    # arm_side
                              0,    # ts_utc
                              -50,  # baro_temp (tenths °C)
                              0,    # data_size (filled later)
                              0,    # frame_count (filled later)
                              3,    # cal_accuracy
                              0)    # _pad
    buf.write(header_data)

    baro_pa_div2 = 50000  # ~100000 Pa = sea level (Pa/2, fits uint16)
    q_w, q_x, q_y, q_z = 16384, 0, 0, 0  # Q14 unit quaternion (×16384)
    la_x, la_y, la_z = 0, 0, -9810  # 1g down in mm/s²

    frame_count = 0
    compressed_start = buf.tell()

    def write_header(stream, delta_ms, pkt_type):
        seq = frame_count & 0x0F
        hdr = (pkt_type << 14) | (seq << 10) | (delta_ms & 0x3FF)
        stream.write(struct.pack("<H", hdr))

    def write_t1(stream, deltas, baro_delta):
        """Type 1: 8×int4 = 4 bytes"""
        nibbles = []
        for d in deltas:
            d = max(-8, min(7, d))
            nibbles.append(d & 0x0F)
        bd = max(-8, min(7, baro_delta))
        nibbles.append(bd & 0x0F)
        stream.write(bytes([
            (nibbles[0] << 4) | nibbles[1],
            (nibbles[2] << 4) | nibbles[3],
            (nibbles[4] << 4) | nibbles[5],
            (nibbles[6] << 4) | nibbles[7],
        ]))

    def write_t2(stream, deltas, baro_delta):
        """Type 2: 8×int8 = 8 bytes"""
        for d in deltas:
            stream.write(bytes([max(-128, min(127, d)) & 0xFF]))
        stream.write(bytes([max(-128, min(127, baro_delta)) & 0xFF]))

    def write_t3(stream, qw, qx, qy, qz, lax, lay, laz, baro):
        """Type 3: 7×int16 + 1×uint16 = 16 bytes"""
        for v in [qw, qx, qy, qz, lax, lay, laz]:
            sv = max(-32768, min(32767, v))
            stream.write(struct.pack("<h", sv))
        stream.write(struct.pack("<H", max(0, min(65535, baro)) & 0xFFFF))

    # Frame 0: Type 3 anchor
    write_header(buf, 0, 2)
    write_t3(buf, q_w, q_x, q_y, q_z, la_x, la_y, la_z, baro_pa_div2)
    frame_count += 1

    # Frames 1-99: alternating Type 1 (coasting) and Type 2 (turning)
    # Simulate a ski run: gentle pressure decrease, periodic turns
    for i in range(1, 100):
        delta_ms = 10  # 100 Hz

        # Simulate turning: every 10 frames, do a turn with larger IMU deltas
        if i % 10 == 0:
            # Type 2: turning
            write_header(buf, delta_ms, 1)
            imu_deltas = [-5, 8, -3, 6, 120, 80, 50]  # larger changes
            baro_delta = -2  # slight pressure decrease
            write_t2(buf, imu_deltas, baro_delta)
            q_w += imu_deltas[0]; q_x += imu_deltas[1]; q_y += imu_deltas[2]; q_z += imu_deltas[3]
            la_x += imu_deltas[4]; la_y += imu_deltas[5]; la_z += imu_deltas[6]
            baro_pa_div2 += baro_delta
        else:
            # Type 1: coasting
            write_header(buf, delta_ms, 0)
            imu_deltas = [0, 1, 0, -1, 2, -1, 3]  # very small changes
            baro_delta = -1  # gradual pressure decrease (descending)
            write_t1(buf, imu_deltas, baro_delta)
            q_w += imu_deltas[0]; q_x += imu_deltas[1]; q_y += imu_deltas[2]; q_z += imu_deltas[3]
            la_x += imu_deltas[4]; la_y += imu_deltas[5]; la_z += imu_deltas[6]
            baro_pa_div2 += baro_delta

        frame_count += 1

    # Frames 100-101: forced Type 3 anchor every 100 frames
    write_header(buf, 10, 2)
    write_t3(buf, q_w, q_x, q_y, q_z, la_x, la_y, la_z, baro_pa_div2)
    frame_count += 1

    # ── CRC32 trailer ──
    compressed_end = buf.tell()
    compressed_data = buf.getvalue()[RUN_HEADER_SIZE:compressed_end]

    # Compute CRC32
    crc = 0xFFFFFFFF
    for b in compressed_data:
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ b) & 0xFF]
    crc ^= 0xFFFFFFFF

    buf.write(bytes([0xC3, 0x32]))  # magic
    buf.write(struct.pack("<I", crc))

    # ── Update header with actual sizes ──
    total = buf.getvalue()
    compressed_size = compressed_end - compressed_start
    header_data = struct.pack("<B B I h I H B B",
                              2,    # format_ver
                              1,    # arm_side
                              0,    # ts_utc
                              -50,  # baro_temp
                              compressed_size,
                              frame_count,
                              3,    # cal_accuracy
                              0)

    final = header_data + total[RUN_HEADER_SIZE:]

    with open(output_path, 'wb') as f:
        f.write(final)

    print(f"✅ Synthetic run: {frame_count} frames, {compressed_size} compressed bytes")
    print(f"   Saved to: {output_path}")
    print(f"   Expected baro range: {baro_pa_div2 * 2} Pa (start) → {baro_pa_div2} Pa/2")
    return output_path


# CRC32 table (Ethernet polynomial)
CRC32_TABLE = [
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,
    0xE963A535,0x9E6495A3,0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,
    0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,0x1DB71064,0x6AB020F2,
    0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,
    0xFA0F3D63,0x8D080DF5,0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,
    0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,0x35B5A8FA,0x42B2986C,
    0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,
    0xCFBA9599,0xB8BDA50F,0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,
    0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,0x76DC4190,0x01DB7106,
    0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,
    0x91646C97,0xE6635C01,0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,
    0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,0x65B0D9C6,0x12B7E950,
    0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,
    0xA4D1C46D,0xD3D6F4FB,0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,
    0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,0x5005713C,0x270241AA,
    0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,
    0xB7BD5C3B,0xC0BA6CAD,0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,
    0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,0xE3630B12,0x94643B84,
    0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,
    0x196C3671,0x6E6B06E7,0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,
    0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,0xD6D6A3E8,0xA1D1937E,
    0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,
    0x316E8EEF,0x4669BE79,0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,
    0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,0xC5BA3BBE,0xB2BD0B28,
    0x2BB45A92,0x5CB30A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
    0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,
    0x72076785,0x05005713,0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,
    0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,0x86D3D2D4,0xF1D4E242,
    0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
    0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,
    0x616BFFD3,0x166CCF45,0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,
    0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,0xAED16A4A,0xD9D65ADC,
    0x40BF0B66,0x37B83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
    0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,
    0x54DE5729,0x23D967BF,0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,
    0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D
]


# ═══════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════

def analyze_file(path: str, plot: bool = True) -> dict:
    """Analyze a binary run file."""
    with open(path, 'rb') as f:
        data = f.read()

    print(f"📂 Loaded {path}: {len(data)} bytes")

    # Check for CRC32 trailer
    has_crc = False
    crc_ok = False
    if len(data) >= 6:
        if data[-6] == 0xC3 and data[-5] == 0x32:
            has_crc = True
            stored_crc = struct.unpack_from("<I", data, len(data) - 4)[0]
            payload = data[:len(data) - 6]
            crc = 0xFFFFFFFF
            for b in payload:
                crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ b) & 0xFF]
            crc ^= 0xFFFFFFFF
            crc_ok = (stored_crc == crc)
            if crc_ok:
                print(f"✅ CRC32 OK (0x{stored_crc:08X})")
            else:
                print(f"❌ CRC32 MISMATCH: stored=0x{stored_crc:08X}, computed=0x{crc:08X}")

    header, frames, pkt_stats = decompress(data)
    baro_data = compute_baro_data(frames)
    stats = compute_stats(header, frames, baro_data)

    print(f"\n{'='*60}")
    print(f"Run Header")
    print(f"{'='*60}")
    print(f"  Format:        v{header.format_version}")
    print(f"  Arm side:      {['left','right'][header.arm_side]}")
    print(f"  Temp:          {header.baro_temp_c:.1f}°C")
    print(f"  Compressed:    {header.compressed_size} bytes")
    print(f"  Frame count:   {header.frame_count}")
    print(f"  Cal accuracy:  {header.cal_accuracy}")

    print(f"\n{'='*60}")
    print(f"Decompression")
    print(f"{'='*60}")
    print(f"  Frames:        {len(frames)} (expected {header.frame_count})")
    print(f"  Duration:      {stats['frames']['duration_s']}s")
    print(f"  Max gap:       {stats['frames']['max_gap_ms']}ms")
    print(f"  Gap issues:    {stats['frames']['gap_issues']} (>20ms gaps)")
    print(f"  Packet types:  T1={pkt_stats[0]}, T2={pkt_stats[1]}, T3={pkt_stats[2]}" + 
                 (f", RESERVED(3)={pkt_stats[3]} ⚠" if pkt_stats[3] > 0 else ""))

    print(f"\n{'='*60}")
    print(f"IMU Diagnostics")
    print(f"{'='*60}")
    print(f"  Quat mag:      {stats['imu']['quat_magnitude_mean']:.4f} ± {stats['imu']['quat_magnitude_std']:.4f}")
    print(f"  Is unit quat?  {'✅ YES' if stats['imu']['quat_is_unit'] else '❌ NO (expected ~1.0, got {:.1f})'.format(stats['imu']['quat_magnitude_mean'])}")
    print(f"  LA mag mean:   {stats['imu']['la_magnitude_mean']:.1f} mm/s²")

    print(f"\n{'='*60}")
    print(f"Barometric Pressure")
    print(f"{'='*60}")
    print(f"  Range:         [{stats['baro_pressure']['min_pa']}, {stats['baro_pressure']['max_pa']}] Pa")
    print(f"  Mean ± σ:      {stats['baro_pressure']['mean_pa']:.1f} ± {stats['baro_pressure']['std_pa']:.1f} Pa")
    print(f"  Pa/2 range:    [{stats['baro_pressure']['div2_min']}, {stats['baro_pressure']['div2_max']}]")
    print(f"  Expected:      ~50000-65000 Pa/2 for typical altitudes")
    if stats['baro_pressure']['max_pa'] > 150000:
        print(f"  ⚠ WARNING: Pressure exceeds reasonable max (150 kPa)!")
    if stats['baro_pressure']['min_pa'] < 30000:
        print(f"  ⚠ WARNING: Pressure below reasonable min (30 kPa)!")
    if stats['baro_pressure']['div2_min'] < 0:
        print(f"  🔴 CRITICAL: Negative Pa/2 values — baro accumulator underflow!")
    if stats['baro_pressure']['div2_max'] > 65535:
        print(f"  🔴 CRITICAL: Pa/2 exceeds uint16 max — value won't fit in sensor encoding!")

    print(f"\n{'='*60}")
    print(f"Altitude")
    print(f"{'='*60}")
    print(f"  Range:         [{stats['altitude']['min_m']}, {stats['altitude']['max_m']}] m")
    print(f"  Mean:          {stats['altitude']['mean_m']:.1f} m")
    print(f"  Span:          {stats['altitude']['span_m']:.1f} m")
    print(f"  Extreme:       {stats['altitude']['extreme_count']} frames ({stats['altitude']['extreme_count']/max(len(frames),1)*100:.1f}%)")
    if stats['altitude']['extreme_count'] > 0:
        print(f"    High (>8km): {stats['altitude']['high_anomalies']}")
        print(f"    Low (<-500m): {stats['altitude']['low_anomalies']}")
    if stats['altitude']['span_m'] > 500:
        print(f"  ⚠ Altitude span >500m — possible sensor or decompression issue")
    if stats['altitude']['max_m'] > 8000:
        print(f"  🔴 Altitude exceeds 8000m — data corruption or sensor error")

    print(f"\n{'='*60}")
    print(f"Vertical Speed (from altitude derivative)")
    print(f"{'='*60}")
    print(f"  Range:         [{stats['vertical_speed']['min_ms']}, {stats['vertical_speed']['max_ms']}] m/s")
    print(f"  |Avg| (app):   {stats['vertical_speed']['abs_mean_ms']:.2f} m/s")
    print(f"  Extreme:       {stats['vertical_speed']['extreme_count']} points (|v|>20 m/s)")
    if stats['vertical_speed']['abs_mean_ms'] > 50:
        print(f"  🔴 |Avg| >50 m/s — impossible for skiing (terminal velocity ~60 m/s)")

    if plot:
        plot_path = path.rsplit('.', 1)[0] + '_analysis.png'
        plot_run(header, frames, baro_data, stats, plot_path)

    return stats


def dump_from_serial(port: str, run_id: int = None, baud: int = 115200):
    """Connect to device via serial and dump a run.
    If run_id is None, lists available runs and dumps the most recent one."""
    try:
        import serial
        import serial.tools.list_ports
    except ImportError:
        print("❌ pyserial not installed. Install: pip install pyserial")
        return None

    # Find port if 'auto'
    if port == 'auto':
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if any(k in p.description.lower() for k in ['nicla', 'arduino', 'mbed', 'nrf52']):
                port = p.device
                break
        if port == 'auto' and ports:
            port = ports[0].device
        if port == 'auto':
            print("❌ No serial port found")
            return None

    print(f"🔌 Connecting to {port} at {baud} baud...")
    ser = serial.Serial(port, baud, timeout=3)
    import time
    time.sleep(0.5)

    # ── Clean up: drain residual data, reset to IDLE ──
    print("🧹 Draining residual serial data...")
    ser.reset_input_buffer()
    ser.write(b'i\n')
    ser.flush()
    time.sleep(0.3)
    # Drain any JSON responses
    ser.timeout = 0.2
    while True:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if not line:
            break
    ser.timeout = 3

    # ── If run_id not specified, query device for run list ──
    if run_id is None:
        ser.write(b'?\n')
        ser.flush()
        time.sleep(0.3)
        ser.timeout = 0.5
        info = {}
        while True:
            line = ser.readline().decode('utf-8', errors='replace').strip()
            if not line:
                break
            try:
                obj = json.loads(line)
                if obj.get('ev') == 'status':
                    info = obj
            except json.JSONDecodeError:
                pass
        ser.timeout = 3

        runs = info.get('runs', 0)
        total = info.get('total_runs', 0)
        print(f"📋 Device has {runs} runs (total created: {total})")

        if runs == 0:
            print("❌ No runs on device")
            ser.close()
            return None

        # Default to most recent run
        run_id = total - 1
        print(f"   Dumping most recent: run #{run_id}")

        # Re-drain before dump
        ser.reset_input_buffer()

    # ── Send dump command ──
    cmd = f'D{run_id}\n'
    ser.write(cmd.encode())
    ser.flush()

    # Read JSON header line
    header_line = ser.readline().decode('utf-8', errors='replace').strip()
    try:
        meta = json.loads(header_line)
        if meta.get('ev') != 'run_dump':
            print(f"❌ Unexpected response: {header_line}")
            ser.close()
            return None
        data_size = meta.get('size', 0)
        run_id = meta.get('id', run_id)
        print(f"📥 Run #{run_id}: {data_size} bytes ({meta.get('frames', '?')} frames, {meta.get('compressed', '?')} compressed)")
    except json.JSONDecodeError:
        print(f"❌ Bad JSON header: {header_line}")
        ser.close()
        return None

    if data_size == 0:
        print("❌ Empty run (0 bytes)")
        ser.close()
        return None

    # Read binary data (loop to handle large transfers)
    import time as _time
    data = b''
    deadline = _time.time() + 15  # generous deadline
    ser.timeout = 2
    while len(data) < data_size and _time.time() < deadline:
        chunk = ser.read(min(data_size - len(data), 4096))
        if chunk:
            data += chunk
        else:
            _time.sleep(0.05)
    ser.close()

    if len(data) != data_size:
        print(f"⚠ Read {len(data)}/{data_size} bytes (timeout?)")

    # Save to file
    out_path = f'run_{run_id}.bin'
    with open(out_path, 'wb') as f:
        f.write(data)

    print(f"💾 Saved to: {out_path}")
    return out_path


def main():
    import argparse
    parser = argparse.ArgumentParser(
        description='SGC Run Analyzer — validates binary run data from device')
    parser.add_argument('file', nargs='?', help='Binary run file (.bin) to analyze')
    parser.add_argument('--port', default=None, help='Serial port for device dump (e.g. COM8)')
    parser.add_argument('--dump', type=int, default=None, nargs='?', const=None,
                        help='Run ID to dump from device (omit to auto-select latest)')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--synthetic', action='store_true',
                        help='Generate + analyze a synthetic test run')
    parser.add_argument('--no-plot', action='store_true', help='Skip matplotlib plots')
    parser.add_argument('--json', action='store_true', help='Output stats as JSON')

    args = parser.parse_args()

    if args.synthetic:
        path = generate_synthetic_run()
        stats = analyze_file(path, plot=not args.no_plot)
        if args.json:
            print(json.dumps(stats, indent=2))
        return

    # --dump without a value: auto-detect latest. --dump N: specific run.
    # argparse nargs='?' makes --dump alone set args.dump to const (None).
    # We distinguish: --dump not given at all → args.dump is default (still None via default=None).
    # But argparse doesn't distinguish "not given" from "--dump without value"
    # when const=None. So: if --port is given WITHOUT a file, imply dump mode.
    dump_requested = args.port and not args.file
    if dump_requested:
        dump_id = args.dump  # None = auto, int = specific
        path = dump_from_serial(args.port, dump_id, args.baud)
        if not path:
            sys.exit(1)
    elif args.file:
        path = args.file
    else:
        parser.print_help()
        sys.exit(1)

    stats = analyze_file(path, plot=not args.no_plot)

    if args.json:
        print(json.dumps(stats, indent=2))


if __name__ == '__main__':
    main()
