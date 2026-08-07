# ADR-003: Run Storage Architecture

**Status:** SUPERSEDED for run payloads (2026-08-06/07) by Opt A raw slots  
**Original:** ACCEPTED (2026-07-11), AMENDED LittleFS v1 (2026-07-22)  
**Current production:** `RawRunStore` (v4.63 multi-slot, v4.64 full-slot prep)

## Why the change

S04 (BHY2 real LOGGING rate) measured:

| Path | FPS |
|------|-----|
| LittleFS append on hot path | ~42–47 |
| Raw SPI program, pre-erased (v4.62 spike) | **99.4** |

Design timing budget (`sgc_system_design.md`) assumed raw page program, not LittleFS COW.

Constraints that remain:

1. **No large RAM ring** — Cordio BLE OOM (MEMORY.md / v4.60→v4.61).
2. **Do not return to full FlashManager-only** — recovery/list bugs led to LittleFS era.
3. **Erases off the descent path** — full-slot erase during POST_RUN cooldown (10 s) or boot; LOGGING is program-only when prepared.

## Architecture (v4.63 → v4.64)

```
┌──────────────────┬────────────────────────────────────┬────────────────┐
│ 0x0000–0x5FFF    │ 0x6000–0x1FBFFF                    │ 0x1FC000+      │
│ FlashRing        │ 8 × ~249 KB raw run SLOTS          │ config @1FC    │
│ (ARMED pre-roll) │ [RunHeader][frames…][CRC trailer]  │ index  @1FD    │
│ sectors 0–5      │                                    │ reserved 1FE–  │
└──────────────────┴────────────────────────────────────┴────────────────┘
```

| API | When | What |
|-----|------|------|
| `prepare_next_run()` | **POST_RUN** (10 s cooldown) and **boot** | Full-slot erase (`RRS_SLOT_SIZE`, ~60×4 KB). **Not ARM.** |
| `create_run()` | LOGGING entry | Write RunHeader on prepared slot; if none prepared, prepares now (force-l / first run). |
| `append_data()` | LOGGING hot path | `program()` only; `ensure_erased()` is belt-and-suspenders if prep incomplete. |
| `close_run()` | POST_RUN | CRC trailer + index commit (sector `0x1FD000`). |

- **Index:** sector `0x1FD000` — RunEntry table + slot mask (RAM cache, persist on close). Magic `RRS1`.
- **BLE FT / hex dump:** same byte layout as LittleFS era (header + payload + CRC).
- **`run_saved.store`:** always `"raw"`.
- **LittleFS / FlashManager / raw_run_writer spike:** retained as `.disabled` reference only — not linked.

### Why full-slot erase moved to POST_RUN (v4.64)

JP: erase the next run during the 10 s POST_RUN cooldown, not at ARM.

- Athlete is stopped → multi-sector erase cost is free.
- ARM stays fast (no ~60 sector erase before green LED / start window).
- LOGGING becomes pure page program when prep succeeded → S04 ~100 Hz path.

## Tests

- **S04** `system_tests/test_bhy2_rate.py`: force-l after arm, expect `store=raw`, fps ≥ 90, `we=0`, runs increment.
  - With `-R`: clean index; first create may pay erase if prep not yet done.
  - Without `-R` after a completed run: next slot already full-erased in prior POST_RUN.
- **S03** stream (when available): natural start-det → LOGGING must also `store=raw`.
- Multi-run: 2× S04 without `-R` between → `runs` increments; 9th overwrites oldest.

## Pre-roll buffer (v4.75) — linear, not circular

ARMED pre-roll is a **forward-only** flash buffer (not 2/3-region circular):

```
0x0000–0xEFFF   3000 × 20 B slots  (~30 s @ 100 Hz = full ARM_TIMEOUT)
0xF000+         RawRunStore
```

| API | When |
|-----|------|
| `prepare_preroll()` | **enter IDLE** (from ARMED / POST_RUN) + **boot** — full erase |
| `write()` | ARMED only — **program only**, no erase, stop at 3000 |
| `trim_to_newest(1000)` | LOGGING entry (natural) — keep last ~10 s for encode/phone |
| drain pop 2 | LOGGING until empty, then live Opt-A |

S04 force-`l` still skips ring. S05 fill / S06 drain bench the pre-roll path.

## Version

Bump `FW_VERSION` in `src/config.h` on every storage change. Confirm `ver` in boot / `?` / `V`.
Current production baseline tag: **v4.71-best-s04** (rate). Pre-roll geometry: **4.75**.
