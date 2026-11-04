# PULL TRANSFER REDESIGN — Requirements (FW 5.76 + App 1.43)

**Status:** REQUIREMENTS — awaiting JP go before implementation
**Author:** ZioClaw (Lead Systems Coordinator) · **Date:** 2026-09-10
**Supersedes:** none · **Related:** TC-2026-08-26-001 (S22 wedge case), FW 5.75/App 1.42 baseline

---

## 0. Decision record (2026-09-09 session, 15:17–19:43 UTC — recovered from archived transcript)

| Time (UTC) | Who | Decision / input |
|---|---|---|
| 18:47 | **JP** | Proposal: model the download on the old SGC (github.com/skixer2/SGC_06): phone connects → asks for available data → asks record by record → loop. Keep transactions lean. |
| 18:49 | ZC | SGC_06 analysis: phone enables notify on characteristic, `read()` → device notifies exactly one record; phone loops; phone does the math. Old transfer = tens of bytes/run. |
| 19:03 | **JP** | Provides firmware reference: github.com/skixer2/SGC_SensiBLE2_1. |
| 19:04 | ZC | SensiBLE2.1 analysis: `{num,type,time}` records served newest-first from NVM; read→notify→index++. ⚠️ ZC's same-message plan (port pole-tap detector, timestamps-only over BLE) — **SUPERSEDED**. |
| 19:08 | **JP** | **CONSTRAINT: "I don't want to alter the structure of the new (Nicla) system!!!"** Only the download procedure changes (call → receive → call). Analysis stays phone-side. Two strategies offered: a) one characteristic per data type; b) single standard 500+ byte text characteristic (**serial replacement**). |
| 19:09 | ZC | Recommends **b**; drafts protocol (stateless requests `(run_id, offset, len)`, text-hex framing, ~7–12 s per 39 KB run, watchdog "no request 20 s → disconnect → re-ADV"). |
| **19:14** | **JP** | **DECISION LOCKED: strategy b. Watchdog = 2 s, not 20 s ("the loop should be much faster than that").** |
| 19:14:32 | ZC | Locked FW 5.76 + App 1.43 scope (below). Announced build start. |
| 19:40–19:43 | ZC | Recon only (FW tree mapped, ArduinoBLE found vendored+patched, warmup build kicked off, zombie-ordering issue found at main.cpp:1484). **Session died 19:43:51 — NO pull-protocol code was written** (verified: no commits, no stash, no branch, clean tree at `e2da657`). |

App 1.42 (restart-from-scratch retry, `e2da657`) was completed, committed and installed at 17:14 — that work survived. The pull redesign does **not** exist in code on either side.

**Process rule (JP, 2026-09-10, binding from now on):** every decision is documented in the test ledger + requirements doc BEFORE implementation. Requirements first (all levels), then code.

---

## 1. Product requirements (PR)

- **PR-1** Run data reaches the phone with zero manual recovery actions despite stochastic S22 link failures. Worst-case interruption = watchdog (2 s) + reconnect (~2–5 s). No device reboots, no reset button, no phone restarts.
- **PR-2** The transfer is a **serial replacement**: a single text characteristic, human-readable frames, debuggable by eye — like the USB console.
- **PR-3** **Nothing else changes in the Nicla system**: raw run recording, storage format, FW state machine, phone-side analysis all stay exactly as today.

## 2. System requirements (SR)

- **SR-1 Phone-paced pull.** Every byte the device sends over BLE is the response to an explicit phone request. No unsolicited push during transfer.
- **SR-2 Stateless server.** Each request is self-describing: `(run_id, offset, len)`. No session state, no resume protocol, no tx_blocked machinery in the pull path. Any disconnect → reconnect → next request continues from the last received byte.
- **SR-3 Liveness watchdog.** While connected: no incoming request for **2 s** → device disconnects → re-advertises. Timer resets on ANY incoming request. (Stateless design makes false positives cheap. Optional future: 1-byte ping request for deliberate long pauses — NOT in v1.)
- **SR-4 Advertising hygiene.** Unconditional re-ADV on every disconnect path; `adv` field added to status JSON.
- **SR-5 One protocol, two transports.** The same text commands work over USB serial and over the BLE console characteristic (parser stays in the serial path).

