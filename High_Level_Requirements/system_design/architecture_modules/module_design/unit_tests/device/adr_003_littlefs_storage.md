# ADR-003: Run Storage + Pre-roll Architecture

**Status:** Opt-A run payloads + linear pre-roll (2026-08-07)  
**Original:** ACCEPTED LittleFS (2026-07-11), v1 amend (2026-07-22)  
**Current baseline tag:** **`v4.78-best-preroll`** (FW 4.78)

## Run payloads — Opt A RawRunStore

S04 measured:

| Path | FPS |
|------|-----|
| LittleFS append on hot path | ~42–47 |
| Raw SPI program, pre-erased | **~99–100** |

| API | When |
|-----|------|
| `prepare_next_run()` | POST_RUN cooldown + boot — full-slot erase (**not** ARM) |
| `create_run` / `append_data` / `close_run` | LOGGING — program-only when prepared |
| `run_saved.store` | always `"raw"` |

Constraints: no large RAM ring (Cordio OOM); erases off descent path.

## Pre-roll — linear buffer (v4.75–v4.78)

**Not** circular 2-half / 3×500. Forward-only:

```
0x0000–0x13FFF   4000 × 20 B RingEntry
                 3000 = ARM fill cap (~30 s @ 100 Hz)
                 +1000 = drain headroom (live while pop2+push1)
0x14000+         RawRunStore (8 slots)
0x1FC000 / 1FD000 config / index RRS1
```

| API | When |
|-----|------|
| `prepare_preroll()` | **enter IDLE** (from ARMED or POST_RUN) + **boot** |
| `write(f, arm_limit=true)` | ARMED — program only, stop at 3000 |
| `trim_to_newest(1000)` | LOGGING entry (natural / `L`) |
| drain | pop 2 encode; if still non-empty push 1 live; else encode live |
| force `l` (S04) | clear ring; live encode only |

### Why linear 3000 + 1000

- Full `ARM_TIMEOUT` (30 s) without wrap/erase during fill → stable ~100 Hz ARMED (S05).
- Phone/run only need ~10 s before start → `PREROLL_KEEP=1000`.
- Drain pop2+push1 needs free slots after head@3000 → +1000 headroom.
- Last frame: pop1 **without** push-back (else r=1 deadlock forever) — fixed v4.78.

### Proven numbers (v4.78, JP bench)

| Test | Result |
|------|--------|
| S04 | **99.2–99.5 fps**, store=raw, we=0 |
| S05 target 1000 | **~109 fps** fill |
| S05 toward 3000 | **~99 fps**, peak ~2850 @ ARM 30 s timeout |
| S06 | drain **~10.8 s** for keep=1000; fr~2356; no hang |

## Anti-patterns (regressions)

1. `?` → `metadata_sync`/`persist_index` during LOGGING  
2. Using `T` as tm query (it toggles)  
3. test_mode handling `L` when tm=0 (steals drain cmd)  
4. Circular MAX_COUNT=1000 on 2×500 physical (erase kills live history)  
5. Drain pop2 without push live **and** without headroom  
6. Drain pop1+push1 when count==1 (deadlock)

## Tests

| ID | Script | Role |
|----|--------|------|
| S04 | `system_tests/test_bhy2_rate.py` | Live encode rate |
| S05 | `system_tests/test_ring_fill.py` | ARMED fill |
| S06 | `system_tests/test_ring_drain.py` | Drain path |
| S03 | `system_tests/test_stream_run.py` | Stream integrity (not Hz) |
| Suite | `system_tests/run_device_suite.py` | S04+S05+S06 |

See `system_tests/device.md`.

## Version

Bump `FW_VERSION` in `src/config.h` on every storage/pre-roll change.  
**Current best tag: `v4.78-best-preroll`.**
