
---

# V2 ADDENDUM (2026-09-10, post-bench + research) — STREAMING PULL

**Trigger:** JP verdict 12:57 (speed regression, settle harmful) + BLE research
(`BLE_TRANSFER_RESEARCH.md`). Phone bench in ~2 days — docs first, then code.

## Why the old push was slow (answer to JP's question, 15:43)

Old push WAS a one-way stream but had three self-inflicted brakes:
1. **Manual cadence 30–60 ms** → 4–8 KB/s by construction (queue-pressure
   theory, now disproven by 5.76 bench: wedges strike with device idle-paced).
2. **One notification in flight** per loop tick; reference impls queue 4–10
   packets per connection event.
3. **DLE/2M-PHY never verified** — if DLE off, 244 B notifications fragment
   into ~10×27 B LL packets → LL ceiling ≈ 7–10 KB/s, matching our best-ever
   push numbers. 50–92 KB/s references assume DLE + full queue.

## V2 design: streaming pull (FW 5.77 + App 1.44)

Control stays pull (JP's model); data path becomes the consensus stream:

- **New command `s <id> <off> <len>`** (len = whole remaining run, e.g. 39372).
  Device replies with BINARY notification frames, back-to-back, paced by
  queue capacity not timers: up to `STREAM_FRAMES_PER_LOOP` (2) per main-loop
  pass, no inter-frame delay.
- **Frame = [seq:2B][len:1B][payload:≤240 B]** (MTU 247 − 3 ATT − 4 hdr + 1
  spare). Final frame: seq=0xFFFF, len=4, payload=CRC32(LE) of whole blob.
- **NO resume strategy (JP, 15:51):** at 2 s per run, the wedge probability
  per attempt is ~8× lower than at 16 s. On ANY failure → reconnect →
  `s <id> 0 <total>` from scratch. No partial buffers, no offset tracking,
  no resume protocol. Restart cost = transfer cost (2 s). `r` command kept
  as debug/ASCII path.
- **Watchdog:** every emitted frame feeds pull_wdt (liveness = TX progress;
  if the link wedges, writeValue stalls → no frames → 2 s → radio restart).
- **Echo kill:** app silently drops value-changed events matching its own
  last request; "unexpected line" no longer surfaces.
- **Settle: 12 s → 4 s** (measured no protective effect, pure UX cost).
- **PHY: request LE 2M** via Nordic `.usePreferredPhy(2M)`; log onPhyUpdate.
- **DLE verification:** bench-day HCI snoop (LL_LENGTH_REQ/RSP) — if Cordio
  doesn't negotiate DLE, investigate ArduinoBLE/Cordio config (SGC-patched
  vendored lib — we control it).
- Target: 39 KB clean ≤ 2–3 s; wedge cost unchanged ~30 s; zero visible errors.

## Verification (bench day, 2 days out)

1. Power-cycle device after flash (DAP-wedge rule), install app, n≥5 runs.
2. HCI snoop: DLE + PHY + packets-per-event → real ceiling diagnosis.
3. KPI: clean run ≤ 3 s; interrupted run ≤ 45 s; no "unexpected line" in UI.
