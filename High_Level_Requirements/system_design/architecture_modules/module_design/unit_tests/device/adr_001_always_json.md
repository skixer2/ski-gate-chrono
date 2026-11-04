# ADR-001: Always-JSON Serial Output

**Date:** 2026-06-21  
**Status:** Accepted  
**Replaces:** Phase 8 dual-path (#ifdef TEST_MODE / #else)

## Context

Phase 8 introduced JSON-lines output gated behind `-DTEST_MODE`, with
human-readable output in production builds. This created two problems:

1. **Untested production path** — the code that ships to ski racers is
   never exercised during bench testing.
2. **Format divergence** — any change to one output path must be
   mirrored in the other, doubling maintenance.

## Decision

**JSON-lines is the only serial output format. All builds, always.**

The `-DTEST_MODE` flag now gates ONLY:
- Test commands (`T`, `B`, `Q`, `L`, `Z`)
- Sensor value injection (`feed_sensors()` synthetic path)

Serial commands (`?`, `a`, `l`, `p`, `s`, `i`, `f`, `R`) work identically
in every build. State transitions, errors, and events always emit JSON.

## Rationale

### Production is physically sealed
The Nicla Sense ME is inside an IP67 enclosure. USB is inaccessible.
Nobody can see serial output in production. No test commands can
be sent. The test command code in the production binary is dead code
with zero security or performance impact.

### Bench test is worst-case timing
Serial connected at 115200 baud introduces TX blocking (~87 µs/byte).
In production with nothing connected, the UART TX FIFO drains instantly
and `Serial.print()` returns in microseconds. Bench testing is therefore
*harder* than production — if the 100 Hz loop works with serial connected,
it works in the field.

### Single code path = single truth
No `#ifdef TEST_MODE` / `#else` on any output call. The code that logs
`{"ev":"st","from":"ARMED","to":"LOGGING"}` is the same code that ships.

## Consequences

- All firmware debug output uses JSON — even files like `spi_flash.cpp`
  and `file_transfer.cpp` that previously had independent print formats.
- Production binary is ~200 bytes larger (JSON helper functions in flash),
  which is negligible on a 512 KB nRF52832.
- Human-readable serial output is gone — no `STATE:`, no `[FT]`, no `[BLE]`.
  If manual debugging is ever needed, a simple `jq` filter restores readability:
  ```bash
  pio device monitor -b 115200 | jq -r '"\(.ev)\t\(.st // .from // "")"'
  ```

## Rejected Alternatives

### Runtime JSON toggle (`J` command)
Adds complexity. Requires storing a mode flag. Creates the same
"test != production" gap — a unit could ship with wrong mode.
Compile-time decision is cleaner for an embedded device.

### JSON only in test builds
The original Phase 8 approach. Rejected because production output
path is never tested.

### No serial output in production
Would require an additional `#ifdef` gate on all output, creating
the untested-path problem in reverse. The UART TX cost with nothing
connected is negligible, so there's no benefit.
