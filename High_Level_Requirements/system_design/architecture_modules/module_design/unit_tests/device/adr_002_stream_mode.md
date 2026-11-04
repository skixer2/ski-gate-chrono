# ADR-002: Binary Stream Mode for Sensor Injection Testing

**Date:** 2026-07-08  
**Status:** Accepted  
**Depends on:** ADR-001 (Always-JSON)

## Context

ADR-001 established that JSON-lines is the only serial output format and
test commands (T, B, Q, L, Z) are always compiled in. These commands set
**static** injected sensor values — the device re-reads the same value
every 10 ms indefinitely.

This enables basic sensor injection tests (U16–U19) but cannot test the
**full temporal pipeline**: ring buffer fill, start detection from a
baro ramp, gate impact timing, end detection from baro flatline, or
flash compression over thousands of frames.

To test the real device end-to-end, we need time-series sensor data
injected at 100 Hz over serial, replacing the I²C sensor reads with
a stream of synthetic frames.

## Decision

**Add a binary stream mode to test_mode, toggled by the `S` serial command.**

The firmware reads binary frames from the serial buffer in a non-blocking
loop. Each frame atomically updates all 8 injected sensor values (pressure
+ quaternion WXYZ + linear acceleration XYZ). The existing `feed_sensors()`
code path is unchanged — it reads `test_get_*()` regardless of whether
the source is serial streaming or manual B/Q/L commands.

### Protocol

```
Binary frame (38 bytes total, little-endian):
  [0xAA 0x55]                      ← sync word (2 bytes)
  [uint32:frame_num]               ← (4 bytes)
  [float32:pressure_hPa]           ← (4 bytes)
  [float32:qw] [float32:qx]        ← quaternion (16 bytes)
  [float32:qy] [float32:qz]
  [float32:lax] [float32:lay]      ← linear acceleration (12 bytes)
  [float32:laz]
  Total: 2 + 4 + 4 + 16 + 12 = 38 bytes

End-of-stream marker: [0xBB]
```

### Bandwidth

- 38 bytes × 100 Hz = 3,800 B/s
- 115,200 baud = 11,520 B/s usable
- **33% utilization** — comfortable margin

### Sync recovery

If the 2-byte sync word `0xAA 0x55` is lost, the parser scans forward
byte-by-byte until it finds the next aligned `0xAA 0x55` pair. The
2-byte pattern avoids false positives from `0xAA` appearing naturally
in float32 payload data (~1/256 per byte). Lost frames are counted and
reported in the `{"ev":"stream_end"}` event.

### Non-blocking guarantee

`test_mode_stream_poll()` only reads when ≥ 2 + STREAM_FRAME_PAYLOAD
bytes are available in the Serial buffer. If insufficient data, it
returns immediately without blocking. The sensor loop is never delayed
by serial I/O.

### Exit

Stream mode exits on receiving `0xBB` or on toggling test mode off (`T`).

## Rationale

### Binary, not text

A text protocol (`"F 1234 79500.0 0.707 0 0.707 0 1000 0 -9810\n"`)
would be ~60 bytes per frame → 6,000 B/s → 52% of bandwidth.
Binary at 33% gives more headroom for firmware processing jitter.

### Same code path as real sensors

`feed_sensors()` calls `test_get_pressure()` / `test_get_quat_w()` /
`test_get_lax()` etc. regardless of injection mode. The state machine,
ring buffer, flash compression, and detectors run identically whether
data comes from I²C sensors or serial stream.

### Offline replay

Binary frames can be recorded to and replayed from NDJSON files:

```json
{"fn":0,"p":79725.0,"q":[0.985,0.0,0.174,0.0],"la":[0.0,0.0,-9810.0]}
```

This enables:
- Reproducible deterministic tests (same seed = same frames)
- Replay of real captured data once available
- Regression testing: replay saved runs against new firmware builds

### No production impact

Stream mode requires test mode (`T`) to be active first — a two-step
activation that can never happen accidentally. In production (IP67
sealed, USB inaccessible), neither command can be sent. The stream
parser code (~200 bytes of flash) is dead code in the field.

## Consequences

- **New serial command `S`** toggles stream mode on/off (valid only
  when test mode is active)
- **`test_mode_stream_poll()`** is called from `handle_serial()` before
  per-char command processing, routing all serial bytes into the frame
  parser when stream mode is active
- **Flash cost:** ~200 bytes for binary frame parser + sync recovery
- **RAM cost:** 37-byte static buffer for frame unpacking
- **Test coverage:** the full pipeline (arm → ring fill → start detect →
  log to flash → end detect → run save) can now be tested on real
  hardware with controlled, reproducible data

## Rejected Alternatives

### Text protocol
As noted above, 52% bandwidth utilization leaves less headroom. Binary
is more efficient and the parsing code is simpler (fixed-size memcpy
vs. variable-length float parsing).

### Pre-load to flash and replay
Would require reserving flash sectors for test data, complex to manage.
Serial streaming is simpler and doesn't consume production flash space.

### BLE streaming
BLE throughput (~10 KB/s theoretical, ~3 KB/s practical) is too low
for 100 Hz streaming. Latency and connection stability are additional
concerns. USB serial is reliable and already available for bench testing.

### Separate test binary
Violates ADR-001 principle: never test code you don't ship. The stream
parser must be in the production binary to be tested.
