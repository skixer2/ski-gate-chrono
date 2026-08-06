# ADR-003: Run Storage Architecture

**Status:** SUPERSEDED in part (2026-08-06) by Opt A raw slots  
**Original:** ACCEPTED (2026-07-11), AMENDED LittleFS v1 (2026-07-22)  
**v4.63:** Run **payload** moved off LittleFS to pre-erased raw SPI slots.

## Why the change

S04 (BHY2 real LOGGING rate) measured:

| Path | FPS |
|------|-----|
| LittleFS append on hot path | ~42–47 |
| Raw SPI program, pre-erased (v4.62 spike) | **99.4** |

Design timing budget (`sgc_system_design.md`) assumed raw page program, not LittleFS COW.

Constraints that remain:

1. **No large RAM ring** — Cordio BLE OOM (MEMORY.md).
2. **Do not return to full FlashManager-only** — recovery/list bugs led to LittleFS.
3. **Erases off the descent path** — pre-erase at ARM (stationary); lazy 4 KB erase ahead of write cursor only when needed.

## Architecture (v4.63)

```
┌──────────────────┬────────────────────────────────────┬────────────┐
│ 0x0000–0x5FFF    │ 0x6000–0x1FBFFF                    │ 0x1FC000+  │
│ FlashRing        │ 8 × ~249 KB raw run SLOTS          │ config     │
│ (ARMED pre-roll) │ [RunHeader][frames…][CRC trailer]  │ index@1FD  │
└──────────────────┴────────────────────────────────────┴────────────┘
```

- **prepare_next_run() @ ARMED:** erase first 16 KB of free/oldest slot (~fast).
- **LOGGING:** `program()` only; `ensure_erased()` erases next sector when cursor approaches.
- **Index:** sector `0x1FD000` — RunEntry table + slot mask (RAM cache, persist on close).
- **BLE FT / hex dump:** same byte layout as before (header + payload + CRC).

LittleFS code retained as `.disabled` reference only — not linked.

## Tests

- **S04** `test_bhy2_rate.py`: force-l, expect `store=raw`, fps ≥ 90, `we=0`.
- **S03** stream (when available): natural start-det → LOGGING must also `store=raw`.
- Multi-run: 2× S04 without `-R` between → `runs` increments; 9th overwrites oldest.

## Version

Bump `FW_VERSION` on every storage change. Confirm `ver` in boot / `?`.
