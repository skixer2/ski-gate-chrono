# SGL Storage Design — Zephyr Port (M2)

*2026-09-29 — the V-model landing for the Zephyr-era run store.
Canonical deep design: `store_layout.h` (policy essay) + `store_flash.c`
header in `implementation/Firmware_Zephyr/apps/03_pull_proto/src/`.
Case history with evidence: `unit_tests/test_ledger/TEST_LEDGER.md`,
2026-09-24..29. This document is the architecture-level reference.*

## 1. What replaced what

| | ArduinoFW (Opt-A, reference) | Zephyr port (SGL2/SGL3) |
|---|---|---|
| Layout | 8 fixed slots (244 KiB each) + linear pre-roll | Packed circular log, sector-padded runs, age-ordered index |
| Erase | whole next slot at POST_RUN/boot | top-up to a **budget-sized runway** after each run + boot/wipe |
| Length knowledge | n/a (slots) | **unknown at start by design** (F62) — runway guarantees a max run always fits |
| Capacity | 8 runs fixed | ~(LOG_SIZE − target)/target full runs (20 SL / 11 DH / 8 max-budget on 2 MB) |
| Index | RRS1 slot records | SGL3: two-frontier {hi, lo} + run_budget, magic-bumped per format change |

## 2. The runway policy (the core idea)

Run length is unknown at record start (an athlete's descent). So erase work
is never sized for the incoming record. Instead, right AFTER a run (and at
boot, and after wipe), the store tops up coverage until:

```
U(cursor) = [cursor, cursor+T)              when it fits before LOG_END
          = [cursor, LOG_END) ∪ [LOG_START, LOG_START+T)   near the end
```

where `T = sector_pad(active_budget + 22 B overhead)`. The two-segment form
is REQUIRED: a wrapping max-run needs T bytes clean at the bottom — a plain
ring interval is provably short there (counterexample in store_layout.h).
Two persisted frontiers track coverage: `erased_hi` (from cursor) and
`erased_lo` (bottom reserve). **No ring arithmetic anywhere** — the SGL1
bug class (inverted wrap ranges → programming unerased flash) is
structurally extinct; absolute math only.

Discipline budgets (F62): SL 90 KB / GS+SG 120 KB / DH 150 KB (default) /
Custom 60–600 KB at the documented 1 KB/s ceiling (100 Hz, ≤10 B/frame,
v2.4 delta-baro packing avg 8.0 B/frame). Device stores BYTES; the
seconds→bytes mapping lives app-side. `b <bytes>` sets (persisted in the
index; re-tops the runway when idle).

## 3. Record lifecycle

```
store_record_start(len)   NO erase, NO reclaim. Coverage guard (defense:
                           loud fail if the invariant broke).
IDLE → WRITING            16 B header + payload chunks + running CRC
     → CLOSING            trailer (C3 32 + CRC32), index entry, persist
     → TOPUP              erase to U(cursor); victims = live entries
                           intersecting the sweep (reclamation is
                           indistinguishable from runway prep)
IDLE
```
Boot/wipe enter TOPUP directly. Crash windows documented in store_flash.c:
(a) mid-TOPUP → loud CRC failures at pull, catalog intact; (b) close→persist
gap → boot re-erases the delta (idempotent); (c) mid-record → boot
`verify_runway()` sniffs non-0xFF at the claimed runway start and resets
the frontier (self-heal).

## 4. Wire compatibility (frozen, DO NOT INVENT)

Blob layout ArduinoFW-verbatim: 16 B RunHeader (v2) + payload + 6 B trailer
(C3 32 + CRC32 LE over payload only). Listing 'D' line set: `{"run":...}`
per run + pure `{"d_end":N}` terminator — **exact-string contract**: the
V2 native downloader string-matches d_end; merged fields caused the
directory-timeout loop (ledger 2026-09-28). `{"free":N}` (F63 capacity
estimate) ships on its own serial-only line.

## 5. Proof stack

- **Host**: `tests/test_layout.c` — runway-plan unit tests + bitmap
  lifecycle simulation mirroring the decision layer; every programmed byte
  must land in an erased sector; full sweep at all four discipline budgets
  (712 records each). This is the test whose absence shipped the SGL1 wrap
  bug.
- **Hardware**: 8× max-budget runs → two-segment top-up (111 sectors) with
  exact reclaims → clean wrap through the bottom reserve → post-wrap
  readback header/payload/CRC match (0x28D0D0AC). Boot top-up + zero-work
  fast path. Setter/re-top/`{"free"}` verified live.
- **End-to-end**: App 1.47/1.48 downloads (idle+dynamic watchdogs),
  90 KB cycle 9.8 s incl. connect.

## 6. Geometry & growth

2 MB stock Nicla: log [0x14000, 0x1FC000), index @0x1FD000, self-test
@0x1FE000. Custom board (MX25R6435F, 8 MB, pin-compatible): move LOG_END +
DTS constants; raise MAX_INDEXED_RUNS toward the 254-entry sector geometry
(needs nRF52833's 128 KB RAM for the bigger listing buffers). 600 s custom
budget on 8 MB = 6% standing reserve (vs 24% on 2 MB).

## 7. Open / parked (with facts)

- Dynamic wrap (record straddling LOG_END): rejected 2026-09-28 (read path
  loses single-linear-read invariant; two-range extents everywhere). Note:
  8×244 KiB tiles the 2 MB log EXACTLY — if ever revisited, the runway
  policy already supports it.
- Unpaced queue-push throughput: effective ceiling today is per-CI notify
  dispatch (~9.9 KB/s at any STREAM_GAP_MS).
- Production streaming producer (M3): starts unknown-length, feeds the same
  append path at 100 Hz — this design is its landing zone.
