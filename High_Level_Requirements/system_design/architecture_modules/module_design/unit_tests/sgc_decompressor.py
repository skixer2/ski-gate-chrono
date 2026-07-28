"""
Decompressor for SGC bit-packed frame format.

Decompresses run files from flash back to per-frame sensor data
(same algorithm as the Flutter phone app decompressor.dart).

Format (from bit_packer.cpp):
  RunHeader  16 bytes
  Frames...  variable length
  CRC trailer 6 bytes (magic 0xC3 0x32 + CRC32 LE)

Each frame:
  Header 2 bytes: bits 9-0=delta_ms, 13-10=seq, 15-14=pkt_type
  T1 payload 4 bytes: 8×int4 deltas (total 6B)
  T2 payload 8 bytes: 8×int8 deltas (total 10B)
  T3 payload 16 bytes: 7×int16 full + 1×uint16 baro (total 18B)
  Forced T3 anchor every 100 frames.
"""

import struct
from typing import List, Tuple, Optional


# Frame data decoded from bit-packed format
class DecompressedFrame:
    __slots__ = ('fn', 'p', 'q_w', 'q_x', 'q_y', 'q_z',
                 'la_x', 'la_y', 'la_z', 'delta_ms')
    def __init__(self):
        self.fn = 0
        self.p = 0.0       # pressure in hPa
        self.q_w = 0
        self.q_x = 0
        self.q_y = 0
        self.q_z = 0
        self.la_x = 0
        self.la_y = 0
        self.la_z = 0
        self.delta_ms = 0

    def to_dict(self):
        """Convert to same format as NDJSON for comparison."""
        return {
            'fn': self.fn,
            'p': round(self.p, 2),
            'q': [self.q_w / 16384.0,
                  self.q_x / 16384.0,
                  self.q_y / 16384.0,
                  self.q_z / 16384.0],
            'la': [self.la_x, self.la_y, self.la_z],
        }


def sign_extend_4bit(val: int) -> int:
    """Sign-extend a 4-bit value (0-15) to Python int."""
    if val & 0x08:
        return val - 16
    return val


def sign_extend_8bit(val: int) -> int:
    """Sign-extend an 8-bit value (0-255) to Python int."""
    if val & 0x80:
        return val - 256
    return val


def decompress_run(raw_bytes: bytes) -> List[DecompressedFrame]:
    """
    Decompress bit-packed run data into list of DecompressedFrame.

    Args:
        raw_bytes: The raw file bytes including RunHeader and CRC trailer.

    Returns:
        List of decompressed frames.
    """
    RUN_HEADER_SIZE = 16
    CRC_TRAILER_SIZE = 6
    ANCHOR_EVERY = 100

    if len(raw_bytes) < RUN_HEADER_SIZE + CRC_TRAILER_SIZE + 18:
        return []  # too short for even one T3 frame

    # Read header
    hdr = raw_bytes[:RUN_HEADER_SIZE]
    format_ver = hdr[0]
    arm_side = hdr[1]
    ts_utc = struct.unpack_from('<I', hdr, 2)[0]
    baro_temp = struct.unpack_from('<h', hdr, 6)[0]
    data_size = struct.unpack_from('<I', hdr, 8)[0]
    frame_count_hdr = struct.unpack_from('<H', hdr, 12)[0]
    cal_accuracy = hdr[14]

    # Data region is between header and CRC trailer
    data = raw_bytes[RUN_HEADER_SIZE:len(raw_bytes) - CRC_TRAILER_SIZE]
    # For format_ver>=2, data_size in header is 0; use actual data length
    actual_data_sz = len(data)

    frames: List[DecompressedFrame] = []
    pos = 0
    fn = 0

    # State for delta accumulation
    q_w, q_x, q_y, q_z = 0, 0, 0, 0
    la_x, la_y, la_z = 0, 0, 0
    baro_pa_div2 = 0

    while pos + 2 <= actual_data_sz:
        # Read 2-byte header
        h = data[pos] | (data[pos + 1] << 8)
        pkt_type = (h >> 14) & 0x03
        # seq = (h >> 10) & 0x0F
        delta_ms = h & 0x3FF

        if pkt_type == 0:  # T1: 6 bytes
            if pos + 6 > actual_data_sz:
                break
            # 4-byte payload: 8 × int4 nibbles
            nibbles = []
            for i in range(4):
                b = data[pos + 2 + i]
                nibbles.append((b >> 4) & 0x0F)
                nibbles.append(b & 0x0F)
            d_q_w = sign_extend_4bit(nibbles[0])
            d_q_x = sign_extend_4bit(nibbles[1])
            d_q_y = sign_extend_4bit(nibbles[2])
            d_q_z = sign_extend_4bit(nibbles[3])
            d_la_x = sign_extend_4bit(nibbles[4])
            d_la_y = sign_extend_4bit(nibbles[5])
            d_la_z = sign_extend_4bit(nibbles[6])
            d_baro = sign_extend_4bit(nibbles[7])
            q_w += d_q_w
            q_x += d_q_x
            q_y += d_q_y
            q_z += d_q_z
            la_x += d_la_x
            la_y += d_la_y
            la_z += d_la_z
            baro_pa_div2 += d_baro
            pos += 6

        elif pkt_type == 1:  # T2: 10 bytes
            if pos + 10 > actual_data_sz:
                break
            d_q_w = sign_extend_8bit(data[pos + 2])
            d_q_x = sign_extend_8bit(data[pos + 3])
            d_q_y = sign_extend_8bit(data[pos + 4])
            d_q_z = sign_extend_8bit(data[pos + 5])
            d_la_x = sign_extend_8bit(data[pos + 6])
            d_la_y = sign_extend_8bit(data[pos + 7])
            d_la_z = sign_extend_8bit(data[pos + 8])
            d_baro = sign_extend_8bit(data[pos + 9])
            q_w += d_q_w
            q_x += d_q_x
            q_y += d_q_y
            q_z += d_q_z
            la_x += d_la_x
            la_y += d_la_y
            la_z += d_la_z
            baro_pa_div2 += d_baro
            pos += 10

        elif pkt_type == 2:  # T3: 18 bytes
            if pos + 18 > actual_data_sz:
                break
            q_w = struct.unpack_from('<h', data, pos + 2)[0]
            q_x = struct.unpack_from('<h', data, pos + 4)[0]
            q_y = struct.unpack_from('<h', data, pos + 6)[0]
            q_z = struct.unpack_from('<h', data, pos + 8)[0]
            la_x = struct.unpack_from('<h', data, pos + 10)[0]
            la_y = struct.unpack_from('<h', data, pos + 12)[0]
            la_z = struct.unpack_from('<h', data, pos + 14)[0]
            baro_pa_div2 = struct.unpack_from('<H', data, pos + 16)[0]
            pos += 18

        else:
            break  # invalid type

        f = DecompressedFrame()
        f.fn = fn
        f.q_w = q_w
        f.q_x = q_x
        f.q_y = q_y
        f.q_z = q_z
        f.la_x = la_x
        f.la_y = la_y
        f.la_z = la_z
        f.p = baro_pa_div2 * 2.0 / 100.0  # Pa/2 → Pa → hPa
        f.delta_ms = delta_ms
        frames.append(f)
        fn += 1

    return frames