## 3. Interface requirements (IR) — wire protocol v1

- **IR-1** One bidirectional BLE characteristic (the *console characteristic*; UUID decided at implementation — reuse of FT request char ABCA or new; freeze in the code review).
- **IR-2** Phone → device: ASCII text request (grammar modeled on existing serial commands; `h <id>` hexdump is the pattern). A read request carries `run_id`, `offset`, `len`.
- **IR-3** Device → phone: text frames `[seq len data]`, payload hex-encoded, ≤ ~244 B per notification (ATT MTU 247 cap). A logical chunk of **≥ 500 B per request** arrives as 2 notifications. App requests MTU 517 at connect; if the stack still caps at 247, 2-notification chunks are the accepted fallback (round trips still halved vs 244 B/request).
- **IR-4** End-of-run marker + CRC32 of the full run payload (existing CRC semantics kept — phone verifies the same byte blob the decompressor already eats).
- **IR-5** Available-data listing request (run ids + sizes + counts) answered from the existing run index.

## 4. Firmware requirements — FW 5.76 (FWR)

- **FWR-1** Pull-read commands added to the existing serial parser (text protocol; same handler serves USB and BLE console characteristic).
- **FWR-2** Console characteristic wired to that parser: write = request line, notify = reply frames.
- **FWR-3** 2 s request watchdog (SR-3): reset on any incoming request; on expiry → `BLE.disconnect()` → re-ADV; serial event JSON `{"ev":"pull_wdt"}` for forensics.
- **FWR-4** Unconditional re-ADV on all disconnect paths (SR-4); `adv:true/false` in `?` status JSON.
- **FWR-5** Push FT engine (`file_transfer.cpp` device-push path) stays in the tree behind a disabled flag — USB/lab path only; removed from the BLE product path.
- **FWR-6** `FW_VERSION` = 5.76; forensics preserved (fcp, rr_req, GPREGRET).
- **FWR-7** Regression: builds clean within RAM/flash budget; S03/S04/S05/S06 suites green (storage path untouched).

## 5. App requirements — App 1.43 (APR)

- **APR-1** New lean pull engine (request → receive → assemble → verify CRC) replaces `NordicBleDownloader` in the product path. Output = the same run byte blob the existing decompressor/analysis consumes.
- **APR-2** Recovery = restart-from-scratch philosophy of 1.42, simplified by statelessness: on any failure/timeout → teardown → wait for ADV → reconnect → continue from last received offset.
- **APR-3** Batch behavior: auto-download missing runs; keep per-run try/catch + immediate save (1.36 semantics).
- **APR-4** Progress UI + a serial-style raw frame log (debuggability, PR-2).
- **APR-5** Version 1.43; foreground service + wakelock behavior from 1.35/1.40 kept.

## 6. Verification (bench plan)

1. **Functional:** 5 stored runs → connect → all synced, displayed, CRC-verified. Target ≤ 12 s per 39 KB run, ≤ 60 s full batch.
2. **Wedge resilience (the point of the redesign):** mid-transfer screen-off/screen-on, app backgrounding, deliberate interference — expect: ≤ 2 s `pull_wdt` → re-ADV → app reconnect → transfer continues from offset; no reboot (no `rr:` events), no manual action. n ≥ 5 with induced interference.
3. **USB parity:** same commands over serial give the same data (SR-5).
4. **Regression:** S03 integrity 100 %, S04 rate, smoke/core suites green.

## 7. Out of scope (explicit)

- No pole-tap/gate-detector port from SGC_06 (ZC 19:04 proposal — superseded by JP 19:08).
- No change to raw run recording or storage format.
- No timestamps-only-over-BLE mode; raw runs remain the transfer payload.

## 8. References

- `github.com/skixer2/SGC_06` — old app: `SGC_TextualViewModel.kt`, read()→notify one-record pull, phone-side rollover/MSB math.
- `github.com/skixer2/SGC_SensiBLE2_1` — old firmware: `{num,type,time}` records newest-first from NVM, read→notify→index++, time-sync writes.
- Archived decision transcript: `sessions/b18840c2-d2bf-4ecd-b0bb-e47c120bcb6e.jsonl.reset.2026-09-10T05-39-34.828Z` (agent gateway).
