# Phase B — Sensor Producer (requirements, v1 draft for JP review)

**App:** 03_pull_proto (Zephyr 4.4, NCS 3.4.1) · **Base:** v0.11.14 (BHI260 Phase A resolved)
**Rule:** this doc + ledger entry BEFORE any code. JP-paced, one step at a time.

## Goal

Replace the recorder's synthetic 8 B placeholder frames with **real BHI260
virtual-sensor data**, encoded **byte-identically to ArduinoFW 5.x**, so the
existing phone pull pipeline and CRC habits work unchanged on Zephyr output.

## Source of truth (ArduinoFW, read from code today — not memory)

| Item | Value | Where |
|---|---|---|
| Sensors | `SENSOR_ID_RV`, `SENSOR_ID_LACC`, baro virtual, temp | main.cpp:152–155 |
| Rates | RV/LACC/baro `.begin(100.0f, 0)`, temp `.begin(1.0f, 0)` | main.cpp:1346–1349 |
| Q14 | `q_* = (int16_t)(f * 16384.0f)` | main.cpp:1016–1019 |
| LACC | int16 mm/s², Arduino_BHY2 scale 1.0, no setRange() | main.cpp:1340–1345 |
| Baro | `baro_pa_div2 = (uint16_t)(hPa * 50.0f)` → Pa/2, 2 Pa/LSB | main.cpp:1023 |
| Temp | header-only `×10 int16`, never per-frame | main.cpp:906 |
| Packets | T1=6 B (int4 baro Δ), T2=10 B (int8 Δ), T3=18 B full, header 2 B, ANCHOR_EVERY 50 | bit_packer.h |
| Avg target | ~8.0 B/frame | bit_packer.h |
| Feed cadence | 10 ms tick, values read AFTER `BHY2.update()` | config + feed_sensors |

## Zephyr state today (v0.11.14)

- Hub boots from flash (`boot_status 0x13`) via vendored bhy2 API + shim
  (manual plain-polarity CS, 8 MHz). Vendored `bhy2.c`/`bhy2_parse.c` =
  full Bosch API incl. FIFO — **no new vendor code needed**.
- `recorder.c`: 100 Hz tick (k_timer→dedicated workqueue), REAL producer
  (B3 PASS 2026-10-03). Pre-roll ring = spec v4-final (recorder.c header):
  20 s pre-trigger + 20 s post-trigger banks, erase-on-IDLE invariant.
- `store`: fake blobs, layout ArduinoFW-verbatim; flash store = later step.

## Steps (each = one bench session, gate before next)

**B1 — configure + drain (serial proof).**
`bhy2_set_virt_sensor_cfg` RV/LACC/baro @100 Hz, temp @1 Hz after
boot_from_flash; drain FIFO in the existing 10 ms tick; parse to per-sensor
frames. Serial JSON 1 Hz summary: per-sensor frame rate, parse status,
FIFO overflow count.
*Pass:* ≥95% of 100 f/s per 100 Hz sensor over 60 s, zero overflows,
`boot_status` stays 0x13.

**B2 — RawFrame + BitPacker C port (golden vectors).**
Port `bit_packer` C++→C, keep output **byte-identical**. Golden vectors =
real ArduinoFW encodes captured on the bench (bench_bhy_test, pio), incl.
forced T3 anchors and Δ-boundary cases (int4/int8 saturation).
*Pass:* 1000+ vectors byte-identical (ztest on-target or host-side).

**B3 — producer swap (recorder integration).**
Synthetic → real: tick reads latest parsed values → RawFrame → BitPacker →
store append path. *Pass:* 60 s ARM+LOG ≥ 99.9% ticks, avg ≤8.5 B/frame,
CRC-intact blob via fake store; then store_flash becomes the next arc.

## Decisions (JP, 2026-10-02 11:31 UTC — recorded)

1. **FIFO: POLL in the 10 ms tick** (ArduinoFW parity). P0.14 INT =
   documented fallback only if B1 rate gates fail.
2. **Golden vectors = yes**: (input frame bytes → packed output bytes)
   pairs captured from ArduinoFW BitPacker on the bench; Zephyr C port
   must reproduce them byte-identically.
3. **Temp: header-only ×10** (0.1 °C/LSB, int16, at LOGGING start) —
   confirmed. **Arrival temp** (wax/ski inference, JP idea): emit in
   serial/BLE status metadata at run close for now; stored-in-blob
   parked for header v3 (Zephyr-era app rebuild) — blob layout frozen
   by current app parser.
4. RESOLVED by design (JP 2026-10-05): no phone connection from arming
   to run end (v0.10.3 ARM tears BLE down). B3 gate = full-cycle bench:
   arm→run→end→topup→phone download→re-arm.

## Ledger

TC-2026-10-02-002 (Phase B, sensor producer) — opened in TEST_LEDGER.md.