def decompress_from_hex(hex_str: str) -> List[DecompressedFrame]:
    """Decompress from a hex string (as returned by 'h <id> raw')."""
    raw = bytes.fromhex(hex_str)
    return decompress_run(raw)


def compare_to_ndjson(frames: List[DecompressedFrame],
                      ndjson_path: str) -> Tuple[int, int, int, List[str]]:
    """
    Compare decompressed frames to original NDJSON.

    Returns:
        (matched, mismatched, missing, errors_list)
    """
    import json

    with open(ndjson_path) as f:
        ndjson_data = [json.loads(line) for line in f if line.strip()]

    errors = []
    matched = 0
    mismatched = 0
    missing = 0

    # Build lookup by frame number
    nd_by_fn = {d['fn']: d for d in ndjson_data}

    for i, frame in enumerate(frames):
        nd = nd_by_fn.get(frame.fn)
        if nd is None:
            missing += 1
            continue

        frame_dict = frame.to_dict()

        # Compare pressure (within ±0.04 hPa for Pa/2 quantization: 2 Pa/LSB)
        p_diff = abs(frame_dict['p'] - nd['p'])
        if p_diff > 0.04:
            errors.append(
                f"  Frame {frame.fn}: pressure mismatch "
                f"(decompressed={frame_dict['p']:.2f}, ndjson={nd['p']:.2f}, diff={p_diff:.3f})"
            )
            mismatched += 1
            if len(errors) >= 20:
                break
            continue

        # Compare quaternion (within ±1 LSB = 1/16384 ≈ 0.00006)
        for j, comp in enumerate(['q_w', 'q_x', 'q_y', 'q_z']):
            nd_q = [nd['q'][0] * 16384, nd['q'][1] * 16384,
                    nd['q'][2] * 16384, nd['q'][3] * 16384]
            dec_q = [frame.q_w, frame.q_x, frame.q_y, frame.q_z]
            if abs(dec_q[j] - nd_q[j]) > 2:
                errors.append(
                    f"  Frame {frame.fn}: quaternion[{j}] mismatch "
                    f"(dec={dec_q[j]}, nd={nd_q[j]:.1f})"
                )
                mismatched += 1
                if len(errors) >= 20:
                    break
                break
        else:
            # Compare linear acceleration (within ±1 LSB)
            nd_la = nd['la']
            dec_la = [frame.la_x, frame.la_y, frame.la_z]
            for j in range(3):
                if abs(dec_la[j] - nd_la[j]) > 1:
                    errors.append(
                        f"  Frame {frame.fn}: la[{j}] mismatch "
                        f"(dec={dec_la[j]}, nd={nd_la[j]})"
                    )
                    mismatched += 1
                    if len(errors) >= 20:
                        break
                    break
            else:
                matched += 1

        if len(errors) >= 20:
            break

    return matched, mismatched, missing, errors
