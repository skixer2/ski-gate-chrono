# OpenClaw Unified Test Ledger — Ski Gate Chrono (SGC)

**Owner role:** Lead Systems Coordinator (this chat / `ski_gate_chrono` session)  
**Test base folder:** `.../module_design/unit_tests/`  
**Ledger root:** `unit_tests/test_ledger/`  
**Last updated:** 2026-09-03 13:15 UTC
**Current baselines:** FW **5.74** (completely sleep BHY2/sensors in SLEEP state, `ed5ca51`) · App **1.41** (single-connection batch + streamed saves, `50fdc12`) · HW **v4.2** · Port **COM8**
**Last harness:** 5.27 partial — **5.37 smoke PASS** (run_20260824_1715, COM3)  
**Results dir:** `unit_tests/tmp_test_results/` · auto-push via `run_*.ps1`

This ledger is the **living test + debug notebook** for cross-stack SGC work
(firmware · Flutter · BLE · hardware). Automated catalogs stay in
`TEST_CATALOG.md`; this file captures **architecture identity**, the **active
case**, raw logs, verdict, and coordinator tasking.

---

## 0. How to use (coordinator protocol)

1. **One active case at a time** in §2. When closed, move a snapshot to
   `history/YYYY-MM-DD_<slug>.md` and clear §2 for the next case.
2. Paste **raw** serial / logcat / Flutter console into §3 (do not paraphrase).
3. Coordinator decides architecture + fix direction → writes **atomic tasks**
   in §5 (file, method, inputs, outputs, constraints).
4. **DeepSeek subagents** implement tasks; coordinator reviews diffs, updates
   ledger verdict, and only then claims green.
5. Automated gates still run via `TEST_CATALOG.md` tiers (smoke / core / full).
   Ledger does **not** replace harness runs — it **frames** them.

### Task card format (mandatory for subagent work)

```text
TASK-ID: T-###
GOAL: <one sentence>
FILE(S): <path relative to repo>
ACTION: create | modify | delete | document | schematic
SYMBOL: <function/class/method or N/A>
BEHAVIOR: <what it must do>
INPUTS: <args / events / bus / BLE char>
OUTPUTS: <return / JSON event / GATT notify / file>
CONSTRAINTS: <stack, timing, no-float-scanf, SPI isolation, …>
DONE WHEN: <observable proof: serial line, test id, screenshot>
OUT OF SCOPE: <explicit non-goals>
```

---

## 1. System Architecture Map

### 1.1 Stack

| Layer | Technology | Location (repo) | Notes |
|-------|------------|-----------------|-------|
| **Device FW** | C++ / PlatformIO / Arduino_BHY2 / ArduinoBLE (Cordio) on **nRF52832** | `implementation/Firmware_implementation/` | Nicla Sense ME today; production = ANNA-B112 + SGC carrier |
| **Sensors** | BHI260AP (IMU/fusion), BMP390 (baro), **Piezo button** (arm/wake), LDC1612 (diag-only, lazy-init) | FW `src/sensors`, `src/state_machine` | SPI0 shared: BHI260 CS + MX25R CS — **no concurrent SPI** |
| **Flash** | MX25R1635F **2 MB** (bench Nicla) / BOM path **MX25R6435F 8 MB** | `src/storage/*` | SFDP size → layout (v5.01+); pre-roll **fixed** `0x0000–0x13FFF` |
| **Phone app** | **Flutter** · Android target | `implementation/Phone_app_prototype/` | iOS not primary |
| **PC tests** | Python harness + SIM | `unit_tests/`, `system_tests/` | COM8; JSON-lines device protocol |
| **Hardware** | Carrier PCB v4.2, USB-C charge, 8 MB flash option | `PCB_layout/`, hw docs in repo root / architecture | Qi dropped; beeper DNP |

### 1.2 Product flash map (FW ≥ 5.01)

| Region | Address (2 MB classic) | Scales with chip? |
|--------|------------------------|-------------------|
| FlashRing pre-roll 4000×20 B | `0x0000–0x13FFF` | **No** (product arm window) |
| Run slots N × ~244 KB | `0x14000` → config | Yes: 2 MB→8, 4 MB→16, 8 MB→32 |
| Config / Index / Self-test / spare | top 4 × 4 KB sectors | Yes (top-of-chip) |

Boot must emit: `{"ev":"flash_map","size_kb":…,"slots":…}` then `{"ev":"raw_store","ok":1,…}`.

### 1.3 BLE identity

| Item | Value |
|------|--------|
| **Local name** | Device config (default SGC family); ADV only in **IDLE / POST_RUN** |
| **Service UUID** | `53470000-0000-1000-8000-00805F9B34FB` |
| **UUID pattern** | `5347` + `XXXX` + `-0000-1000-8000-00805F9B34FB` |

| Char | UUID | Props | Role |
|------|------|-------|------|
| **ABC0** | `5347ABC0-0000-1000-8000-00805F9B34FB` | Write | UTC epoch time sync |
| **ABC1** | `5347ABC1-…` | R/W | Device name (≤20) |
| **ABC2** | `5347ABC2-…` | R/W | Arm side |
| **ABC3** | `5347ABC3-…` | R/W | Discipline |
| **ABC4** | `5347ABC4-…` | R/N | State (bits4–0) + sensor flags (7–5) |
| **ABC5** | `5347ABC5-…` | R/N | Battery % (6–0) + charging (7) |
| **ABC7** | `5347ABC7-…` | R/N | Flash used % |
| **ABC8** | `5347ABC8-…` | R/N | Run count u16 + oldest age u32 (6 B) |
| **ABC9** | `5347ABC9-…` | R | Run list JSON (512 B) |
| **ABCA** | `5347ABCA-…` | W | FT request (run id) |
| **ABCB** | `5347ABCB-…` | N | FT chunk (notify; FW paces ~20 B @ 25 ms) |
| **ABCC** | `5347ABCC-…` | R | FT CRC32 |
| **ABCD** | `5347ABCD-…` | R/N | FT status (2=done, 3=error, …) |
| **ABD0** | `5347ABD0-…` | R/N | Calibration |

**Sources of truth:**  
FW `src/ble/sgc_service.cpp` · App `lib/ble/sgc_service.dart`

### 1.4 Critical runtime invariants (do not regress)

1. **Init order:** BLE first → Flash → RawRunStore → Ring → BHY2 → Piezo Button.  **LDC1612 NOT in boot path** (v5.34+) — lazy-init on `c`/`z` serial commands only.  
2. **No large stack/heap** after Cordio up (nRF52832) — index I/O streams ≤256 B pages (FW 5.02).  
3. **During BLE FT:** no BHY2.update / feed_sensors / ambient SPI (FW 4.97+).  
4. **Erases off descent path:** full-slot prepare at POST_RUN/boot, not ARM.  
5. **JSON-lines only** on serial (ADR-001).  
6. **LA units:** mm/s², |g|=9810 on phone ImpactDetector.  
7. **CRC:** payload-only (skip 16 B RunHeader) on app validate.

### 1.5 Related docs (do not duplicate wholesale)

| Doc | Role |
|-----|------|
| `unit_tests/TEST_CATALOG.md` | Automated keep/merge/drop + smoke/core/full |
| `unit_tests/README.md` | How to run harness |
| `system_tests/device.md` | Device system test semantics |
| `TOOLS.md` / `MEMORY.md` (workspace) | Operator cheatsheet + durable handoff |

---

## 2. Active Session Test Case

### TC-2026-10-02-002 — M3 Phase B: sensor producer (real BHI260 frames) — OPEN (design review)

| Field | Value |
|---|---|
| App | `03_pull_proto` (base v0.11.14) |
| Opened | 2026-10-02 10:55 UTC (ZioClaw, requirements-first per JP rule) |
| Goal | Synthetic 8 B placeholder frames → real RV/LACC/baro @100 Hz + temp @1 Hz, byte-identical ArduinoFW encoding |
| Requirements | `Firmware_Zephyr/docs/phase_b_requirements.md` (source-of-truth table read from ArduinoFW code, not memory) |
| Steps | B1 config+FIFO drain (serial proof, ≥95% of 100 f/s) → B2 BitPacker C port (golden vectors from Arduino bench) → B3 producer swap (≥99.9% ticks, ≤8.5 B/frame avg, BLE-loaded) |
| Decisions (11:31) | FIFO=poll (INT=fallback); golden vectors=yes (bench capture); temp header-only ×10 + arrival-temp in status metadata (blob parked for header v3) |
| B1 PASS (14:1x) | JP approved B1. Progression v0.12.0→v0.12.7: (a) K_THREAD_DEFINE delay is ms-int in Zephyr 4.4.2 (use -1, not K_FOREVER); (b) vendored BHY2 has DELEGATE_FIFO_PARSE_CB_INFO_MGMT=1 → register_fifo_parse_callback stores NOTHING; empty delegate → BUS FAULT on garbage ptr. Fix: host-owned sparse 4-entry callback table in bhy2_zephyr.c + `bhy2_zephyr_set_fifo_callback()`; (c) event_size INCLUDES sid+status: data_size observed RV=11, LACC=7 (payload at data_ptr+1, values sane, sync stable) but BARO=4 (uint24×0.0078 per Bosch SensorID.h — byte window TBD) and TEMP=5 (int16×0.01 + 2 trailing bytes, 0xFD-family markers seen = window TBD). v0.12.4 bench (b1cap7): rv=99/s, lacc=99/s, baro=16.4/s (hub-clamped), temp≈1/s, err=0, short=0, 60+s — RATE GATE PASS (hold-last for baro/temp = ArduinoFW parity). Baro/temp exact decode → B2 golden vectors on Arduino bench (reference decoder, same-moment values). Raw FIFO hexdump capability proven (v0.12.7, b1capA). |
| Status | B1 RATE GATE PASS; decode of baro/temp windows open → B2 golden vectors. Next: B2 → JP review |
| v0.12.8 (15:40) | Payload at data_ptr+0 (event size INCLUDES SID; no status byte in stream). Proof: unit-quat check on FIFO dump; baro 0x01C02A=114730 raw == decoded baro_pa, ×0.0078=894.9 hPa ≈ ambient at bench elevation. Stationary 40 s: qw 14936–14988 stable, tx10=289 (28.9 °C). RV/LACC/BARO decode LIVE-verified; temp window B2. |
| | Motion PASS (16:05) | JP bench capture while moving device: qw swings 505–7744 (rotation tracked, in Q14 range); baro_pa 114743–114758 (±8 noise); tx10 28.4–28.6 °C. Counters (rv/lacc/baro/temp/err/short) constant by design = rates & anomaly tallies, not values. **v0.12.8 B1 verification COMPLETE: rates ✓ stability ✓ ranges ✓ motion response ✓** |
| **B2a PASS (16:40, c50edf5)** | **Golden vectors LOCKED: 20/20 byte-exact vs Arduino_BHY2 reference, 15/15 physics.** v0.13.1 'v' command (serial-only, line-terminated rx — no \n = command ignored) → gv lines (raw hex + fw decode, same frame); tools/golden_check.py = host reference decode + physics gate; vectors: tests/golden/golden_vectors_20261002.jsonl. **TEMP solved: event = SID + int16(0.01 °C) + 2×00** (drift 28.10→28.13 °C visible). BARO byte-exact (0x01C05E=114782≈895 hPa). RV acc field = -14065 int16 (≈π Q14 = uncalibrated heading-accuracy semantics; Arduino reads it Uint16). RV/LACC Q14/mg confirmed.** Next: BitPacker C port (vectors = its unit test) + optional B2b live Arduino cross-flash.** |
| **B2b PASS (19:20, f18f8ac, v0.14.0)** | **BitPacker C port byte-identical to ArduinoFW original — on-target differential, 81/81 frames, 0 fails** (GOLDEN 5 / COAST 60 incl. forced anchor @50 / BOUND 7 ±8,7±128,127 / TJUMP 4 >1023ms / BARO 5). Method: original bit_packer.cpp compiled INTO the Zephyr app (bp_ref/, CONFIG_CPP — Zephyr 4.4 name, was CPLUSPLUS) behind a C glue; 'V' runs both impls on identical sequences on the nRF52832, memcmp per frame. C port: src/bitpacker.{c,h} (sgc_* API, no Arduino deps). Host differential twin parked at tests/bitpack/ (gateway container has NO host compiler — no gcc/g++/qemu; arm-none-eabi from pio is cross-only). **B2 COMPLETE. Next B3: wire producer → BitPacker → SGL3 (baro unit decision Pa/2 vs raw pending), then Phase C detector.** bp_ref C++ (+~2 KB flash) stays as the on-bench oracle; strip at producer-merge review. |
| **B3 PASS (2026-10-03 ~11:00, v0.15.2, 46d9d43)** | **Producer swap COMPLETE: BHI260 → BitPacker → SGL3 → serial dump → host decode → physics PASS.** 30 s run: 2998 frames, 21,948 B, **avg 7.32 B/frame** (≤8.5 ✓), T1/T2/T3 = 2128/810/60 (host decode == FW counts EXACT), \|q\| 1.0000±0.0001, baro 896.7–896.8 hPa, \|la\|max 26 mg, 30.0 s, 100% payload consumed, 0 skipped ticks. Chain: seqlock snapshot (drain→rec_q) → sgc_bitpacker (k_uptime ts) → store_record_append → 'H 12' hexdump (dump_run.py, read-until-rh_end) → decode_packed.py. **Findings fixed en route:** (1) BHI260 + NOR flash SHARE spi2 — first combined run starved drain (prio 5) → hub FIFO stall at 28 s; fix = drain prio 3 (above rec_q 4) + 5 s stall guard w/ fifo_flush (bhy2_hif_set_fifo_flush(SID, hif) — 2 args!). (2) STALE-HEX TRAP: v0.15.0 build failed (unclosed case brace) but zflash flashed the old hex — 23,984 B = 2998×8.0 synth signature exposed it. RULE: verify build log tail BEFORE zflash, always. (3) b1_rate reporter memsets whole prod struct — values zeroed after sensor death (misleading); counters-only now. Open (B4): ARMED pre-roll ring, rec_q tick jitter 2% dt≠10ms under flash load, FW avg stat divides by st_frames incl. ARMED discards (report t-frac), seq_err 1/2998 benign. |
| **Precision audit (2026-10-05, JP directive: impacts detected OFFLINE on phone, target 1/100 s = 10 ms (JP corrected from 1/110))** | Device role = full-rate la + true timestamps, NOTHING decimated (device-side 10 Hz hook is LIFECYCLE detectors only — start drop / flatline end; impact detection is phone-side per JP). b3dump analysis: dms 10ms×2939 (98%), 11ms×43, 9ms×14, 0ms×2 (>1023ms T3 blind gaps, 0.07%). Phone interpolation on rising edge → ~2-5 ms achievable vs 9.1 ms target. OPEN (B4): eliminate >1 s rec_q stalls (2/3000, cause TBD — flash flush?), phone flags dms=0. **B4 SCOPE (JP-clarified 13:05): (a) ARMED pre-roll ring port — 4000×20B raw+ts region 0x0000-0x13FFF, staging-buffer net-−1/tick (F02/v6.0); at trigger, last 1000 frames = 10 s PREFIX of the run. PURPOSE (JP): capture the START MOMENT — start trigger = poles swinging 90°→60° w.r.t. horizontal (30° from vertical/gravity vector), seen in RV; trigger fires after motion begins, pre-trigger frames pin the exact start. Impacts inside pre-trigger = ordinary impacts, no special treatment. (b) rec_q stall root-cause (2/3000 frames stored dms=0 because >1023 ms gaps exceed the 10-bit header delta field — timeline holes for the phone). (c) FULL-CYCLE bench (NOT "BLE-loaded" — JP: no phone connection from arming to run end, v0.10.3 ARM tears BLE): arm→run→end→topup→phone download→re-arm.**
START-DETECTOR SPEC (JP, first stated 13:05): pole tilt 90°→60° w.r.t. horizontal = 30° from vertical, via gravity-vector angle in RV — device-side lifecycle detector (10 Hz hook), Phase C. |
| **B4 step-0 RESULT (2026-10-05 ~14:30, v0.15.4)** | **Timing VERDICT: YES — 0 tick_gap events (>50 ms) under full simulated drain load** (3x20 B writes + 2x20 B reads per 10 ms tick, page-buffered, whole-run duration), sensors solid (rv=99 for 90+ s, no bhy_stall). **BUT sim wedged flash path**: 60 B/s x 60 s into 20 KB region = ~17 page-reprogram wraps (no erase) → flash_write hang, rec_q stuck, 'e' never processed, run never closed (bench device left mid-LOGGING; reboot heals). ROOT-CAUSE next session: driver vs chip on program-to-programmed-page. **DESIGN QUESTION RAISED (JP's min-size + clear-at-run-end)**: 1000-slot ring can be overfilled by long ARM (timeout 120 s = 12k frames > 1000 slots) — needs ARM-phase write-stop policy (ArduinoFW 4000-slot: 3000 ARM cap + 1000 drain headroom = arithmetic fit) OR bigger ring. Read ArduinoFW flash_ring wrap policy BEFORE porting. Sim fix: stop-at-full or erase-at-wrap (never reprogram). |
| **RING SPEC v2 (JP, 14:04-14:10)** | **15 s pre-trigger + 15 s drain runway = 3000 slots x 20 B (60,000 B -> 15 sectors, 0x0000-0xEFFF, inside reserved region). Both banks erased after a run, before next ARM (+boot).** No wrap/reprogram by construction during ARM<30 s or any run (15 s wait + 7 s run = zero loops; head 2/tick outruns tail 1/tick). Runs shorter than drain leave old backlog unspliced (harmless). OPEN (JP decision): armed wait > 30 s — (a) ping-pong: erase the OLDER bank when both full (retention = always last 15 s; introduces ARM-phase sector erases ~0.5-0.8 s spread 1/tick, stale frames at risk only) vs (b) stop-at-30 s (no ARM erases ever; armed > 30 s = stale pre-trigger). Sim timing PASS transfers unchanged (per-tick load identical). |
| **RING SPEC v4-FINAL (JP, 15:15)** | **ARM timeout = 20 s** (room for arming→start→drop delays; 120 s was too much). **Semantics (JP 15:58): A = 20 s PRE-trigger max, B = 20 s POST-trigger max. A: 0x0000-0x09FFF (10 sectors, 2048 slots); B: 0x0A000-0x13FFF (10 sectors — worst case: full A backlog → drain 2048 ticks → exactly 2048 live frames transit B; head 2/tick catches tail exactly at drain end). Ring carries ONLY the first ≤20 s of post-trigger (drain bridge); afterwards live frames encode DIRECT to the run store — run length bounded by budget, not the ring. Region fills 0x0-0x13FFF EXACTLY (zero slack: ring bookkeeping in RAM only, LOG_START 0x14000 untouched, future sector need = something moves). ERASE RULE: entering IDLE ⟹ A+B erased (coherent invariant, JP-approved: ARM always starts on clean banks).**
| **RING SPEC v3 FINAL (JP, 15:03)** | Ping-pong REJECTED (erase stall = real hole inside retained window). **ABSOLUTE: ARM self-terminates at 15 s → IDLE.** No fill-wait: trigger at any t≤15 s splices the existing pre-trigger (≤15 s) then post-trigger frames continue immediately. A (1500 slots) = pre-trigger cap, cannot overflow (ARM ≤ 15 s); B (1500) = drain runway, absorbs worst-case tail writes (backlog ≤ 1500, net −1/tick). 3000 slots × 20 B = 15 sectors 0x0–0xEFFF. Erase A+B: at POST_RUN AND on arm-timeout (ZioClaw proposal, JP to confirm — prevents aborted-attempt data splicing into the next run). ARM timeout 120 s → 15 s (recorder). Zero wrap/reprogram/holes by arithmetic. |
| **B4 PORT LANDED + SPLICE PROVEN (2026-10-05 ~16:45, v0.16.1, 5b553b2)** | ring.c/ring.h per spec v4-final; recorder: ARMED capture→ring, v4.77-safe drain (pop2→push1-if-nonempty / direct), 20 s ARM cap, erase-on-IDLE (timer stop BEFORE erase). **Bench: 2 s arm + 10 s run → 1199 frames, 12.0 s continuous stream (pre-trigger splice PROVEN, true arrival ts), |q|=1.0000, baro 889.2 stable, 0 dms holes, T3=24 exact, avg 7.05 B/f, PASS.** v0.16.0 page-buffer ring was GARBAGE (20 B/256 B misalignment → staged-at-0/read-at-240; |q| 0→2.5, 169 holes) — deleted for direct per-slot 20 B program/read (~0.5 ms/push, affordable per sim). TRANSIENT (watch): run-16 frame-0 anchor had baro=0 (not reproduced in run-17; if recurs → instrument). OPEN: ~80 ms tick_gap seen 2/3 runs (~1/min class — prime suspect flash program; the >1 s release-blocker stall still unreproduced since v0.15.2), arm-timeout path bench, full-20 s-A test, full-cycle w/ phone. |
| **ABC0 EPOCH SYNC (2026-10-05 20:25, v0.17.0, 46a514e)** | JP GO. ArduinoFW-parity port: ABC0 (5347ABC0 UUID) write-only uint32 LE epoch s, >1e9 sanity, silent; store anchor + uptime extrapolation; next_run_ts = max(last+1, wall||BUILD_EPOCH-fallback). App-visibility mystery SOLVED: all runs fake-dated 09-24/25 (BUILD_EPOCH+monotonic) → new runs indistinguishable/interleaved in app (JP confirmed: most runs show 09-25, no filters). ARCHITECTURE CONFIRMED (JP): ALL detectors phone-side; device-side = 2 m BARO-DROP TRIGGER ONLY (starts logging) + budget close. Flash complete, awaiting phone connect to verify {"ev":"epoch_sync"} + first real-ts run. Next: JP connects app → sync event → verification run → app shows it TODAY. |
| **ABC0 MYSTERY SOLVED — UUID typo (2026-10-05 21:10, JP found it in nRF)** | Char WAS registered all along — at **5347ABC0-0000-03e8-...**: my BT_UUID_128_ENCODE used decimal `1000` instead of `0x1000` in group 3 (1000 dec = 0x3E8). App searched the correct -1000- UUID → silent `_writeChar` skip → no sync. JP's nRF check ("slightly modified UUID!") = the catch. Fixed v0.17.2 (`8b130b2`); 'G' GATT-dump debug (added 0.17.1, 2 build fails: callback signature + nonexistent bt_gatt_attr_get_uuid in 4.4) STRIPPED in 0.17.4 — diagnostic served by nRF instead. v0.17.4 flashed + boot-verified. PENDING: phone reconnect → {"ev":"epoch_sync"} → first real-ts run → app shows it TODAY. LESSON: UUID literals — always 0x-prefixed; verify new chars in nRF before blaming caches/apps (saved by JP's eye). |
| **EPOCH SYNC + DIRECTORY SAGA (2026-10-06 05:46-08:45, v0.17.7)** | **ABC0 epoch sync WORKS end-to-end** (JP's morning connect: {"ev":"epoch_sync","utc":1791265551}=true UTC; run 19 ts=1791265620 real ✓). **Directory root cause FOUND + FIXED**: app/FBP delivers MAX 512 B of an ABC9 read (v1.1 contract "char_run_list 512"); our 20-run listing (950 B) truncated mid-JSON → FormatException "Unterminated string at 513" → "0 on device" (Sept never exceeded 512 = why it worked). Fix v0.17.7: listing fills NEWEST-FIRST, whole entries only, ≤510 B → app shows 20/9-missing, Download button LIVE. >10-run listings = app-side v1.49 item (paginated dir or console-D path). **NEW BLOCKER (open): download fails "D: directory timeout"** — V2 download flow re-fetches directory via console 'D' request-response between runs and times out; HYPOTHESIS: interleaved {"rec":"IDLE"}/{"free":12} printk lines in the D response stream break the app's d_end wait (seen in serial captures; wire-contract lesson 09-28 adjacent). NOT verified. |
| **DOWNLOAD TIMEOUT ROOT-CAUSED (2026-10-09 19:54 UTC, logcat-proven)** | **ATT TX-pool exhaustion: cmd_work emitted ALL 21 D-burst notifications back-to-back; gatt_notify fail-fasts -ENOMEM once CONFIG_BT_ATT_TX_COUNT=10 pool drains over-the-air (~1 PDU/conn-event) → phone received exactly 9 run lines (runs 0-8), d_end LOST → "D: directory timeout" ×8 attempts.** Zephyr source proof: gatt.c `bt_att_create_pdu` → NULL → return -ENOMEM (fail-fast, LOG_DBG only). Evidence chain: requestMtu(517)→247 OK · 'D' write OK · 9 notifications then silence (SGC_NORDIC logcat) · pool arithmetic 10−(write rsp+CCCD)≈9. 10-06 interleaving hypothesis DEAD ({"rec"}/{"free"} are serial-mirror printk by design, never on BLE). MTU-23 UI card = stale Flutter state, not real. ABC9 read works because ATT reads are per-request flow-controlled — only notify BURSTS hit this. **Fix v0.17.8: paced dir emitter — one line/15 ms via k_work_delayable, retry SAME line on -ENOMEM, E 4 busy-guards (D and s cross-guard), d_abort on link death, serial extras once. Scales 21 lines=320 ms, 255 runs=3.9 s < 10 s deadline.** App 1.49: pullDirectory timeout now reports packets=N + last line (error-tolerance directive). Bench verify = flash 0.17.8 → rebuild debug APK → Download Missing ×9 runs incl. run 19. |
| **DOWNLOAD FIXED + VERIFIED (2026-10-09 20:27, v0.17.8 / App 1.49)** | **Bench PASS: Download Missing → 9/9 runs, ALL attempt=1, single connection, 169 KB in ~31.5 s, ~9.7 KB/s on big runs (historic ceiling), byte counts exact (+22 B trailer), 0 ERROR:notify on serial, d_end delivered, rec/free stayed serial-only. Values gate: Run #12 = 30.0 s / 2998 frames == B3 source run EXACT; #13/#14 60.0 s 5995 f; #15/#16 9.6/11.2 s 1121 f; #11 = 1 frame (Sept-era junk run, same on ArduinoFW — data, not decode). Full-cycle leg PROVEN: store → paced D → s-stream → CRC32 → decompress → decode. TC-2026-10-06 DOWNLOAD BLOCKED → RESOLVED. Build p0178 RAM 81.6%/FLASH 26.7%. Fresh install needed (SYSTEM debug keystore ≠ v17ni keystore → INSTALL_FAILED_UPDATE_INCOMPATIBLE; uninstall+install, BLE permission re-grant). |
| **ERROR-TOLERANCE AUDIT (2026-10-09 21:0x UTC, JP GO 18:42 — ABC9 + all chars, device + app)** | **FINDINGS (all paths walked; evidence: sgc_service.dart, ble_manager.dart, file_transfer.dart, run_list_screen.dart, NordicBleDownloader.kt, pull_proto.c):** ⛔ CRITICAL — (A1) `_readChar` returns Uint8List(0) for BOTH char-missing AND read-fail, debugPrint only; (A2) `_writeChar` swallows everything incl. char-not-found (syncTime failure = invisible forever); (A3) `readConfig` fabricates defaults (name ""/left/gs/cal 0) — **the "Arm Side Left / Discipline GS" card is CLIENT-SIDE FICTION on Zephyr FW** (B10: ABC1/ABC2/ABC3/ABD0 not implemented — grep-verified); config WRITES vanish silently too (via A2; ATT-level error eaten by catch(_)); (A4) `getRunCount`→0 on failure ("0 on device"); (A5) `getRunList`→[] on empty/parse-fail = THE 10-06 masker (FormatException "Unterminated string at 513" hit exactly this catch → "0 on device" + green Connected snackbar (A6: screen setup try/catch debugPrint-only, success snackbar fires regardless)); (B11) ABC4/ABC5/ABC7 missing on FW → getDeviceState=0/getFlashUsed=0/**DANGEROUS 0%-flash default**, battery=-1 — app presents as real. ⚠️ HIGH — (A7) Kotlin pullDirectory ignores "E x" lines → 10 s timeout instead of instant device-error; (A8/B12) ABC0 write: insane-epoch silently ignored, no readback → app can NEVER verify time sync (propose: ABC0 readable = last-synced epoch, app verifies post-write); (A9) downloadRun stream-CRC mismatch = warn-only (mitigated: trailer CRC gate in _downloadOne + native onRunComplete validateCRC — but should throw). ℹ️ LOW — MTU card reads cached _ble.mtu while disconnected (stale "23 bytes" seen during native batch — hide when disconnected); ABC8 age field = placeholder zeros (no RTC yet); write_console binary-drop = ArduinoFW parity BY DESIGN (document); battery -1 sentinel fine IF UI renders "unknown". **FIX BATCHES: (1) app-mechanical — typed exceptions (_readChar throws CharMissing/CharReadFail, _writeChar throws), readConfig→nullable+UI "unknown", getRunCount/getRunList throw w/ context (len+head), setup errors → red snackbar + degraded state, pullDirectory fast-fail on E, hide stale MTU; (2) device-small — ABC0 readback, E-signal on rejected epoch optional; (3) DECISION JP — implement ABC1-3/ABD0 + ABC4/5/7 on Zephyr FW (config persistence = flash config sector, interacts w/ producer-merge) vs interim app-side "not supported by FW" explicit badge.** |
| **JP DIRECTIVE (2026-10-06 06:46): ERROR-TOLERANT READS, UNIVERSAL STANDARD** | Every code part (device AND app) must tolerate read errors AND SIGNAL them explicitly — never silent empty/fallback (the silent _readChar/Uint8List(0) pattern masked this bug for days; silent config-char defaults hid missing ABC1-3/ABD0 chars). Audit target: ABC9 read path first, then all BLE chars + app-side error surfaces. |
| **BENCH STATE (2026-10-06 08:45)** | FW v0.17.7 running (ABC0 sync + 512-fit listing + abc9_read instrumentation still in — strip at review). S22 has the DEBUG 1.48 APK installed (157 MB, debugPrint visible via logcat — KEEP until download-timeout hunt done, then reinstall release; build via build_debug.bat, JAVA_HOME=Android Studio jbr). Repo synced both sides. Runs 0-18 fake-dated (BUILD_EPOCH), run 19 = first real-ts run. Tools added: tap_watch.py, watch_cycle.py, uiparse.py, lcgrep.py, lctail.py, loglines.py, sendcmd.py, serial_watch.py, run_cycle.py, findjava.py (all via repo relay — echo-chains unusable under SYSTEM). |
| **RULE (JP, 2026-10-02 15:58)** | **No decode/pipeline step is reported PASS — and no code leaves the bench — until output VALUES are verified meaningful AND physically possible.** Minimum gate per sensor: range check (quat components ≤±16384 Q14, \|q\|≈1; baro raw×0.0078 ∈ 800–1050 hPa; temp −10…+60 °C), stationarity (values stable on a still device), response (values track device motion/orientation change). Rates/err counters alone NEVER suffice. B1 was declared PASS on counters while values were garbage — do not repeat. |

### TC-2026-10-02-001 — BHI260AP Zephyr bring-up (M3 Phase A) — RESOLVED (PASS)

| Field | Value |
|---|---|
| App / FW | `03_pull_proto` v0.11.14 (commit b8c6574) |
| Duration | 2026-10-01 13:28 → 2026-10-02 08:15 UTC (bench, remote-driven) |
| Symptom | product_id 0 / soft_reset −5 / boot_status 0x00 — “hub silent” |
| Root cause 1 | `bosch,bhi260ap` has NO binding in Zephyr 4.4 → unbound node → `SPI_CS_GPIOS_DT_SPEC_GET` empty → driver silently skipped CS (hub never selected) |
| Root cause 2 | manual-CS era: `GPIO_ACTIVE_LOW` + `gpio_pin_set_dt(cs,0)` = physical HIGH — assert/release inverted |
| Phantom | reg 0x90 reads 0x00 even via working mbed stack (probe4 S0) — the “dead hub” metric was noise; boot_status 0x11/0x13 is the real one |
| Proof | id 137 (0x89), soft_reset 0, boot_from_flash 0, status 0x11→0x13 |
| Collateral fixes | 8 MHz explicit (16 illegal on nRF52832 SPIM2); scratch-buffer read framing (no NULL skip entries); P0.18 reset held high (mbed core parity, CNF 0x301); BB probe + PMIC probe arsenal (compiled out, serial 'B' rearm) |
| Docs | `Firmware_Zephyr/docs/bhi260_zephyr_bringup.md` (full case study) |
| Next | Phase B (JP-paced, this afternoon): virtual sensors RV/LACC/baro @100 Hz + temp @1 Hz → FIFO poll → BitPacker → SGL3 |

*Prior session case below (historical).*

*Status: **RESOLVED (PASS 5.74 / 1.41)** — Zero-interruption BLE downloads achieved via memory isolation, sensor-bus sleeping, connection priority scaling, and single-connection batching.*

> **2026-09-03 13:15 morning campaign & complete resolution:** Pushed **FW 5.74 (`ed5ca51`)** and **App 1.40 (`4df3cb7`)** resolving the final cross-stack BLE bottlenecks:
> 
> 1. **GATT-layer Notification Overwrite (Wedge Root Cause):** Android's BLE stack overwrites the internal byte array buffer when fast notifications arrive. Nordic BleManager's callback was passing a raw reference `data.value`. By the time our background thread processed it, the next notification had overwritten its contents. This caused payload CRC failures, spurious `0x04` errors, and stack wedging. **App 1.39** fixed this by cloning the notification array immediately (`it.clone()`), completely stabilizing the stream and letting us reduce the settle delay back to 4 s.
> 2. **Post-FT SPI/I2C contention freeze (Reboot Root Cause):** When `g_ft_state` became `FT_DONE` (active = false), the device's main loop immediately resumed `BHY2.update()` and `feed_sensors()` while BLE was still flushing the final CRC packet. This hardware bus lockup hung the main loop, expiring the 12 s grace and triggering a WDT `rr:2` reboot. **FW 5.74** completely suspends `BHY2.update()` and `feed_sensors()` during `SLEEP` state (there is zero need for sensor polling when the device is asleep and connected), eliminating the post-FT freeze.
> 3. **Queue Saturation on S22:** S22 running at a default/balanced Connection Interval (e.g. 60 ms) with a 60 ms SGC packet cadence has 100% duty cycle, meaning any single RF packet loss exhausts the queue and crashes the link (`status=8 GATT_CONN_TIMEOUT`). **App 1.40** requests `CONNECTION_PRIORITY_HIGH` (11.25-15 ms) on connection start, giving the BLE link 4x retransmission headroom per packet and preventing queue starvation.
> 4. **Fast desync recovery:** Verified FW 5.73 fast recovery via forced `BLE.disconnect()` (W wedges cleared, device back to ADV, phone auto-reconnects in <2 s).
> 5. **Invisible local runs:** Hardened `run_list_screen.dart` to save runs incrementally (one native call per run) and `local_storage.dart` `listAll` to skip corrupt index entries instead of failing silently (App 1.36). All 5 runs now download and display perfectly.

> **2026-09-01 16:35 architectural correction:** nRF Connect app source is
> closed, but Nordic's Android BLE Library is open-source BSD-3-Clause and
> provides the hardened queued `BleManager` we should have used instead of a
> hand-rolled `BluetoothGatt` engine. **App 1.31** adds
> `no.nordicsemi.android:ble:2.11.0` and switches the native downloader to a
> `NordicBleDownloader` built on `BleManager`, keeping the same FT protocol,
> resume, one-run-per-connection batch, full notify parity, and 4 s settle.

> **2026-09-01 13:27 native batch bench:** major progress — one tap completed
> **two full downloads in a row** (expected batch behavior: one run per
> connection). Third transfer wedged at chunk 115 → clean `tx_blocked`, then
> the device unexpectedly rebooted (`boot rr:2` WDT). Root hole: FT ISR WDT
> feeder stopped immediately at `ft_abort` while the wedged BLE link was still
> tearing down. **FW 5.67:** 3 s post-FT WDT grace via `mbed::Timeout` +
> explicit feeds around abort ERROR notify.

> **2026-09-01 13:18 App 1.29 build failure:** this Flutter/Android SDK stub
> does not expose `BluetoothGattCallback.onConnectionUpdated` to Kotlin
> (`overrides nothing`). App 1.30 removes the callback and uses a fixed 4 s
> post-subscription settle delay instead.

> **2026-09-01 13:06 App 1.28 bench:** still wedged, device `ft_txfail`
> chunk 121 / offset 29,282 → `tx_blocked`. Full notification parity was not
> sufficient. Remaining nRF difference: manual human delay between service
> subscription and CMD_START allowed Android connection updates to settle.
> App 1.29 logs `onConnectionUpdated`, waits up to 3.5 s for a post-
> subscription update + 500 ms grace, and avoids re-writing the ABCD CCCD.

> **2026-09-01 13:01 App 1.27 bench:** still wedged, device `ft_txfail`
> chunk 114 / offset 27,588 → `tx_blocked`. Removing connection-priority was
> not sufficient. nRF's successful log showed notifications enabled for
> ABC4/ABC5/ABC7/ABC8/ABCD/ABD0 + Service Changed, while native enabled only
> ABCD. App 1.28 copies the full nRF subscription set; FT parser consumes
> only ABCD packets.

> **2026-09-01 12:36 nRF Connect control: PASS.** Same S22, same device,
> same 39,372 B run. nRF Connect received FINAL `03-BE-A7-B6-28`; LE CRC
> `0x28B6A7BE = 683059134`, matching device `ft_done`. Device and S22 BLE
> path are both capable. Main setup difference in native downloader was
> `requestConnectionPriority(BALANCED)`; App 1.27 removes it and leaves link
> parameters at Android default.

> **2026-09-01 11:44 first native bench evidence:** Kotlin engine connected,
> negotiated MTU 247, started run #0 and received **39,204/39,372 B**. Device
> completed (`ft_done`, 163 chunks, 0 blocks); phone lost the final **168 B +
> CRC** when Android dropped the link `status=8`. Native then hit reconnect
> `status=147` while racing device re-advertising.
>
> **App 1.26 response:** fail fast on FT link loss (no 20 s idle wait), fail
> connect immediately on Android GATT status, and scan/wait for the device
> advertisement before resume reconnect.

> **2026-09-01 10:58 JP direction:** after ~2 weeks on the BLE connection
> issue, compare against the old working pair `SGC_SensiBLE2_1` + `SGC_06`.
> The old phone side was native Android BlueSTSDK, with explicit
> `BluetoothGatt` queues on dedicated handler threads; the working transfer
> style was small request/response timestamp pulls. This supports moving the
> current BLE-critical path out of FlutterBluePlus.
>
> **App 1.25 native spike:** new Kotlin `NativeBleDownloader` + Dart bridge +
> “Native Download Missing” card. Native downloads only currently-missing run
> IDs with one-operation-at-a-time GATT sequencing, resume-capable FT, fresh
> connection per run; Flutter keeps decompression/CRC/local storage/UI.
> Bench target: all available missing runs, not a fixed count.

> **2026-09-01 09:14 JP correction (5-run device inventory):** the earlier
> "two runs, two ft_done / runs were saved" result was **not the full batch**.
> The phone lists **5 runs on the device**; after download only **2 appear
> locally**, and **3 remain reported as to-download**. Interpretation: the
> two completed transfers were real, but the batch did not land the full
> device inventory. Most likely phone-side multi-run/GATT reconnect gap after
> the Phase-2 one-run-per-connection transition; need run-key logs to rule
> out an identity mismatch (`#id@timestamp`).
>
> **App 1.24 response:** log device/local/missing run keys, retry inter-run
> reconnects 3× with increasing settle, retry empty/CRC-bad runs on a fresh
> GATT session, refresh local index after every save, and show failed run IDs
> in the final snackbar. Bench request: capture Flutter console from batch
> start; expected evidence = full 5-run plan line plus per-run outcome.

> **2026-08-31 21:00 bench (5.65 + 1.22): TWO RUNS, TWO ft_done ✓✓**
> (run 0: 39 372 B/163 chunks/0 blocks/9.85 s · run 1: 39 668 B/164 chunks/
> 0 blocks/9.87 s) — and both WERE saved to local storage. But the screen
> showed **no runs until app relaunch**: the intentional Phase-2 per-run
> disconnect tripped `_onConnectionChanged`, clearing all UI state
> mid-download. Cosmetic-but-alarming; runs were never actually lost.
> `state_blocked not_armed` = blocked force_state(LOGGING) from SLEEP —
> harmless serial spam (hold_sleep guards timers regardless).
>
> **App 1.23:** drop-handler skips clearing while `_isDownloading`; batch
> cancel via disconnect button; device list + service refreshed post-batch.
> **FW 5.66:** force LOGGING only from ARMED — spam gone.
>
> **Device-side status: SOLVED.** Two complete transfers, zero blocks,
> resume + one-run-per-connection working. Remaining risk is phone-side
> wedge frequency (multiple stops/run) — handled by resume, to be further
> reduced by Phase 3 background Auto-Sync (1-min poll).

> **2026-08-31 bench (5.64 + 1.21): FIRST ft_done ✓ (39 372 B, 163 chunks,
> 0 blocks, 7.58 s) — but two app-visible defects:** run #2 did NOT recover
> after `tx_blocked`, and the pre-reset partial run was "lost" (restarted
> from 0). Root cause: `sgc_ble_ft_abort` wrote a **bare 3** (FT_ERROR) —
> in the L-STREAM protocol 0x03 = FINAL → app completed with a partial
> buffer, no error → no resume → payload-CRC fail downstream.
>
> **FW 5.65 + App 1.22 (Phases 1+2, GO from JP):**
> - Device: abort = proper ERROR packet `[0x04, code]` (0x10 tx_blocked /
>   0x11 phone / 0x12 new_request / 0xFF other)
> - App: short-transfer guard (`received < total` → throw → resume) —
>   recovers even against legacy bare-3 aborts
> - App: **one run per connection** (reconnect between runs, replaces 2 s
>   cooldown) — JP's Auto-Sync model, Phase 2
>
> **Phase 3 (approved, after 1+2 validated):** background Auto-Sync — app
> launched + screen off, auto-connect on detection, **poll every 1 min**
> (athlete passes near phone between runs; avoid end-of-day backlog),
> download newest missing run, manual button stays. Needs foreground
> service + battery-optimization exemption.
>
> **Open watch-items:** chunk-26 hang (device "reactive but doing
> nothing", needed hard reset — one-off); `state_blocked not_armed`
> between runs (harmless?); reconnect timing after device reboot.

> **2026-08-31 bench (5.63 + 1.21): resume FIRED but reconnect failed.**
> Attempt 1 streamed to 26 620 B (~68%), wedge → fail-fast `FT: link lost`
> → app auto-resumed from offset ✓. But reconnect attempts hit
> `GATT_CONNECTION_TIMEOUT` (147): CMD_START forces SM → LOGGING (ADV OFF)
> and never restored it; the ~18 s txfail window kept the device busy.
> Device serial: 6× `ft_txfail` 2001 ms @ chunk 114 → `tx_blocked`.
>
> **FW 5.64:** pre-FT state captured at CMD_START, restored on every FT
> exit + immediate re-advertise @100 ms when not connected; retry window
> cut to 3 fails (~6.6 s) — wedge is terminal, patience only delayed
> re-advertising. Expected bench: wedge ~70 % → abort ~6 s → app resumes
> at +2 s against an already-advertising device → completes → stream CRC OK.
> (Note: JP backgrounded the app mid-resume in this bench — keep it in
> foreground for the download.)

> **2026-08-31 bench (5.62, Garmin DISCONNECTED): 77.5% + wedge verdict.**
> 126/162 chunks (30 492/39 372 B) at steady 60 ms — Garmin watch was a
> major confounder (chunk 12 → 126). Then 6 escalating retries ALL blocked
> 2000 ms → **wedge is TERMINAL within an attempt** (zero LL recovery,
> phone kills link at supervision). Decision table → resume-capable FT.
>
> **FW 5.63 + App 1.21 = resume:** CMD_START+offset (LE32), device
> CRC-prefills skipped prefix so final CRC stays whole-run valid; app
> auto-reconnects and resumes ≤5× (2/4/6/8 s backoff). **Bonus fixes (app):
> START size misparse + chunk-index byte stored inline (corrupted every
> 242 B block — would have failed payload CRC on the first completed
> download); live services getter; stream CRC32 verify; fail-fast on drop.**

> **2026-08-31 bench (5.61): CLEAN DETERMINISTIC FAILURE — library patch
> verified.** `ft_prog chunks:10` → chunk 12 blocked: `ft_txfail` ×3,
> each `blk_ms:2001` (bounded poll hit its 2000 ms cap, returned 0),
> then `ft_abort("tx_blocked")` at ~6.3 s. **No reboot, no hang** — main
> loop alive throughout. Hard proof: once the S22 wedges, it shows ZERO LL
> progress for ~6 s, then kills the link (phone supervision).
>
> **FW 5.62 experiment (one bench run → 1:1 decision):**
> (a) steady **60 ms** cadence, breathe disabled (2.4 KB/s) — does a gentle
> stream avoid the wedge trigger? (b) **patient escalating retry**
> 300/600/1200/2000/2000 ms, abort at 6 — if it still wedges, is it
> transient (`ft_recover`) or terminal (`disconnect`/`tx_blocked`)?
> Outcomes: completes clean → cadence was the fix · recovers → patient
> retry suffices · terminal → build resume-capable protocol (CMD_START+offset).

> **2026-08-31 bench (5.60): ZOMBIE HANG at chunk 12.** No reboot (5.60
> Ticker WDT feed works) but `SND_DATA 12` → frozen 60 s+. Library bug
> confirmed in source: `HCIClass::handleEventPkt` / `EVT_DISCONN_COMPLETE`
> never clears `_pendingPkt` → `sendAclPkt`'s `while (_pendingPkt >=
> _maxPkt) poll();` spins forever even after the phone disconnects.
>
> **FW 5.61 = vendored + patched ArduinoBLE 2.0.2** (`lib/ArduinoBLE`,
> removed from lib_deps, `SGC_PATCHES.md`): (1) sendAclPkt busy-wait
> bounded 2000 ms → -1; (2) `_pendingPkt=0` on disconnect; (3) handleNotify
> propagates failure → `writeValue()==0`. FT: 3 consecutive TX fails (300 ms
> holds) → `ft_abort("tx_blocked")`. Every link-death path now ends in a
> clean abort — no reboot (5.60), no hang (5.61).

> **2026-08-31 bench (5.59): device REBOOTED mid-transfer.** Serial:
> `SND_DATA 78` → `{"ev":"boot","ver":"5.59","rr":2,...}`. `rr:2` =
> RESETREAS bit 1 = **watchdog**. Sequence: phone wedged → `writeValue()`
> blocked inside `sendAclPkt()` → nobody feeds NRF_WDT (main loop stuck)
> → 5 s WDT fired → radio died → phone logged LINK_SUPERVISION_TIMEOUT as
> a SYMPTOM. The phone was never the (only) problem — **the device was
> killing itself**.
>
> **FW 5.60 = WDT survival:** mbed::Ticker feeds `NRF_WDT->RR[0]` every
> 500 ms from interrupt context while FT_STREAMING (nRF52 WDT can't be
> reconfigured once started — ISR feed is the only option). Scoped to FT
> only; WDT still guards all other paths. Abort threshold 3 s → 8 s;
> block > 1 s → 500 ms deep-throttle hold.

> **2026-08-31 — mechanism CONFIRMED in library source:** `HCI.sendAclPkt()`
> busy-blocks (`while (_pendingPkt >= _maxPkt) poll();`) when the nRF52
> controller TX queue is full, and `ATT.handleNotify()` **discards** its
> return value → `writeValue()` can freeze the FT loop for seconds with zero
> error signal. 244 B @ 30 ms outpaces the ~40–60 ms connection-event drain,
> so the device sits permanently at the congestion edge; when the S22
> controller/host suffocates and delays LL ACKs, the phone kills the link
> with `LINK_SUPERVISION_TIMEOUT` (0x08) after its supervision window.
> ACK-loop (5.50–5.56) and turbo push (5.58) both failed because they never
> addressed the queue-pressure trigger.
>
> **FW 5.59 = Burst & Breathe + forensics:**
> - 10 chunks @ 30 ms → 100 ms breathe gap (effective ~40 ms/chunk)
> - `writeValue()` block timing on every chunk: `ft_txblk` JSON > 50 ms,
>   adaptive extra breathe > 100 ms, clean `ft_abort("tx_blocked")` > 3000 ms
> - Supervision timeout hint 5 s → 10 s (recovery room for transient stalls)
> - Wire protocol unchanged — App 1.20 needs no update

### Meta

| Field | Value |
|-------|--------|
| **Case ID** | `TC-2026-08-26-001` |
| **Title** | FT blocking $\rightarrow$ S22 BLE Buffer Exhaustion $\rightarrow$ WDT Reboot |
| **Objective** | Complete batch download on S22 without `LINK_SUPERVISION_TIMEOUT` or reboots |
| **Baseline under test** | FW **5.50** · App **1.20** (ACK loop) · HW **v4.2** |
| **Priority** | **P0** |
| **Opened** | 2026-08-26 |
| **Owner** | Lead Systems Coordinator |
| **Parent** | TC-2026-08-25-001 (FT stall on S22) |

### Timeline (2026-08-24)

#### JP report — FW 5.32–5.33

```text
JP: FW 5.32 pushed. Button does not trigger ARMED.
    After booting, device enters ARMED without being asked.
    After `i` (SLEEP), P0.02 voltage = 0.4 V.
    After booting, P0.02 voltage = 1.8 V.
```

#### Root cause

LDC1612 was **still being initialized** in `setup()`:
- `g_ldc.begin()` started conversions → DRDY fires every ~819 µs
- `g_ldc.enable_interrupt()` reconfigured P0.02 as LDC INTB input
- LDC INTB open-drain output physically wired to P0.02, actively pulling LOW

All three symptoms explained:
1. **Auto-arm on boot** — INTB LOW → LATCH bit set → `WAKE_LDC` → auto-arm
2. **0.4 V after `i`** — LDC running, DRDY pulls INTB LOW ~98 % of the time
3. **Button dead** — pin already driven LOW by LDC; pressing button changes nothing

#### Fix — FW 5.34 (commit `54f4b7d`)

1. Removed `g_ldc.begin()` + `g_ldc.enable_interrupt()` from `setup()`
2. LDC lazy-init: `g_ldc.begin()` called on-demand only for `c` (recal) / `z` (diag)
3. `g_ldc_inited` flag guards all other `g_ldc.*` calls
4. `WAKE_LDC` → `WAKE_BUTTON` rename
5. Removed `g_ldc.force_recalibrate()` on SLEEP transition
6. Comments updated: P0.02 = "piezo button" not "LDC INTB"

### Expected bench results (FW 5.34)

- P0.02 idle voltage: **3.3 V** (pull-up, no LDC loading)
- P0.02 pressed voltage: **~0 V** (button to GND)
- Boot → **SLEEP** (no auto-arm — LATCH not set, pin is HIGH)
- Button press in SLEEP → **ARMED**
- `i` → SLEEP → button press → **ARMED**
- System Off → button press → **wake** (GPIO SENSE_LOW, no DRDY race)
- `?` status: `ldc:false,ldc_raw:0` (LDC not initialized)

### 2026-08-31 — FW 5.59 Burst & Breathe (commit `d501062`)

**Stall history (this case):** chunk 0 (5.54 L-STREAM) → ~50 (5.57 open-loop,
20 B) → ~136 ≈ 33 KB (5.46 @ 50 ms **and** 5.58 @ 30 ms turbo). Faster big
chunks got furthest → per-packet queue pressure, not per-byte volume, is the
trigger. Library mechanism: see blockquote above (verified in
`.pio/libdeps/nicla/ArduinoBLE/src/utility/HCI.cpp:638` + `ATT.cpp:624`).

**FW 5.59 changes (`file_transfer.cpp`, `sgc_service.cpp`, `config.h`):**

| # | Change | Why |
|---|--------|-----|
| 1 | Burst gate: 10 × 30 ms, then 100 ms breathe (~40 ms/chunk) | Keeps `_pendingPkt` off the `_maxPkt` ceiling; Android drains GATT/HCI + does LL housekeeping in the gaps |
| 2 | `writeValue()` timed per chunk | First-ever visibility into the silent block (previously invisible — return discarded by lib) |
| 3 | `ft_txblk` JSON when block > 50 ms | Serial forensics: shows phone falling behind seconds before supervision timeout |
| 4 | Adaptive +100 ms hold when block > 100 ms | Self-throttle before the wedge cascades |
| 5 | `ft_abort("tx_blocked")` when block > 3000 ms | Clean FT_ERROR to app beats zombie 5 s timeout |
| 6 | Supervision hint 500 → 1000 (10 s) | Transient wedges < 10 s recover instead of dying |

**Bench test protocol (JP, S22 + serial monitor open):**

1. `git pull && pio run -t upload -e nicla` → expect `ver=5.59`
2. App 1.20 → Download Latest Run → watch **both** phone log and device serial
3. **PASS:** `ft_done` with CRC OK on phone; serial shows `ft_prog` … `ft_done`
4. **If it stalls again, the serial now tells the story:**
   - `ft_txblk` lines with rising `blk_ms` **before** the stall → phone ACK
     starvation confirmed → next lever: `FT_BREATHE_MS` 100→200 ms or
     `FT_BURST_COUNT` 10→5 (both one-line changes)
   - `ft_abort,"tx_blocked"` → device detected the dying link cleanly
     (expected to replace the zombie timeout)
   - **No** `ft_txblk` at all + stall → block hypothesis wrong → next:
     phone-side host drain (app) or resume-capable batch protocol
5. Note the negotiated MTU/CI if visible in logcat

**Next levers (ordered):** (a) tune breathe/burst constants; (b) CI request
32–48 → shorter min; (c) resume-capable batch download (CMD_START with
offset — protocol + app change); (d) vendor patched ArduinoBLE exposing
`_pendingPkt` so pacing keys off real queue depth.
- `c` / `z` commands: LDC lazy-inits, diagnostics work (but P0.02 recontaminated — reboot after)

### Verdict

| Field | Value |
|-------|--------|
| **Result** | **PASS** — smoke green (run_20260824_1715, COM3) |
| **Code review** | ✅ Coordinator — full pin audit, all Arduino API calls verified |
| **Build** | ✅ JP built + flashed 5.37 |
| **Bench** | ✅ Boot→SLEEP, button→ARMED, P0.02 voltage correct |
| **Smoke** | ✅ sensor_injection + flash + S04 ALL PASS |
| **Notes** | • Button press needed to wake from System Off (not SLEEP) — tests send `i` for SLEEP wake |
|  | • SLEEP_SYSTEM_OFF_MS reverted to 1h (30s caused System Off during test setup) |
|  | • SSH key not yet set up — results push failed, results local only |
| **Next** | S03 stream run; SSH key setup (`ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_sgc`) |

#### Iteration 2 — FW 5.34 (removing begin() was not enough)

```text
JP: 5.34 flashed. Device still starts in ARMED.
    P0.02 = 1.8V at boot, 0V after 'i'.
    Button still dead.
```

Root cause: LDC1612 chip **powers up in active mode by default** — DRDY fires
immediately and INTB open-drain pulls P0.02 LOW, even without `g_ldc.begin()`.
The chip's power-on default has CONFIG bit 0 = 1 (active). Removing `begin()`
from setup was necessary but not sufficient — the chip was still actively
driving INTB LOW.

#### Fix — FW 5.35 (commit `0a64276`)

Added `LDC1612::quiesce()`:
- `Wire.begin()` + `Wire.setClock(100kHz)`
- Ping I2C address 0x2B (skip if not present)
- Write `CONFIG = 0x0000` (sleep mode — bit 0 = 0 stops conversions)
- Read `DATA0_MSB` + `DATA0_LSB` to clear any pending DRDY
- INTB releases HIGH (pull-up, no more DRDY)

Called in `setup()` **before** `g_button.begin()` to free P0.02 for the piezo button.

Boot log should show: `{"ev":"init","sub":"ldc_quiesce","ok":true}`

#### Iteration 3 — FW 5.35 (LDC quiesced, still broken)

```text
JP: LDC1612 physically removed from board. 5.35 flashed.
    Device still starts in ARMED.
    P0.02 = 1.8V at boot, 0V after 'i'.
    Button still dead.
```

Root cause: **Arduino pin mapping bug** — Nicla Sense ME PinDescription array maps:

```text
Arduino pin 0  → P0.10 (GPIO3)
Arduino pin 1  → P0.09 (GPIO2/RX)
Arduino pin 2  → P0.20 (GPIO1/TX)    ← WRONG PIN!
Arduino pin 3  → P0.23 (SCL1)
...
Arduino pin 10 → P0.02 (A0)          ← CORRECT PIN
```

Piezo button code used `PIN = 2` for everything:
- `pinMode(2, INPUT_PULLUP)` → configured **P0.20** (TX), not P0.02
- `attachInterrupt(digitalPinToInterrupt(2), ...)` → interrupt on **P0.20**
- `NRF_GPIO->IN >> 2` → read **P0.02** (correct, but no pull-up, floating!)

P0.02 was floating (1.8V at boot, 0V noise after `i`).
P0.20 (TX) toggled during serial comms → spurious ISR fires → `m_pressed = true` → auto-arm.

The LDC1612 code had the **same bug** (`INTB_PIN = 2`), but it used direct
register access for wake (`PIN_CNF[2]`), so it partially worked.

#### Fix — FW 5.36 (commit `343102c`)

Split into two constants:
- `ARDUINO_PIN = 10` (A0 = P0.02) for `pinMode` / `attachInterrupt`
- `NRF_PIN = 2` (P0.02 physical) for `NRF_GPIO->IN` / `PIN_CNF` register access

Also fixed LDC1612 `INTB_PIN` (was 2, now 10).

#### Iteration 4 — FW 5.36 (LDC removed, still broken)

```text
JP: LDC1612 physically removed from board. 5.36 flashed.
    Device still starts in ARMED. P0.02 = 1.8V at boot, 0V after 'i'.
    Button still dead.
    "Are you telling me that we spent a week debugging the LDC behaviour,
     and the problem was a pin definition? Now check all the PIN definitions!"
```

Root cause: **Same pin mapping bug in 3 more files.** Full audit of every
`pinMode` / `digitalWrite` / `digitalRead` / `attachInterrupt` call:

| File | Code | Got | Should be | Impact |
|------|------|-----|-----------|--------|
| `spi_flash.cpp` | `pinMode(26)` | out of range | `16` (CS FLASH) | DP never worked |
| `spi_flash.cpp` | `pinMode(3)` | P0.23 (SCL1!) | `9` (SCK P0.11) | toggled I²C SCL |
| `spi_flash.cpp` | `pinMode(4)` | P0.22 (SDA1!) | `8` (MOSI P0.27) | toggled I²C SDA |
| `main.cpp` `w` | `pinMode(22)` | out of range | `4` (SDA1) | silent no-op |
| `main.cpp` `w` | `pinMode(23)` | out of range | `3` (SCL1) | silent no-op |
| `main.cpp` `?` | `digitalRead(10)` | P0.02 (button!) | no Qi hw | wrong `qi` field |

#### Fix — FW 5.37 (commit `11e2839`)

Full pin audit + fix all remaining Arduino API pin mapping bugs:
- `spi_flash.cpp`: 26→16, 3→9, 4→8 (both enter + release DP)
- `main.cpp w`: 22→4, 23→3 (SDA1/SCL1 toggle)
- `main.cpp qi`: `digitalRead(10)` → `false` (no Qi hardware)
- Verified correct (no change): `PIN_CNF[2]`, `PIN_CNF[14]`, `PIN_CNF[19]`,
  `NRF_P0->OUTCLR`, `NRF_GPIO->IN >> 2` — all direct register access, correct

### Pin map reference (Nicla Sense ME)

```
Arduino  0 → P0.10 (GPIO3)          Arduino 10 → P0.02 (A0 / button)
Arduino  1 → P0.09 (GPIO2/RX)       Arduino 11 → P0.30 (A1)
Arduino  2 → P0.20 (GPIO1/TX)       Arduino 12 → P0.19 (ESLOV/LED)
Arduino  3 → P0.23 (SCL1)           Arduino 13 → P0.18 (Reset BHI260)
Arduino  4 → P0.22 (SDA1)           Arduino 14 → P0.14 (BHI260 INT)
Arduino  5 → P0.24 (GPIO0)          Arduino 15 → P0.25 (BQ25120 CD)
Arduino  6 → P0.29 (CS BHI260)      Arduino 16 → P0.26 (CS FLASH)
Arduino  7 → P0.28 (MISO)           Arduino 17 → P0.31 (CS BHI260 alt)
Arduino  8 → P0.27 (MOSI)
Arduino  9 → P0.11 (SCK)
```

### Timeline (2026-09-02) — native path + post-abort WDT reboot

#### JP bench — FW 5.67 + App 1.31 (Nordic BleManager, S22)
1. Press "Native Download Missing" → **no visible feedback** (native batch
   reports only at end; connect retry cycle can run 20–60 s silently).
2. After restarting both sides: wedge at chunk 118 → 3× `ft_txfail`
   (blk 2001 ms) → clean `ft_abort("tx_blocked")` → **reboot `rr:2`**
   despite the 5.67 3 s grace.

#### Root causes
1. **Post-abort WDT reset:** after `tx_blocked`, FW wrote the ERROR notify
   into the wedged TX queue and kept polling Cordio. Each wedged
   `sendAclPkt` blocks ~2 s (patched bound); ≥3 queued packets in one
   `BLE.poll()` > 5 s WDT → reset. The 3 s grace only covered ~1.5 packets.
2. **Mute UI:** zero native→Dart progress until batch end.

#### Fixes
- **FW 5.68 (`556f827`):** on `tx_blocked`, skip ERROR notify and force
  `BLE.disconnect()` — controller flushes TX queue (patched lib zeroes
  `_pendingPkt` on disconnect), loop is free, phone sees the drop →
  auto-resume reconnects sooner. Grace 3 s → 6 s.
- **App 1.32 (`556f827`):** `sgc_native_ble_events` channel — Kotlin log
  lines + throttled FT byte progress (~250 ms) → `_downloadStatus` /
  `_downloadBytes` / `_downloadRunSize`. Listener cleared in `finally`.
- Also today: 1.31 build fixes (`720d840`, `83671ac`) — `ReadResponse`
  has no `.value`; no-arg `await()` is void (TimeoutableRequest); typed
  `await(ReadResponse::class.java)` + `rawData?.value`.

#### Bench expectation (5.68 + 1.32)
wedge → `tx_blocked` → `ble_disc` shortly after → **no `rr:2` boot** →
native resume (offset ≈ 28 KB) → `ft_resume` → completion → `ft_done`.

#### Bench outcome (2026-09-02, 19:20 UTC) — FW 5.68 VERIFIED + resume-reconnect gap
- Wedge @ chunk 62 (off 15004) → 3× `ft_txfail` → `ft_abort("tx_blocked")` →
  **NO rr:2 reboot** ✓ — device recovered (`ble_adv`/`ble_recover desync`).
- Phone: attempt 1 lost link at 14036 B (status 8) → **resume fired** ✓ but
  attempts 2–5 never reached the device (no `ble_conn`): attempt 2 hit the
  device post-abort recovery window (status 147); attempts 3–5 ran with the
  **app backgrounded / screen off** (logcat `VRI stopped`, `surfaceDestroyed`)
  → Android BLE throttling → 147 / -5 timeouts.
- **App 1.34 (`10d119d`):** KEEP_SCREEN_ON + partial WakeLock during native
  batch; resume attempts 6→10; connect retry 3/250→5/500; 1 s settle after
  advertisement sighting before connectGatt.
- Bench expectation: wedge → tx_blocked → ble_disc (no reboot) → resume
  reconnect succeeds within 2–3 attempts → `ft_resume` → `ft_done`.

#### nRF Connect reference run (2026-09-02, 21:44 UTC) — WEDGE IS NOT APP-SPECIFIC
- mcp downloaded a full run on FW 5.68: `ft_done 39372 B, chunks:163,
  blocks:0 blk_ms:0, 9.86 s` — SAME params as our sessions (CI 60 ms,
  supervision 10 s, 244 B chunks), Garmin connected the whole time.
- **mcp's own earlier link (clientIf=13) ALSO died status=8 at 21:44:12** —
  the wedge is stochastic, not app-specific. mcp just recovers by hand.
- Remaining mcp-vs-us delta: Samsung **Freecess** (visible in logcat:
  `BlueToothConnectedFilter`) can CPU-throttle our process mid-transfer →
  host HCI drain slows → controller RX backs up → LL stall → wedge.
- **App 1.35 (`275dffd`):** foreground service (`connectedDevice` type) +
  ongoing notification during native batch → foreground scheduling priority.
  Foundation for Phase 3 Auto-Sync.

#### Resume-reconnect root cause FOUND (2026-09-02, 20:00 UTC) → FW 5.69
- App 1.35 bench: wedge @ chunk 90 (20812 B phone / 21780 device) → clean
  tx_blocked, no reboot ✓ — but attempt 2 connect failed 147, sub-retry -5.
- **Root cause:** 5.68's `BLE.disconnect()` on a wedged link sends an LL
  terminate the phone never ACKs → nRF52 controller keeps the half-open
  link in its single connection slot until device-side supervision (10 s)
  frees it. Every phone resume inside the window bounces; mcp's manual
  +8–13 s reconnects land after it. Explains ALL resume failures today.
- **FW 5.69 (`f1003d3`):** tx_blocked → deferred hard radio restart
  (V5.04 `request_ble_radio_restart` → BLE.end/begin, proven V5.15 path).
  Pristine radio + ADV in <1 s; phone attempt 2 (+4 s) finds connectable
  device. WDT grace 8 s covers the restart.
- Bench expectation: wedge → tx_blocked → `ble_radio ok:1` → attempt 2
  connects (`ble_conn`) → `ft_resume` → `ft_done`.

#### FW 5.69 bench (2026-09-02, 20:23 UTC) — ALL 5 RUNS DOWNLOADED ✓ but 27 s/wedge
- Auto-resume worked end-to-end: 5/5 runs, ~3 min total, multiple resumes.
- But: `ble_radio ok:0 retry:1 reboot:1` — BLE.begin() failed on the still-
  tearing-down controller → NVIC reboot (rr:4) → old WDT fired mid-boot
  (rr:2) → two boots ≈ 27 s per interruption.
- Pre-reset symptom "phone doesn't see device" despite ble_adv: stale
  half-open link occupying the single slot blocks connectable ADV until
  supervision frees it — consistent with the single-slot theory.
- **FW 5.70 (`d78420c`):** lazy recovery — on tx_blocked touch the link NOT
  AT ALL (no notify/disconnect/restart); device-side supervision kills the
  wedged link naturally → on_ble_disconnected re-advertises → phone resume
  on freed slot. Supervision hint 10 s → **4 s** (resume works → fast free
  beats recovery room; LL empty PDUs keep quiet links alive, only dead
  links fire). FT_TX_FAIL_ABORT 3 → 2 (zero recoveries ever observed).
- Target: ~8 s per interruption (~6.6 s detect+hold → ~4.3 s; supervision
  fires ≈ same window; phone attempt 2 at +2 s + scan + 1 s settle →
  connect ≈ +7 s).

#### Second clean mcp run (2026-09-03, 08:17 UTC, FW 5.70) → chunk-size A/B
- mcp: run 0 full 39372 B, 163 chunks, blocks:0 blk_ms:0, 9.8 s — SECOND
  clean mcp run. Our app: 6+ wedges. No longer stochastic.
- **Key unknown:** did mcp ever negotiate MTU 247? If not, Cordio silently
  truncates writeValue(244) → 20 B notifies → mcp "reliability" = 4.97-era
  20 B profile; our app always gets 244 B. Would explain everything.
- **FW 5.71 (`3e177f3`):** CMD_START chunk-size code — len 4 [0,lo,hi,code]
  or len 8 [0,lo,hi,code,off0..3]; 1=128, 2=64, 3=20 B, else 244 B;
  echoed in ft_start JSON. JP can A/B from nRF Connect without app changes.
- A/B protocol: (a) mcp + explicit MTU 247 request, 244 B → wedge? (b)
  mcp + code 3 (20 B) → clean? (c) mcp + code 1 (128 B) → ?

## 2b. Closed / parked this session

### TC-2026-08-14-002 — BLE zombie / no ADV (5.03)

| Field | Value |
|-------|--------|
| **Result** | **PASS (5.03)** — `sgc_ble_force_recover` fixed zombie link + ADV restart |
| **Fix** | `sgc_ble_force_recover` in `ble/sgc_service.cpp` |
| **Closed** | 2026-08-14 |

### TC-2026-08-14-001 — FW 5.02 boot + smoke

| Field | Value |
|-------|--------|
| **Result** | **PASS (smoke)** — core/BLE not claimed |
| **Evidence** | `run_20260814_0829`; boot ver 5.02; chip 2048 / 8 slots; S04 99.5 fps |
| **Note** | First harness attach failed until HW reset (see D-006). |

---

## 3. Component Logs

### 3.1 Hardware serial output (5.03 BLE OK)

```text
{"ev":"flash_map","size_kb":2048,"slots":8,"slot_kb":244,"cfg":2080768,"idx":2084864,"preroll_end":81920}
{"ev":"raw_store","ok":1,"slots":8,"slot_kb":244,"chip_kb":2048,"runs":1}
{"ev":"st","from":"SLEEP","to":"IDLE"}
{"ev":"ble_adv","why":"state"}
{"ev":"ble_recover","why":"state","st":"IDLE"}   # boot double-fire — cosmetic
{"ev":"ready","st":"IDLE","runs":1,"ver":"5.03","used_pct":12}
{"ev":"ble_conn","addr":"43:80:d1:f0:fb:e9"}
{"ev":"ble_adv","why":"disconnect"}
{"ev":"ble_disc","st":"IDLE"}
… multiple app reconnect cycles …
{"ev":"timeout","from":"IDLE","to":"SLEEP"}
{"ev":"ble_recover","why":"sleep","st":"SLEEP"}
{"ev":"st","from":"SLEEP","to":"IDLE"}
{"ev":"ble_recover","why":"serial_i","st":"IDLE"}
```

### 3.2 Flutter / logcat

```text
JP: after hard reset, SGC found; close/reopen app many times — OK.
(First attempt without phone BLE/app restart was a false start.)
```

### 3.3 PC harness summary

```text
run_20260814_0900  FW 5.03  Harness v2.26.0
test_sensor_injection  ✅ 21/21
test_flash             ❌ 9/10  (U12 PASS; U13 step2 got cmd B instead of status)
test_s04_bhy2_rate     ✅ 1/1   S04 99.7 fps store=raw we=0 ver=5.03
```

### 3.4 Attachments / paths

| Kind | Path |
|------|------|
| Smoke 0900 | `unit_tests/tmp_test_results/run_20260814_0900*` |
| Prior 5.02 | `run_20260814_0829` (PC; may not be in workspace) |

---

## 4. Open defects / risks (coordinator board)

| ID | Sev | Symptom | Suspected area | Status |
|----|-----|---------|----------------|--------|
| **D-008** | **P0→CODE MITIGATED** | `ble_radio` restart returns ok=0 after heavy traffic | Heavy 244 B FT; BLE.end() can't drain Cordio pending HCI | **T-008c: NVIC_SystemReset fallback (5.16)** — self-recovery confirmed. Zombie path fixed by T-008b. Active-traffic path reboots. |
| **D-009** | **P0→CLOSED** | FT stall after flash (warm reset) — Cordio statics survive | nRF52 warm reset | **T-008a warm_deinit (5.15) CONFIRMED** — BLE ok:1 on every warm boot |
| D-001 | P0 | Boot crash after BLE on 5.01 index load/persist | `raw_run_store.cpp` 4 KB stack | **PASS on bench 5.02** (smoke) |
| D-002 | P1 | BLE FT hang if BHY2 SPI during transfer | shared SPI0 | Fixed 4.97; regression-watch |
| D-TIME | P2 | Run ts_utc stuck at 1970 | ABC0 no-op / create_run | **PASS bench** — after phone connect, S03 run time correct (4.99 path) |
| D-003 | P2 | S03 dump `bad_id` intermittent | host parse race | Mitigated SIM 2.50.0 retry |
| D-004 | P2 | U19 inject flake historically | serial/inject | **PASS** on 0829 |
| D-005 | P3 | No automated serial test for BLE hold-idle / re-ADV | BLE 5.00 | Manual only |
| D-006 | P0 | Zombie BLE connected: no auto-sleep, not scannable | `sgc_ble_force_recover` | **CLOSED PASS on 5.03** |
| D-007 | P2 | U13 step "Record initial runs" got `cmd B p=0` instead of `status` | harness serial RX race | **WATCH** — flake on 0900; **PASS 10/10** on 0911 |

---

## 5. Coordinator task queue

| ID | Status | Notes |
|----|--------|-------|
| T-001 | **DONE** | `sgc_ble_force_recover` + FW 5.03 |
| T-002 | **DONE** | wired SLEEP/IDLE/serial `i`/desync/main POST_RUN |
| T-003 | **PENDING** | catalog baseline 5.03 + harness 2.27 note |
| T-004 | **DONE** | harness → `tmp_test_results/` auto staging (v2.27) |
| T-005 | **OPEN** | harden U13/`?` expect against stray `cmd B` (D-007) |
| T-006 | **DONE** | PC→VPS `push_test_results.ps1/.sh` via SSH **key** (no password); DeepSeek + review/push |
| **T-008** | **DONE** | FW 5.15 T-008a+b + FW 5.16 T-008c — confirmed on bench |
| **T-008a** | **DONE ✅** | warm_deinit on every warm boot → BLE ok:1 (rr:2, rr:4, rr:6) |
| **T-008b** | **DONE ✅** | zombie: ok:1 retry:0. Active traffic: ok:0 → T-008c catches. |
| **T-008c** | **DONE ✅** | NVIC_SystemReset fallback — `reboot:1` → clean recovery → FT OK |
| **T-009** | **DONE** | 5.16 validated: S03 OK, FT OK after self-recovery. Full smoke not run. |

### 5.03 implementation review (coordinator)

**Files:** `config.h`, `ble/sgc_service.h`, `ble/sgc_service.cpp`, `main.cpp`

**Verified in tree:**
- `FW_VERSION "5.03"`
- `sgc_ble_force_recover`: FT abort → stop ADV → disconnect+≤3 poll → clear flags → ADV iff IDLE/POST_RUN → `ble_recover` JSON
- IDLE/POST_RUN: keep link only if `g_central_connected && BLE.connected()`; else force_recover
- SLEEP: `force_recover("sleep")` (ADV off)
- Serial `i`: `force_recover("serial_i")` even if already IDLE
- Main loop desync heal before hold refresh
- Bare `BLE.advertise()` removed from POST_RUN in main
- Not called from `on_ble_disconnected` (no recursive disconnect)

**Residual risk:** double ADV restart if disconnect handler fires during force_recover poll — should be idempotent.

**Not claimed green** until JP flashes and scans.

#### JP bench 2026-09-08 15:24 UTC — FW 5.74 + App 1.41 (first field data)

**Pre-bench:** device in SLEEP, not scannable from phone → JP hard-reset the
device to start. Same signature as the known "phone holds stale link" issue.
New suspect: App 1.38 auto-reconnect re-establishing the link after the
batch-end intentional disconnect, then idling connected (1.35 foreground
service keeps process alive). Next-occurrence protocol: force-stop app →
rescan → BT toggle → report which step frees it BEFORE hard-resetting device.

**Wedge + recovery (serial):**

```text
{"ev":"ft_prog","off":36300,"sz":39044,"chunks":150,"blocks":0,"blk_ms":0,"recov":0,"ms":9016}   ← 93 % through, 9 s in
{"ev":"ft_txfail","chunk":150,"off":36300,"fails":1,"blk_ms":2001}
{"ev":"ft_txfail","chunk":150,"off":36300,"fails":2,"blk_ms":2001}
{"ev":"ft_abort","reason":"tx_blocked","off":36300,"sz":39044,"chunks":150}
{"ev":"ble_adv","why":"desync"}
{"ev":"ble_recover","why":"desync","st":"SLEEP"}
```

- Designed failure path ran CLEAN end-to-end: wedge → 2×2 s fails → abort →
  desync recovery → SLEEP + re-ADV. **No rr:2** — first wild run of the full
  5.73/5.74 chain.
- Phone logcat (17:24 local = 15:24 UTC): two `btif_gattc_open_impl` to the
  same public addr **11 s apart** (17:24:27.505 → 17:24:38.277) — initial
  connect + post-wedge resume candidate. `phy=1` at open (**LE 1M**) — first
  PHY datapoint. Two other opens to random addresses 230 ms apart = ambient.
- **OPEN:** did the run complete after the 38.277 reconnect? Need serial tail
  (`ft_start` resume @36300 → `ft_done` + CRC) + app save confirmation.
- **OPEN:** map logcat addresses to SGC MAC; capture `onPhyUpdate` lines.
- Chunk-size datapoint (JP): historically tried 244 → … → 28 B — smaller was
  WORSE; cadence 60 → 120 ms no effect. Confirms stochastic phone-side link
  failure model, not congestion. Strategy unchanged: fast recovery; KPI =
  seconds per interruption.
- **Node plan:** JP installing Windows Hub node on PC → coordinator gets adb
  logcat + serial capture + pio flash/install loop directly.

#### CORRECTION 15:38 UTC — NOT clean: silent rr:4 + double boot (JP full log)

Earlier "failure path ran clean" read was WRONG. Full serial shows:

```text
ft_abort(tx_blocked) → ble_adv(desync) → ble_recover(desync,SLEEP)
{"ev":"boot","ver":"5.74","rr":4,"wrs":"UNKNOWN","fcp":1}     ← silent SOFT reset
  boot #1: … ble_warm_deinit(rr:4) → {"ev":"init","sub":"ble"     ← TRUNCATED
{"ev":"boot","ver":"5.74","rr":2,"wrs":"UNKNOWN"}              ← WDT mid-BLE.begin
  boot #2: clean … raw_prep slot 5 (244 KB erase) … ble_conn(65:63:fc:51:25:9d)
{"ev":"ft_resume","off":35332,"chunk":146} → ft_start → chunk 150 in 222 ms → …
```

Decode (source-verified):
- **rr:4 fcp=1:** GPREGRET 0x01 = fcp window EXPIRED sentinel (`fcp()` at
  main.cpp:1394) → crash happened **≥20 s post-abort** (window 20 s, armed
  at FT exit, file_transfer.cpp:309). No `ble_radio`/`ble_conn`/`reboot` JSON
  before it → not a preamble'd NVIC site. Prime suspect: **zombie path**
  (30 s idle → request_ble_radio_restart("zombie") → begin fail → NVIC at
  sgc_service.cpp:378) after a missed disconnect event. Timing fits (30 s
  zombie > 20 s fcp). NEED: confirm log contiguity between ble_recover and
  boot (any dropped ble_conn/ble_radio lines?).
- **Boot #1 BLE.begin() hang:** `init sub:ble` split-print truncated → WDT
  fired mid-begin() → rr:2. 5.69-era rr:4→rr:2 double boot is back.
- **Resume worked:** app re-requested @35332 (chunk 146), 150 re-reached in
  222 ms. Tail after last SND_DATA 150 unknown (second cycle? ft_done?).
- **Total >60 s** = 5 s abort + ≥20 s mystery gap + 5 s WDT + boots + phone
  reconnect latency. KPI far off target; two reboots per wedge unacceptable.

FW 5.75 plan: (1) fcp window 20 s → 120 s; (2) zombie de-escalation → lazy
re-ADV, no BLE.end/NVIC; (3) boot hardening: WDT feeds + per-step boot
timing, 300 ms settle before BLE.begin on warm boots; (4) optional FW-side
idle-connected timeout in SLEEP (~3 min) vs app-held stale links.
App 1.42 plan: batch end tears down BOTH stacks (native GATT + Dart link),
suppress 1.38 auto-reconnect via batch-complete flag, stop foreground
service. (Force-stop test CONFIRMED app holds link; 1.41 = single-connection
batch per git 50fdc12.)
PHY: no onPhyUpdate lines → LE 1M end-to-end; variable eliminated.

---

## 6. Case history index

| Case ID | Date | Title | Result | Archive |
|---------|------|-------|--------|---------|
| TC-2026-08-14-001 | 2026-08-14 | FW 5.02 boot + smoke | **PASS smoke** | §2b |
| TC-2026-08-14-002 | 2026-08-14 | BLE zombie / no ADV | **PASS 5.03** | §2b (closed) |
| TC-2026-08-15-001 | 2026-08-15 | FT download hang + `ble_radio` regression | **PASS (5.16)** | history |
| TC-2026-08-22-001 | 2026-08-22 | LDC stale-status race + System Off wake + boot diagnostics | **PASS (5.27)** | history |
| TC-2026-08-24-001 | 2026-08-24 | LDC1612 still in boot path — button dead + auto-arm | **PENDING JP BENCH (5.34)** | §2 (active) |
| run_20260814_0900 | 2026-08-14 | Smoke 5.03 | inject+S04 PASS; flash **9/10** U13 flake | `tmp_test_results/` |
| run_20260814_0911 | 2026-08-14 | Smoke 5.03 | **ALL PASS** 21+10+1 | `tmp_test_results/` (SSH push OK) |
| 2026-08-18 FT | 2026-08-18 | FW 5.14 FT warm-reset stall + hard-reboot success | FT ✅ after POR; `ble_radio` ❌ | §2 + §3 |

Snapshots: `test_ledger/history/`  
Optional deep-dive cases: `test_ledger/cases/TC-….md`

---

## 7. Quick commands (PC)

```powershell
# Flash
cd F:\Documents\Progetti\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Firmware_implementation
pio run -t upload -e nicla
pio device monitor -b 115200

# Smoke (run + auto-push results to the coordinator)
cd ..\..\..\unit_tests   # adjust to unit_tests path on PC
.\run_smoke.ps1          # -SkipPush to skip push, -Clean to prune old runs first
# Core tier: .\run_core.ps1
```

Path note: JP PC canonical tree may be `F:\Documents\Progetti\ski-gate-chrono`; workspace mirror is under OpenClaw `ski_gate_chrono/`.
## L-STREAM (v5.54) Architecture
Consolidated FT characteristics into single Stream characteristic to eliminate S22 GATT stall.

### 2026-09-08 16:34 UTC — bench tail + FW 5.75 implemented

**JP answers:** (1) "boot rr:4 appears nowhere" — it IS in his 15:38 paste:
`{"ev":"boot","ver":"5.74","rr":4,"wrs":"UNKNOWN","fcp":1}` (shorthand "rr:4" =
the `rr` field). (2) Tail after resume: **RUN COMPLETED** — chunks 158-162,
`ft_done crc=1820547675 sz=39044 chunks=162 ms=986`, then clean
`ble_adv(disconnect)` + `ble_disc(SLEEP)`. Resume protocol proven end-to-end
again (986 ms, zero blocks). Run save in app unconfirmed. (3) logcat ≥17:24:38
lost.

**Double-boot root cause FOUND (surviving WDT):** the nRF52 WDT is NOT cleared
by warm resets — it keeps counting from the crashed session's last feed.
setup() only STARTS the WDT at the end (V5.09); nothing feeds the surviving
WDT during init → boot #1 (rr:4 path) was shot mid-`BLE.begin()` by the OLD
watchdog (rr:2). Boot #2 survived because preroll was already erased (faster
init). The 5.69-era "old WDT fired mid-boot" note described the same disease;
5.70 avoided it by not resetting — now that resets are back (5.73 forced
disconnect), the boot path must be robust.

**FW 5.75 (implemented, this commit):**
1. Boot WDT feeds at every init milestone (RESETREAS read, post-preroll,
   pre-BLE.begin, around 244 KB raw_prep, post) — kills the double boot
2. `request_ble_radio_restart()` prints `{"ev":"rr_req","why":...}` when
   queued — every deliberate rr:4 now has a preamble
3. Zombie arm prints `{"ev":"zombie_arm","idle_ms":...}` before requesting
4. fcp window 20 s → 120 s — forensics outlive zombie (30 s) + recovery
5. Silent rr:4 source remains UNKNOWN (zombie needs a ble_conn that isn't
   in the log; monitor fidelity unproven) — items 2-4 will name it next bench

**Bench ask (5.75):** pull → build → flash → same 5-run protocol. Watch:
`rr_req`/`zombie_arm`/`fcp` lines, wedge count, seconds wedge→ft_done, no
double boots. Pending: App 1.42 (batch-end dual-stack teardown + auto-reconnect
suppress + service stop).

### 2026-09-09 14:56 UTC — 5.75 bench: Freecess caught red-handed (logcat)

**Bench:** start-invisible again (app stale link — JP hardware-reset instead
of force-stop protocol; "? " worked over serial = device alive). After 2nd
reset, connect OK → native download → **wedge @ chunk 51/163, 3.0 s in** →
2× ft_txfail(2001) → tx_blocked → desync recover → SLEEP + re-ADV. Tail NOT
pasted — 5.75 forensics (rr_req/zombie_arm/fcp, single boot?) unverified.

**SMOKING GUN (logcat 16:53:48.255 local, filtered "BLE"):**
```
FreecessHandler$MainHandler.handleLcdOnFreeze
→ FreecessController.freeze
→ BlueToothConnectedFilter.updateBTUsingPackages
→ BluetoothAdapter.getHWUsingApps → getAdvertisingSetUids
→ (16:53:48.303) PowerManagerService: SetWakeLockEnableDisable uid=10535 disable=true
```
Samsung Freecess freezing an app on an LCD-state event; the BT filter only
checks **ADVERTISING** uids — a pure CENTRAL app (us) isn't on the list →
freeze proceeds. PWL line right after DISABLES wake-lock acquisition for
uid 10535. If 10535 = com.skigatechono.sgc_phone: Freecess froze our process
(mid-download), KILLING KEEP_SCREEN_ON(1.34) + partial wakelock(1.34) +
foreground service(1.35) protections — explaining why wedges survived all
three. Wedge model upgraded: stochastic → **screen-event-triggered Samsung
freezer**. Retroactively closes PHY/MTU/cadence/chunk-size chapters (never RF).

**Validation protocol (JP, no code needed):**
1. `adb shell dumpsys package com.skigatechono.sgc_phone | grep userId` → 10535?
2. Settings → Battery → Background usage limits → **Never sleeping apps** → add SGC
3. Re-bench with screen OFF mid-download. Prediction: wedge count ≈ 0.

> **🚨 RETRACTED for SGC (2026-09-09 17:39 local):** JP ran
> `pm list packages --uid 10535` → **`package:com.alibaba.aliexpresshd
> uid:10535`**. The freezer victim + wakelock-disable target was **AliExpress**,
> NOT our app. The "Freecess froze SGC mid-download" mechanism is UNPROVEN —
> we were watching the wrong uid. What survives: (a) wedges correlate with
> screen/LCD-transition events; (b) Freecess + BT-filter churn fires at those
> same moments (system-wide BT binder traffic); (c) AliExpress is BLE-active
> in background (scanner/GATT) — candidate RF/BT-stack interferer, not proven
> killer. Never-sleeping-apps test for SGC still worth running.
>
> **Revised A/B protocol:** (1) get SGC real uid (dumpsys userId) + owner of
> pid 27803; (2) `am force-stop com.alibaba.aliexpresshd` → re-bench →
> wedge rate; (3) if clean, re-enable → re-bench → wedge returns =
> interference confirmed.

#### 17:44 local — AliExpress UNINSTALLED, 1st A/B run: WEDGE PERSISTS

Serial: `SND_DATA 10/11/12 → ft_txfail chunk:12 off:2904 fails:1 blk_ms:2001
→ retry 12 → fails:2 → ft_abort tx_blocked → ble_adv desync →
ble_recover desync SLEEP`.

- **Wedge @ chunk 12 / 163 (~2.9 KB, earliest yet)** — with AliExpress
  GONE. Interferer theory WEAKENED (n=1, stochastic baseline unknown).
- **FW 5.75 graceful path: textbook.** 2×2 s fails → abort @ ~4 s → desync
  re-ADV → SLEEP. **No rr:2, no fcp, no zombie.** Interruption cost
  device-side ≈ 4 s + reconnect. This is the designed KPI behavior.
- Wedge chunk distribution now 12, 26, 51, 62, 90, 100, 118, 162 — spans the
  whole 163-chunk transfer ≈ 0–10 s of streaming ≈ 4–15 s post-connect.
  **Every wedge ever observed lives in the "fragile first 15 s of
  connection" window.** Churn-window hypothesis (settle >15 s before FT)
  still viable — queued as App A/B (1.37's 12 s probe was reverted at 1.39
> when the ghost-buffer bug landed; retry at 15–20 s).
- App-side outcome of this run — **RECOVERY LOOP BROKEN (JP 15:52): app
  attempted reconnect after the abort; device saw NOTHING.** No serial
  events at all after `ble_recover desync SLEEP` — no connection, no ft_start.
  The wedge→abort→re-ADV half worked; the phone→device reconnect step died
  silently. **Now the #1 user-facing bug: post-tx_blocked recovery never
  completes.** Candidate mechanisms: (A) phone stack holds half-open link
  until ~10 s supervision → Android swallows early reconnect attempts →
  count-based retry budget (1.34: 10×) exhausts inside the blind window;
  (B) SLEEP/desync ADV non-connectable or short-lived; (C) desync re-ADV is
  one-shot, phone scans after it stops. Discriminators queued: (1) fresh
  scan in app — device visible? (2) force-stop app → rescan (stale-link
  protocol); (3) serial tail after ble_recover — repeated ble_adv? (4) if
  bench_full.txt was recording: onClientConnectionState + GATT status codes
  around the abort.
  App fix direction (→ 1.42): reconnect only AFTER onConnectionStateChange
  DISCONNECTED + ~1–2 s grace; time-based retry budget (≥60 s), not
  count-based.

#### 17:50 — ROOT CAUSE OF "INVISIBLE DEVICE": ZOMBIE CONNECTION (device-side)

- dumpsys + serial cross-reference, two independent zombie episodes:
  - **17:22-17:36 (App 1.41):** connect 17:22:53 → wedge 17:23:23 (status=8,
    17 s in) → **13+ min of 147s** (Direct connection timeout = device
    never answers CONNECT_IND = not advertising).
  - **19:41 (App 1.42):** ble_conn 19:41:42 → phone ACL CONNECTION_TIMEOUT
    (0x08) at 19:41:55 (13 s in) → 147 storm 19:42:07–19:47+ → my force-stop
    19:49. **Serial: NO ble_disc EVER — the device never noticed the link
    died.** It still thinks it's connected → stopped advertising → invisible.
- **Mechanism:** device's dead-link detection only exists on the ACTIVE TX
  path (ft_txfail → tx_blocked → BLE.disconnect → re-ADV; worked 15:46/
  15:57). When the wedge hits EARLY/IDLE (during settle/subscriptions, before
  TX pressure), NOTHING on the device detects it: phone supervision fired
  (0x08) but device supervision/controller clock never did (frozen by the
  same radio wedge). Zombie until pin reset.
- Retroactively explains: every "device invisible, serial says sleep, must
  reset" incident; phone was innocent every time (no ghost link — connected: 0).
- **FW 5.76 fix list (concrete):** (1) app-level liveness watchdog — if
  connected and no GATT traffic for ~15–30 s → force BLE.disconnect() +
  re-ADV (never trust controller supervision); (2) unconditional re-ADV on
  every disconnect path; (3) `adv:1/0` in status JSON; (4) keep tx_blocked
  path for active TX.
- App 1.42 note: patch went to NativeBleDownloader.kt but the LIVE engine is
  NordicBleDownloader.kt (SGC_NORDIC tags) — port restart-from-scratch +
  teardown recipe to NordicBleDownloader next build.
- Bench reality: every wedge so far bricks the device until reset — 1.42's
  reconnect can't help until FW 5.76 makes the device re-advertise.

#### 15:57 — App+phone restarted → ABORTED RUN AUTO-RESUMED AND COMPLETED ✓; next run wedged @ 36

- After app + phone restart, connecting auto-triggered download of the
  previously aborted run → **SUCCESS**. Resume protocol + run persistence
  work end-to-end — currently gated on manual restarts (App 1.42 fix).
- Fresh run 2: wedge @ **chunk 36/163** (off 8712, sz 38950) → 2× ft_txfail
  (~2001 ms) → tx_abort tx_blocked → desync re-ADV → SLEEP. No rr:2, no
  fcp — 5.75 stays textbook.
- **AliExpress-uninstalled wedge tally: 2/2 runs wedge** (chunks 12, 36).
  Interferer theory DEAD as primary cause. Remaining viable hypothesis:
  churn-window — every wedge ever observed sits ≤ ~15 s post-connect.
#### 16:06 — JP DIRECTIVE: SIMPLIFY. Kill download rescue. Restart from scratch.

- **Policy change:** no resume-on-failure. Transfer fails → full teardown →
  reconnect → restart transfer from chunk 0. Rationale: resume saves ≤10 s
  (163 × 60 ms total) while adding offset bookkeeping, partial-buffer state,
  and bug surface; reconnect dominates recovery cost anyway. Today's rescue
  needed app+phone restarts = minutes — useless.
- **Implementation insight: APP-ONLY change.** FW 5.75 already implements the
  restart-from-scratch protocol (abort @4 s → re-ADV → SLEEP → fresh
  connection); FW keeps offset support but app stops using it. No reflash.
- **App 1.42 (simplified scope):** (1) on ANY transfer failure (ERROR
  packet / GATT disconnect / timeout): gatt.disconnect()+close(), full
  teardown; (2) wait for onConnectionStateChange DISCONNECTED + 1–2 s
  grace (out-wait phone 10 s ghost link); (3) reconnect → CMD_START offset 0
  → fresh 10 s transfer; (4) time-based budget (~3 attempts / 60 s), then
  clear per-run UI error + next run; (5) discard partial buffers; keep
  save-on-complete + auto-download-missing. CONNECT_SETTLE_MS stays 4 s.
- **KPI:** wedge → automatic full recovery → complete run, ≤30 s, zero
  manual steps. Bench n≥5 runs, count wedges + s-per-interruption.
- Root-cause hunt (settle 18 s A/B) queued AFTER 1.42 loop proven — it's
  one constant.
- Critical path: Windows node pairing (JP installing; npm PATH resolved
  after fresh terminal).

#### 17:15 — APP 1.42 BUILT, INSTALLED ON S22, COMMITTED (`e2da657`)

- Built on JP-PC via node (flutter 3.47.1, debug APK 175 MB), installed
  with `adb install -r` — **Success**. Commit: 2 files, +47/−30.
- Diff: MAX_FRESH_ATTEMPTS=3 (was 6 resume); buffer/CRC/expected fresh per
  attempt; CMD_START always offset 0 (3-byte); on failure: closeGatt (waits
  DISCONNECTED ≤4 s) → 2 s×attempt backoff → ADV wait ≤15 s (logs if not
  seen) → fresh connect; onPhyUpdate/onPhyRead logged + gatt.readPhy()
  post-MTU. FW unchanged (5.75).
- Node exec transport lessons (for future builds): NO hand-typed base64
  (write file → gateway `base64 -w0` → paste literal); no `$()` substitution
  in node commands; long builds = detached Popen + log polling; git commit
  via `-F` file (cmd eats quoted -m); findstr breaks on fwd-slash flags+paths
  (use py); `echo X >` adds trailing space (strip when parsing).
- Bench infrastructure live: serial logger pid 32816 →
  C:\Users\v17ni\sgc_serial.log (timestamped, auto-reconnect); logcat
  cleared; adb S22 attached.

#### 16:40 — **BENCH STATION LIVE: Windows node JP-PC fully operational**

- Node host 2026.9.3 paired + connected (needed: allowCommands config set by
  JP + nodes remove/re-pair to refresh command snapshot + fresh node.json).
- Verified end-to-end from agent: `adb devices` (S22 R3CT30H0M2X ✓),
  serial COM3 → `?` status: **{"st":"SLEEP","bat":94,"runs":5,"ver":"5.75"}** ✓,
  git repo @ f001964 ✓, Flutter 3.47.1 ✓, PlatformIO 6.2.0 installed ✓,
  Python 3.13.15 + pyserial ✓ (use `py`; cmd quoting → b64-wrap python -c).
- **The agent now flashes FW, builds/installs the app, drives adb+serial,
  and reads logs directly. JP = hands on bench only.**
- Device at bench start: SLEEP, bat 94%, 5 runs stored, flash 62%.
- Next: App 1.42 (restart-from-scratch retry) built via node → install on
  S22 → bench n≥5.
- Package name `com.skigatechono.sgc_phone` WRONG (dumpsys empty; pid 27803
  dead) → get real applicationId: `pm list packages | grep -i ski`.

**App 1.42 (queued):** REQUEST_IGNORE_BATTERY_OPTIMIZATIONS + exemption UX;
batch-end teardown BOTH stacks (kills stale link) + autoReconnect suppress;
verify enterDownloadMode still on 1.41 path (it is — MainActivity.kt:68).

#### Bench window 2026-09-09 17:22–17:23 local (JP, app+device relaunched, logcat filter live)

Filter: `onPhyUpdate|handleLcdOnFreeze|uid=10535|status=8|froze` (WSL:
`adb.exe logcat -v time | grep -iE ...`). JP: "No, not appearing" — no wedge,
no wakelock-disable event this window. Caught:

- **17:23:23 status=8 was MID-DOWNLOAD (JP 15:36): transfer in flight when the
  link died, app did NOT auto-recover ("No restart").** → New defect: native
  NordicBleDownloader has no handler for raw GATT disconnect mid-transfer
  (1.34 retry = tx_blocked path; 1.38 auto-reconnect = FBP/Dart link only).
  Proven resume protocol (79 ms re-attach) never gets invoked on this path.
  → **App 1.42 scope grows:** onUnexpectedDisconnect mid-batch → wait ~5 s
  (device 4 s supervision + re-ADV) → reconnect → CMD_START resume offset.
  Serial tail at that moment still unknown (tx_blocked printed?).
- Freecess machinery ACTIVE on screen events: `handleLcdOnFreeze` at
  17:22:09/17/23/29 (~6 s apart). Lines are stack noise only — no victim named.
- uid=10535 churn: network **BLOCKED** 17:22:39 → UNBLOCKED 17:22:08/17:23:51;
  procState 19↔10, then 19→8→10→19 at 17:23:51. **ALL of this is AliExpress
  traffic, not SGC** (uid resolved 17:39 — see retraction above).
- uid=10535 side traffic: keystore2 `store_new_key ... uid=10535` (17:22:15),
  RemoteDesktopService `Permission Denied uid=10535` (17:22:22) — **resolved:
  AliExpress crypto + framework pokes, not SGC.**
- `onPhyUpdate`: **zero lines — app never calls `readPhy()`**, callback never
  invoked → PHY undiagnosable via logcat until app logs it. → App 1.42:
  call `gatt.readPhy()` post-connect + log `onPhyUpdate`/`onPhyUpdate` status.
- `sync unfroze` 17:22:53 victims all Samsung bloat (dqagent, biometrics,
  fmm, wssyncmldm, sohservice) — not us.

**Open now:** (1) SGC app's REAL uid (`dumpsys package
com.skigatechono.sgc_phone | grep userId`) + pid 27803 owner (`ps -A |
grep 27803`); (2) A/B: force-stop AliExpress → wedge rate; (3)
Never-sleeping-apps for SGC; (4) serial tail (rr_req/zombie_arm/fcp) from
5.75 bench.

### 2026-09-10 05:39 — JP review: zombie-gate AND/OR + repo audit (no code change)

**Q (JP):** main.cpp L1484 `sgc_ble_central_connected() && !BLE.connected()
&& !sgc_ble_ft_active()` — why AND, not OR ("one missing = no connection")?

**A:** The gate is not "is a link absent" — it is "**our connect flag
contradicts the stack** (we believe connected, stack says not) **and FT
doesn't own the link**". Truth table over flag/stack: idle (0/0 — OR would
fire here: 30 s after ANY idle → BLE.end/begin loop, kills SLEEP power
design + murders fresh connects); fresh connect (0/1 — V5.11:
BLE.connected() lags on_ble_connected by a poll; single-signal logic killed
fresh links until update_state learned to trust the flag); live link (1/1);
**the lie (1/0)** = the only sick state → L1477 desync heal (SOFT:
force_recover clears flag + re-ADV) fires immediately; L1484 is only the
delayed HARD escalation (radio restart) if the state persists 30 s
(`BLE_ZOMBIE_TIMEOUT_MS`, config.h:51).

- `!sgc_ble_ft_active()`: never BLE.end()/begin() mid-transfer — the FT
  engine owns link recovery during a transfer (tx_blocked → abort → phone
  restart-from-scratch, App 1.42).
- The 30 s idle gate separates the persistent lie from the V5.11 transient.
- Contrast L1495: `set_hold_sleep(central || connected || ft)` uses OR —
  that question IS "might anything be alive?" (conservative sleep hold).
  Same three signals, two different questions → two connectives.
- **Reachability note:** force_recover (L1477) clears g_central_connected
  before L1484 evaluates in the same pass → in 5.75 the zombie arm is a
  dormant safety net; a zombie_arm print would itself signal an ordering
  bug — exactly why 5.75 instruments it. History: v5.07 zombie had NO
  !connected() gate (T-007: `ble_radio ok=0 why=zombie` after heavy FT on
  5.10-5.11); 5.17 added the gate to protect live-but-idle links.

**Repo audit (JP-PC, 05:39 UTC):** FW **5.75** (`f001964`) = workspace
mirror = repo tip; App **1.42** (`e2da657`) installed on S22. **Code
complete + committed; bench validation pending** (n≥5 runs, serial tail
rr_req/zombie_arm/fcp). Stray uncommitted: `sgc_service.dart` braces-only
formatting artifact in `_findChar` (no semantics — discard or commit,
JP's call). ⚠️ Untracked `ubuntu_credentials_UM790pro.txt` sits IN REPO
ROOT — credentials in a git tree = accidental-commit risk; move out or
.gitignore. Node-exec lesson: quoted grep patterns leak literal quotes
through the cmd wrapper (greps silently return empty) — unquoted patterns
only; `git grep <pattern>` unquoted works.

**Ledger sync note:** workspace ledger had diverged from repo copy by
~12.5 KB (repo missing 09-08/09-09 bench-station entries). Workspace copy
re-shipped to repo in full and committed — workspace = master.

### 2026-09-10 06:23 — RECOVERED: 09-09 evening pull-transfer decisions (session died 19:43) + requirements-first rule

**What happened:** the 09-09 webchat session (15:17–19:43 UTC) died at
19:43:51 mid-FW-5.76-recon; its transcript was archived at 05:39:34 today
when the session reset. None of its decisions had been written to the
ledger → this morning's fresh session answered "no record". All decisions
recovered verbatim from archive `b18840c2-*.jsonl.reset.2026-09-10T05-39-34`.

**Decision record (09-09):**
- 18:47 JP: model download on old SGC (SGC_06): phone asks record-by-record, lean transactions.
- 19:08 JP: **no structure change to the Nicla system** — transport only;
strategies a) char per data type, b) single 500+ B text char (**serial
replacement**).
- 19:14 JP: **b is the way; watchdog 2 s (not 20 s)**.
- Locked scope (19:14:32): FW 5.76 = pull commands in serial parser (USB+BLE
same path) + console characteristic (write req → notify reply, `[seq len
data]` text-hex frames, ≥500 B logical chunk = 2×244 B notifications at MTU
247) + 2 s request watchdog → disconnect → re-ADV + unconditional re-ADV +
`adv` in status JSON; push FT engine disabled behind flag (USB/lab).
App 1.43 = lean pull engine (request→receive→assemble→CRC) feeding the SAME
blob to existing decompressor/analysis; restart-from-scratch recovery kept.
- References: github.com/skixer2/SGC_06 (app: read→notify one-record pull,
SGC_TextualViewModel.kt) + github.com/skixer2/SGC_SensiBLE2_1 (fw:
{num,type,time} newest-first from NVM, read→notify→index++, time-sync).
- Superseded same-evening proposal (19:04): port pole-tap detector /
timestamps-only-over-BLE — **rejected by JP 19:08**, recorded to prevent
re-introduction.

**Code state: NOTHING pull-related written.** Session died during recon
(ArduinoBLE vendored+SGC-patched in lib/, warmup build started, upload via
cmsis-dap). No commits/stash/branch; tree = e2da657 + trivial dart diff.
App 1.42 (17:14, restart-from-scratch) survived and is installed.

**Requirements:** `module_design/PULL_TRANSFER_REDESIGN.md` — full PR/SR/IR/
FWR/APR/verification levels + this decision record. **Awaiting JP go before
any FW 5.76 / App 1.43 code.**

**🚨 BINDING PROCESS RULE (JP, 2026-09-10):** every decision goes into the
test ledger + requirements doc BEFORE implementation. Requirements first
(all levels), then code. Session transcripts are not memory — write it
down or it never happened.


### 2026-09-10 08:33 — JP doc-review directives → HLR v6.0 (DOC-ONLY, code untouched)

JP reviewed the requirements docs; corrections applied everywhere:

- **F03** arm = single piezo-button press (inductive abandoned — LDC1612
  removed from board 2026-08-22; metallic poles unmanageable). **F03a
  PROPOSED (not implemented):** one-press-arms-both via advertising flag.
- **F42** factory reset = **5 presses within 3 s** (piezo_button.h:
  FACTORY_PRESS_COUNT=5, PRESS_WINDOW_MS=3000) — supersedes 20 s hold.
- **F12** SLEEP **immediately** (primary waiting state); **System Off after
  1 h** unconnected (SLEEP_SYSTEM_OFF_MS=3600000); BLE holds SLEEP.
- **F13** wake = button GPIO sense (System-On instant; System Off cold boot).
- **F04** start trigger = **drop-only** (> 2.0 m from P₀); descent-speed
  mode removed.
- **F02** (JP asked 5 s or 10 s): **10 s** — 1000 frames in **Flash**
  (FlashRing linear pre-roll; ARM fill cap 3000/30 s; RAM ring abandoned
  v4.60 after heap pressure). Verified from flash_layout.h + main.cpp.
- **F05** corrected: drain = **pop 2 + push 1 live per 10 ms (net −1)** →
  1000-frame backlog merges in ~10 s. Old "500-sample/2.5 s" math ignored
  ongoing acquisition — withdrawn. Code untouched (it was always correct).
- **F14** no beeper in v1 (DNP footprint, reserved for user request).
- **F38/P08** MTU: **request 517, ≥ 500 B payload target** (pull transport
  IR-3; 247 fallback = 2×244 B notifications).
- **F51/H08/H12-adjacent** BMM150 magnetometer unused → N/A.
- **H10/I09** Qi → **sealable USB-C** (GCT USB4085 → BQ25120, HW v4.2).
- **H14/I12** reed switch (never built) → piezo button. **I03** LDC removed.

**Docs updated:** sgc_requirements.md **v6.0**, sgc_system_design.md
(state diagram + tables rebuilt), integration_tests/hardware.md,
acceptance_tests/device.md, requirements_traceability.md,
sgc_architecture_hardware.md v2.3, sgc_architecture_phone.md (charge stream),
sgc_architecture_decisions.md **AD-018**, sgc_context_gemini_01.md (marked
FROZEN — historical). BOM already correct (v4.x DNP markers). Historical
sections/ledgers preserved (not rewritten).

### 2026-09-10 09:21 — JP directives round 2 → AD-019 (doc-only)

- **F13 confirmed:** no IDLE state (removed FW V5.19); SLEEP → ARMED
  directly on button press. System-design note corrected accordingly.
- **Custom board module = ANNA-B402** (u-blox open CPU, nRF52833; more
  powerful, same code as B112/Nicla line). Supersedes B112 (v2.0-2.3 docs)
  and the Aug-2026 B412 evaluation. B112 pin-map tables marked for B402
  revalidation at PCB redesign.
- **Companion carrier PCB DROPPED:** production = Nicla prototypes (P0) →
  standalone custom PCB ANNA-B402 (P1+). No Nicla+carrier phase.
  sgc_production_strategy.md v1.6; hardware arch v2.4; BOM note added.

### 2026-09-10 09:28 — AD-019 rationale corrected (JP)

B402 chosen because **open CPU** (own firmware, same code as B112/Nicla
line); B412 rejected because **u-connect** (closed AT stack — cannot run
our code). Hardware arch v2.4 + AD-019 wording updated accordingly.

### 2026-09-10 12:39–12:55 — ✅ OTA BENCH COMPLETE: ALL 5 RUNS SYNCED (pull protocol)

**Result: 5/5 runs on the phone, CRC-verified, ZERO manual intervention on the
final batch.** The objective of TC-2026-08-26-001 — complete batch download on
the S22 without manual recovery — is MET by the phone-pull architecture
(FW 5.76 + App 1.43).

- Batch 1 (old app, attempts=3): #0 ✓(2 att), #2 ✓(3 att), #3 ✓(3 att);
  #1/#4 failed (budget). Batch 2 (attempts=8, settle 12 s): #1 ✓(4 att),
  #4 ✓(4 att) — **"pull batch end: ok=2 failed=0"**.
- Wedge evidence: struck mid-transfer even with device pacing on-demand at
  20 ms (512 B/req, ~210 ms RT) → **stochastic phone-side, NOT device TX
  pressure. Churn-window theory (12 s settle) RETIRED — wedges continued.**
- Full recovery loop proven repeatedly: wedge → phone teardown → device
  pull_wdt @2 s → hard radio restart (ok:1, or ok:0→clean reboot) → phone
  reconnect (ADV-wait) → **continue from last offset** → DONE.
  Cost per wedge ≈ 30–40 s; every attempt converts to forward progress.
- Device never lost data, never WDT-crashed; only intentional radio-recovery
  reboots (rr) after failed BLE.begin retries.
- Fixes this bench cycle: `c8e5fce` (wdt arming), `679e404` (hard radio
  restart on pull_wdt), `d2b4a1d` (attempts 8 / settle 12 s A/B).

**Remaining tuning (non-blocking):** wedge cost 30–40 s (backoff+ADV+settle);
settle can return to 4–6 s (no protective effect measured); possible: request
pacing 30 ms, WRITE_TYPE_NO_RESPONSE (kills echo), phone-BT-state hygiene.

### 2026-09-10 12:57 — JP VERDICT on pull bench (binding for next session)

**"Frankly, I am deceived. We still have continuous disconnections and
1 minute to download a run. Even without interruptions the process is
SLOWER than the previous strategy. The 12 s settling doesn't help — it's
NOT harmless: it's a user thinking he bought crap. I often see
'Unexpected line...' on the phone."**

Honest product assessment (agreed by coordinator):
1. **Speed regression vs push:** clean pull run ≈ 16 s (2.4 KB/s) vs clean
   push run ≈ 8 s (~5 KB/s). Causes: hex doubling on air, 512 B/req,
   20 ms×5 frame pacing, write-with-response round trips, request echo
   duplicating traffic. With wedges (3-4/run × 30-40 s recovery) → ~60 s/run.
2. **12 s settle: harmful, zero measured benefit** → revert to 4 s.
3. **"Unexpected line" = request echo shown in UI log** → filter/silent.
4. Wedges: unchanged, stochastic phone-side (proven). Pull didn't cause
   them; pull also didn't reduce them. What pull DID deliver: zero data
   loss, unattended completion, clean recovery semantics.

**Next-session agenda (JP: "continue another day"):**
a) Speed: chunk 512→2048 B/req, kill 20 ms pacing (TX-pressure theory dead),
   WRITE_TYPE_NO_RESPONSE (halves RT), consider base64 or binary frames
   (hex = 2× air bytes) — target clean run ≤ 8 s.
b) UX: silence echo lines, settle → 4 s, hide retry churn behind progress.
c) Open question for JP: pure-pull-optimized vs hybrid (fast push transfer +
   pull-style stateless resume for recovery) — push clean runs were faster.
**Restart state:** FW 5.76 flashed (`d2b4a1d`…`d85b007`), App 1.43 on S22,
5 runs on phone (CRC ✓), docs v6.0, serial logger live on JP-PC COM3.

### 2026-09-10 15:43 — DECISION (JP, pre-code): V2 streaming pull (FW 5.77 + App 1.44)

JP accepted research direction; bench in ~2 days. Docs-first rule honored:
PULL_TRANSFER_REDESIGN.md V2 addendum written BEFORE this code entry.
- Data path = one-way binary notification stream paced by queue capacity
  (≤2 frames/loop, no timer): `s <id> <off> <len>` → [seq16][len8][≤240B]*
  → [0xFFFF] end marker; resume stateless from bytesReceived; phone-side
  trailer CRC verify unchanged. `r` ASCII path kept for debug.
- Watchdog fed by TX progress (frame emitted = alive).
- Echo silenced; settle 12→4 s; request LE 2M PHY; DLE verified via HCI
  snoop on bench day. Old push slowness root-caused (manual 30–60 ms
  cadence + single-in-flight + unverified DLE) — not the stream's fault.
- Target: 39 KB clean ≤ 3 s. FW flash deferred to bench day (DAP power-cycle
  rule); app installed via adb now.

### 2026-09-10 15:51 — JP simplification: NO RESUME in V2 (restart from scratch)

At 2 s clean transfer, wedge probability per attempt ≈ 8× lower than at 16 s.
If it fails → reconnect → `s <id> 0 <total>` again. No partial buffers,
no offset tracking, no resume code. The simplest possible protocol:
phone says "give me run N", device streams, phone CRCs, retry if bad.

### 2026-09-14 10:55–16:30 UTC — THE V2 BENCH MARATHON: FW 5.77→5.86, App 1.44→1.46 (nine firmware versions, one day)

**Context:** bench day for V2 streaming pull (FW 5.77/App 1.44, `75be1da`).
JP-PC node reconnected at 11:22; flash+install+pm clear via adb from gateway.

**Version ladder (each = a failure observed and fixed):**
- **5.78** — pull watchdog (FWR-3, 2 s) fired mid-settle of run #1: app reconnects
  39 ms after DONE without disconnecting (Android link reuse) → armed state
  survives → terminate+radio restart → 147 storm. Fix: disarm at every job
  terminal (d_end, end marker, tail), re-arm on next request.
- **5.79** — SPEED ROOT CAUSE: emit_bin's per-frame USB serial mirror =
  21 ms/frame baud cap = 5 KB/s ("exactly the old version"). Mirror removed
  (pull_prog every 40 frames), 4 frames/loop. Result: 40 fps bursts, full runs
  in 3-5 s — but flooding wedged the S22 4/4 (rr:2 via writeValue block).
- **5.80** — pacing 15 ms/1 frame per pass. Still rr:2 (offer 66 > drain 40).
- **5.81** — pacing 25 ms (= measured 40 fps drain equilibrium) + connected-
  gate before writes. Gate ineffective (stale state inside frozen loop).
- **5.82** — ISR WDT feeder (push-era ft_wdt_ticker de-static'd): feeds HW WDT
  from interrupt context, re-armed per frame, 6 s grace. Controlled rr:2 only.
- **5.83** — silent-ghost detector: live-but-silent central >30 s → disconnect
  + re-ADV (the 17:59 zombie slot: phone's failed-CCCD race, 20+ min hold,
  untouchable by force-stop or BT toggle).
- **5.84** — feeder grace 10 s past stream completion (boundary churn between
  runs was still rebooting).
- **5.85** — pull watchdog 2 s → 8 s (S22 slow mode = 60 ms CI = 13 fps with
  >2 s mid-stream jitters; the 2 s wdt executed every slow run, 8 attempts on
  run #2, app gave up).
- **5.86** — JP DIRECTIVE "absolutely avoid rebooting": ISR feeder now runs
  the ENTIRE time a central is connected; any activity re-arms a 30 s budget.
  Hardware reboot now requires a 30 s total wedge; all faster paths are
  non-reboot recoveries.

**App ladder:**
- **1.45** — both download buttons → native V2 (legacy FBP button still ran
  V1.21 resume + push protocol = the "resume" JP saw; killed per JP order).
- **1.46** — SLOW-MODE GUARD: re-assert CONNECTION_PRIORITY_HIGH on any
  conn-param update >30 ms (parsed from Nordic logs) + before every console
  request. Slow mode = S2x system churn (BatteryManager/Bose probes) leaving
  CI at 60 ms.

**Phone-side findings (TC-2026-08-26-001 additions):**
- Slow mode root: CI 60 ms vs 15 ms (1 notification/event ≈ 13 fps — matches
  every measurement). Central-side parameter; app re-asserts.
- System services (BluetoothDeviceBatteryManager, BoseMobile, D_FMM) probe
  the SGC around every connect/disconnect — churn source.
- Failed-CCCD races can leave ghost slots that survive force-stop + BT toggle.

**State at wrap (16:30 UTC):** FW 5.86 flashed+booted. App 1.46 APK BUILT,
NOT installed (phone left with JP — `adb install -r` when back on USB).
2 of 5 runs on phone; 3 missing. Transport lesson: vault→git→node relay is
the reliable channel (canvas ship flaked; b64 hand-copy corrupted 4×).

### 2026-09-14 19:30–20:19 UTC — Evening session: cache-133 saga, deadline VERIFIED, 5.88 closes the reboot escalation

- JP's 15-min window stretched to an hour. Phone wedge per run (evening mood, worst of the day).
- **133-after-reboot = cached GATT database**: fixed via `pm clear com.android.bluetooth`
  (wipes BT stack cache; nothing paired, nothing lost) + `pm clear` of the app. Run 1 then OK.
- **App 1.46 installed + live** (CI re-assert guard). Phone restarted once.
- **pull_deadline (5.87) VERIFIED in production**: 6+ firings, all on spec (10-14 s, partial
  bytes logged: 2880/5520/7680/18720/18960/27120 B of 39044-39668).
- **5.87 BUG: deadline → radio restart → begin() ok:0 → NVIC reboot escalation** (6 reboots,
  "ble_radio ok:0 retry:1 reboot:1" each time). Root: restart tore down after only 3 polls
  while the trickling link still lived (5.69 lesson resurfaced via the new path).
- **FW 5.88**: radio restart now disconnects and waits (2 s bounded, ISR feeder covering)
  for link teardown before end/begin; settle 100→300 ms. Booted 22:18 local. UNTESTED —
  first bench tomorrow verifies deadline→ok:1→re-ADV with no reboot.
- HCI snoop pipeline BUILT and proven (bugreport → extract FS/data/log/bt/btsnoop_hci.log →
  scp via vps → python parser). Ring buffer ≈13 min — pull bugreport IMMEDIATELY after the
  next bench window. DLE (Data Length Change event) still unconfirmed — snoop never covered
  a connection setup yet.
- JP's MTU question: app requests 517; device caps 247 (ArduinoBLE/Cordio config). 5.88+
  idea: device MTU 511 → 508 B frames → halves notifications/KB, doubles slow-mode throughput
  (~8 KB/s at 60 ms CI). Spec'd, not built.
- **Tomorrow's resume checklist**: power-cycle device → clean 5-run sync on 5.88/1.46 →
  verify deadline recovery has no reboots → bugreport right after → parse snoop for DLE +
  wedge signature. Phone settings pending: Never Sleeping Apps + unrestricted battery.
  App 1.47 backlog: scan-rate cap (5/30 s), surgical connection priority (HIGH only during
  stream), device MTU 511.

### 2026-09-15 11:58 UTC — BINDING ARCHITECTURE DECISION: firmware platform switches to Zephyr + nRF Connect SDK

JP verdict after two bench days: Arduino + mbed-Cordio BLE is UNUSABLE for the product bar
("athlete at -10°C, trainer waiting" — data must be visible in seconds, not minutes).
Final bench on 5.89/1.46 (BT system app unrestricted): run 1 directory timeout, 5 failed
attempts. Every recovery path worked as designed but the frequency of phone-stack
failures + Cordio's API ceiling (begin-after-end impossible, blocking writeValue, no
backpressure) = no path to "passably well" on this stack.

DECISION (JP): port firmware to Nordic SoftDevice controller + nRF Connect SDK (Zephyr).
Rationale: (a) same nRF52833 silicon in the Nicla's ANNA-B112 and the production
ANNA-B402 — the port lands on production hardware unchanged; (b) qualified controller,
no ArduinoBLE failure classes; (c) JP will not fund PCB development without a
passably-working prototype in hand.

Carries over VERBATIM: BLE protocol (D/s commands, binary frames, FFFF end marker,
CRC trailer) → the phone app is UNCHANGED and remains the validation loop.
Port order: M0 board bring-up (Zephyr def for B112/Nicla) → M1 GATT+pull+CRC benched
against existing app (decision validation gate) → M2 RawRunStore on Zephyr flash API →
M3 BHI260 via Bosch C API (long pole) → M4 state machine + full bench.
ArduinoFW 5.89 = FINAL Arduino build (kept for reference/comparison benches).
App 1.46 remains current. All watchdog/deadline knowledge documents the failure
taxonomy; most becomes unnecessary on a real stack.

### 2026-09-18 — ZEPHYR M0 CLOSED + M1 STEPS 1-2 (all on JP-PC, VS Code-managed SDK)

**Environment (JP-installed, binding):** SDK `C:\ncs\v3.4.0` (west workspace) +
toolchain `dcbdc366a1`, installed via VS Code Toolchain Manager. ZioClaw installs
NOTHING on JP-PC (JP rule 09-18). SEGGER J-Link V9.24a present (unused for Nicla).
Flash path: xPack OpenOCD (pio package) + on-board CMSIS-DAP (`0AFB5B3B`, COM3).

**M0 ACCEPTED (JP-verified):**
- Stock board `arduino_nicla_sense_me` (Zephyr 4.4, no custom board needed).
  Pins match SGC hardware: button P0.21, IS31FL3194 LED bus (TWIM0), sensor bus
  P0.22/23, UART console TX P0.20/RX P0.9 @115200.
- hello_world: flashed SWD @0x0 (Arduino bootloader overwritten — recoverable via
  bootloader hex if ever needed), boot banner verified on COM3.
- BLE beacon sample: SoftDevice Controller init + advertising, JP phone-verified.
- IS31FL319x LED sample: R/G/B fade sequence JP-verified. NOTE: driver implements
  set_color/write_channels/get_info ONLY — led_on/led_off = silent -ENOTSUP.
- UICR measured: hw reset pin = P0.00 (PSELRESET=0), button P0.21 = plain GPIO.

**M1 STEP 1 — app 01_button_led v0.1.1 (working, JP-verified "It beats"):**
DT alias via node path (board has no led_rgb label), DT_PARENT walk to chip device,
button GPIO IRQ callback, LED heartbeat via led_set_color (checked returns).

**M1 STEP 2 — app 02_ble_hello v0.2.0 (working, phone demo pending JP Monday):**
- SGC UUID family FIXED: `8d5aXXXX-84a4-4c5e-8e4f-1c2b3d4f5a6c`
  (0001 service, 0002 counter). Do not regenerate, ever.
- GATT: 1 service + READ|NOTIFY u32 counter, manual read cb (helper not linked),
  CCC subscribe tracking. Connectable adv as "SGC-Dev" (BT_LE_ADV_CONN_FAST_1).
- Concurrency: k_timer(1Hz) -> k_work -> notify/LED; button ISR -> k_work -> notify;
  main() idles K_FOREVER. This is the SGC pattern from here on.
- Serial boot log verified: "Bluetooth initialized / Advertising as SGC-Dev".
- Phone verification (nRF Connect subscribe + counter tick + button notify) PENDING.

**Zephyr 4.4 API lessons (all cost compile iterations today, now documented):**
BT_LE_ADV_CONN renamed FAST_1 · BT_UUID_128_ENCODE args must be inlined (comma-macro
trap) · CONFIG_BT_PERIPHERAL=y REQUIRED for GATT server (beacon didn't need it) ·
bt_gatt_attr_read helper Kconfig-gated out → manual offset-aware read ·
gpio_add_callback = 2 args · sys_kernel_version_get() + manual unpack (macros gone) ·
is31fl319x: no led_on/off ops.

**Toolchain/dev-env decisions:**
- Tools LIVE IN REPO: `Firmware_Zephyr/tools/` (zbuild/zflash/zreset/zserial(.bat)/
  zpeek + _oocd_common + README). PC-independent (discovery: NCS_DIR→C:\ncs,
  %USERPROFILE%\.platformio→PATH openocd). On JP user PATH; zserial.bat wrapper
  because py ignores PATH for script args.
- `C:\sgc` junction → deep Firmware_Zephyr path (V-model tree kept per JP);
  self-documented via JUNCTION.md. VS Code builds MUST go via C:\sgc (Win 260-char
  limit crashes CMake at 250 otherwise).
- VS Code flow proven by JP: trust folder → nRF Connect → Add Build Configuration →
  board arduino_nicla_sense_me → F12 IntelliSense after first build.
- Relay model PROVEN: VPS commit → PC fetch (ssh://vps/... via id_ed25519_sgc) →
  cherry-pick → push GitHub. Host needed git safe.directory exception (done).
  NEVER `git add -A` in mirror (release_tests_v4.90/ stays untracked).

**NEXT (Monday):** JP verifies app-02 phone demo + reads code; then step 3 =
pull-protocol characteristics (D/s commands, binary frames, FFFF end, CRC trailer —
protocol verbatim from ArduinoFW, benched against UNCHANGED App 1.46).

### 2026-09-21/22 — M1 STEP 2 VERIFIED + STEP 2.1 (v0.2.1 LED-as-state)

**App-02 v0.2.0 phone demo VERIFIED (JP bench 09-21):** scan → connect
"SGC-Dev" → subscribe → counter ticks 1/s = CCC + notify path proven
end-to-end. LED blink correctly unaffected by connection (heartbeat by
design — no bt_conn_cb existed in v0.2.0). JP caught a comment bug:
`{0xFF,0,0}` = **BLUE**, not red — stock board DTS declares
`color-mapping = <BLUE, GREEN, RED>` and is31fl319x set_color() writes
the array to channels as-is (no logical color remap) → index 0 = blue
die. Verified against zephyr v4.4.0 source (board dts + driver).
Button-notify path not exercised this bench (optional confirm).

**M1 STEP 2.1 — app 02_ble_hello v0.2.1 (LED = connection state):**
Requirement: LED communicates link state — 0.5 Hz blue blink while
advertising, solid green while connected, and the device re-advertises
BY ITSELF after any disconnect. Introduces the connection lifecycle
(bt_conn_cb) + self-recovery to advertising — the exact pattern the
step-3 pull protocol needs (device must become discoverable again after
every download session).
- New: `BT_CONN_CB_DEFINE` connected/disconnected callbacks printing peer
  addr + disconnect reason (0x13 clean remote term / 0x08 supervision
  timeout — the S22 wedge friend); shared `start_advertising()` helper
  (bt_ready / connect-fail / disconnect, -EALREADY tolerated);
  `volatile bool connected` (BT RX thread writes, sysworkq reads).
- Tick handler: solid green when connected, else blue blink; color
  comment corrected (index 0 = blue die on Nicla).
- CCC note documented: stack clears subscription on disconnect (ccc cb
  fires with value 0 → "phone unsubscribed" on serial).
- Kconfig: unchanged (BT_CONN implied by BT_PERIPHERAL).

**SDK (JP 09-22):** NCS v3.4.1 LTS patch offered by Toolchain Manager.
Decision: take it — additive install (v3.4.0 dir stays), patch-level =
bugfixes only. zbuild auto-follows (picks last version dir under
C:\ncs with .west; toolchain hash likely unchanged for a patch — if it
DOES install a new hash, pin NCS_DIR in tools to avoid mixing eras).
Regression = rebuild + bench app-02 (covers BT + LED + GATT stack paths).
- Dev-env: `zpull.bat` added to tools/ (relay fetch+merge from VPS mirror in one
  command) after JP pulled from GitHub origin and saw no changes (expected:
  origin is always behind; relay VPS→PC→GitHub). TortoiseGit equivalent
  documented in session: add remote `vps` = ssh://vps/root/.openclaw/workspace/
  ski_gate_chrono, SSH client must be Git's ssh.exe (not TortoisePlink).
- Dev-env: NCS 3.4 **sysbuild** nests the app domain in the build dir → hex at
  `<build>\<app>\zephyr\zephyr.hex` (JP hit it: C:/sgc/build/02_ble_hello/
  02_ble_hello/zephyr/zephyr.hex). zflash.bat now accepts the build dir and
  finds zephyr.hex itself; README fixed.
- Dev-env: JP-PC **user PATH clobbered** — HKCU\Environment\Path = literal
  `%PATH%;C:\msys64\ucrt64\bin` (bad setx; per-user entries lost in fresh
  shells: py launcher, WindowsApps, Python313\Scripts, flutter, npm, adb).
  Node-host process still runs with the old rich PATH (why remote probes saw
  `py` while JP's PS did not). zserial.bat hardened: resolves py via
  %LOCALAPPDATA%\Programs\Python\Launcher\py.exe, PATH-independent.
  Registry repair offered to JP (pending his go). Node exec lesson: quoted
  args leak as \" through the wrapper — quote-free commands or bat files only.
- Dev-env FIXED: JP-PC user PATH repaired (JP-approved, 09-22 07:20 UTC). Root cause:
  JP's `setx PATH "%PATH%;C:\msys64\ucrt64\bin"` (MSYS2 for C++). New HKCU Path =
  flutter\bin; Python\Launcher; WindowsApps; Python313\Scripts; Roaming\npm;
  msys64\ucrt64\bin (backup at C:\Users\v17ni\path-backup_20260922_091938.txt).
  Fresh shells get py/flutter/npm back. zserial stays LOCALAPPDATA-hardened anyway.
- DEFERRED: flutter upgrade (3.47.1 → stable). Not during Zephyr port; App 1.46
  baseline stays on 3.47.1. Do it deliberately at next app-side milestone:
  upgrade -> doctor -> rebuild+smoke App 1.46 -> proceed.
- Dev-env: SAC (Smart App Control) auto-flipped evaluation→enforcement 09-22,
  blocked dart.exe (and would block openocd/msys2). JP turned it OFF (one-way)
  via Windows Security -> App & browser control. Root cause of "Device Guard"
  error text. Flutter verified working after.

### 2026-09-22 — M1 STEP 2.1 BENCH: 4.5/5 PASS + adv-restart bug caught (v0.2.2)

**Bench (JP, serial-attached, SDK v3.4.1 / Zephyr 4.4.2 build):**
connect->solid green ✓, button->notify ✓ (first bench), subscribe->1 Hz
notify ✓, disconnect->blue ✓, `phone unsubscribed` auto-fires ✓.
**BUG:** after disconnect, SGC-Dev NOT visible in scans. Serial evidence:
`disconnected ... re-advertising` then `ERROR: adv start -> -12` (-ENOMEM).
**Root cause:** bt_le_adv_start() called INSIDE disconnected_cb → controller
still holds the old adv set (released only AFTER the callback returns) →
ENOMEM → device never advertises again.
**Fix (v0.2.2):** deferred restart — callback submits k_work_delayable;
handler restarts adv from sysworkq + retry (10x100ms) on error. Boot path
(bt_ready) stays direct (no teardown pending). Lesson added to the 4.4 API
list: connection lifecycle work must be DEFERRED, never inline in conn cbs.

### 2026-09-22 — M1 STEP 2.1 CLOSED: v0.2.2 FULL LIFECYCLE PASS ✓

Bench (JP, serial-attached): connect -> solid green -> subscribe -> 1 Hz
notify -> unsubscribe -> disconnect (0x13) -> **advertising up (no ERROR)**
-> **reconnect on new phone addr** -> second cycle clean. Deferred k_work
restart + retry logic proven. Self-recovering peripheral complete.
Clarifications benched: (a) button notify only transmits while subscribed
(BLE rule; presses while unsubscribed = printk only — misleading print,
polish pending); (b) Read = instant snapshot of the u32, no queue —
climbing values on repeated reads are 1 Hz ticks + nRF display history.
**NEXT = M1 STEP 3: pull-protocol characteristics** (D/s commands, binary
frames, FFFF end, CRC — verbatim from ArduinoFW) benched against
UNCHANGED App 1.46 = M1 decision gate. Proposed split: 3a = characteristic
skeleton + subscribe-gated loopback frames; 3b = full protocol + CRC.

### 2026-09-22 — M1 STEP 3a REQUIREMENT: pull-protocol skeleton (app 03, loopback)

Spec (verbatim from ArduinoFW 5.89 pull_transfer.* — extracted 09-22):
- UUIDs (ArduinoFW SGC_UUID macro): service **0x5347** ("SG") on the BT base
  UUID 0000-1000-8000-00805F9B34FB; console char **0xABCA** (write+notify, 247).
  NOTE: this is the family App 1.46 speaks — 8d5a stays as demo-service only.
- Commands (ASCII line per write): `D` → one `{"run":id,"ts":..,"sz":..,"side":..}`
  line per run + `{"d_end":n}` · `s <id> <off> <len>` → binary stream ·
  errors: E1 bad run · E2 bad off/len · E3 malformed · E4 unknown cmd.
- Stream frames: [seq:2B LE][len:1B][payload ≤ 240 B], paced 25 ms/frame,
  END marker FF FF 00. Stateless (id,off,len carried per request).
- Capture-only write handlers (V5.16 lesson); stateless abort on disconnect.
3a SCOPE: new app `03_pull_proto` v0.3.0 = app-02 base (demo svc, LED state,
deferred adv) + 0x5347/ABCA console + D/s commands served from a **RAM ramp
pattern** (2048 B, byte = offset&0xFF) — no flash, no CRC yet. Honest button
print folded in. 3b = flash blobs + CRC trailer + real dir + App 1.46 bench.

### 2026-09-22 — M1 STEP 3a CLOSED: app-03 v0.3.0 pull skeleton PASS ✓

Bench (JP, nRF Connect + serial, 09:50 UTC): connect (LED green) →
subscribe ABCA → "D" → 2 run lines + d_end ✓ → "s 1 0 2048" → frames
seq 0-7 @240B + seq 8 @128B + FFFF end, {"ev":"s_done","seq":9} ✓,
~225 ms total, zero errors. Demo svc (counter/CCC) co-exists ✓.
Not explicitly exercised (deferred to 3b bench): E1/E2/E4 paths,
mid-stream disconnect abort, eyeball-confirm of FF FF 00 on phone.
Build: remote-compiled by ZioClaw (start /b detach + log — new proven
pattern), zero warnings, FLASH 21% / RAM 31.5%.
**NEXT = 3b: byte-exact blob format (16B RunHeader + payload + 6B CRC32
trailer, RAM-backed fake store), real directory walk, adv name + 0x5347
UUIDs for App 1.46 discovery, CRC verified by the UNCHANGED app = M1
decision gate.** (Storage port = its own later step.)

### 2026-09-22 — M1 STEP 3b SPEC (extracted verbatim, app-side verified)

- **RunHeader 16 B packed** (littlefs_storage.h): format_ver(1)=2 · arm_side(1)
  · ts_utc(4) · baro_temp(2, tenths °C) · data_size(4)=0 (v2 on-disk) ·
  frame_count(2)=0 (v2) · cal_accuracy(1) · _pad(1).
- **CRC32: PAYLOAD ONLY** (raw_run_store.cpp: init 0xFFFFFFFF at create_run,
  updated in append_data only — header bytes NEVER CRC'd; finalize = xor
  0xFFFFFFFF; standard reflected table). **Trailer 6 B** = C3 32 magic +
  crc32 LE(4). Blob = 16B hdr + payload + 6B trailer.
- **App 1.46 discovery (Phone_app_prototype/lib/ble)**: FlutterBluePlus
  GENERIC scan (no UUID/name filter — device picked by name in UI);
  `_findChar` null = tolerated (best-effort read/write) → ABCA-only device
  is benchable. Download path = ABCA console (native downloader).
- **3b impl (app-03 v0.4.0)**: RAM fake store, 2 runs (payload 2048 B →
  sz 2070; payload 39350 B → sz 39372 = classic bench run), payload =
  ramp byte (off&0xFF) synthesized on the fly (no RAM buffer — 64 KB
  budget), CRC precomputed at boot; real dir walk; s_done gains
  k_uptime_get_32() elapsed (JP request). E1/E2/E4 + mid-stream abort to
  be benched this round. Gate: unchanged App 1.46 downloads run + CRC OK.
- 3b bench PHASE 1 (JP, nRF Connect, 10:59 UTC): run 0 → 9 frames, s_done
  ms=252 (pacer honest: 25 ms + ~3 ms overhead/frame; phone-side slowness
  = CI drain, confirmed). Run 1 (classic 39372 B) → 165 frames, ms=4606
  = 8.55 KB/s device-side (vs 4 KB/s old push-FT via nRF). ABORT path
  incidentally benched: 's' without CCC → s_abort seq=0, clean recovery.
  Framing byte-perfect (240x164+12=39372). REMAINING FOR 3b CLOSURE:
  App 1.46 unchanged download + app-side CRC verification.
- 3b GAP FOUND (app bench "no saved runs"): App 1.46 connect flow = syncTime
  (ABC0, tolerated) → readConfig (tolerated) → getRunCount (ABC8) →
  getRunList (ABC9 JSON) → native pull download (button = _downloadRunsNative,
  VERIFIED — pull path, already implemented). Missing = ABC8/ABC9 listing chars.
  Formats extracted: ABC8 = count[2 LE]+age[4]; ABC9 = build_run_list JSON
  [{"id":N,"ts":N,"size":PAYLOAD_ONLY,"side":"left|right"}] (size =
  compressed_size = payload, NOT blob total — differs from D's sz!).
  v0.4.1 adds both chars (read cbs, offset-aware, built from fake store).
- 3b ROOT CAUSE (app 0 missing, nRF clean): Zephyr chars declared as 16-bit
  UUIDs (0xABC8/9/A = 0000ABCX-0000-1000-8000-00805F9B34FB) but ArduinoFW
  SGC_UUID() emits FULL 128-bit 5347ABCX-0000-1000-8000-8000? no —
  5347ABCX-0000-1000-8000-00805F9B34FB. Same digits, DIFFERENT namespace.
  nRF interacts by handle (worked); App 1.46 resolves by UUID string
  (_toFullUuid compare) → null → silent empty reads → "no saved runs".
  v0.4.2: all four product UUIDs as BT_UUID_INIT_128 (byte-identical to
  ArduinoFW), service UUID added to advertisement (setAdvertisedService
  parity). LESSON: 16-bit-vs-128-bit UUID trap — must match the FULL form
  the legacy stack transmitted.
- 3b root cause #2: "run#0 not in device directory" = phone at ATT MTU 23
  (no exchange) → notify cap 20 B → 41-B dir lines FAIL SILENTLY (return
  unchecked), 11-B d_end delivered → app parses empty dir + terminator →
  gives up per run. nRF always requests 247 (why benches passed). Fix
  v0.4.3: CONFIG_BT_L2CAP_RX_MTU=247 + notify return checked & logged.
  LESSON: never ignore bt_gatt_notify rc — a size-rejected notify is
  invisible without it.
- 3b root cause #3: notify -12 (ENOMEM) on d_end after MTU fix — run lines
  now FIT (41 B @ 247) and drained the default TX pool (~3 bufs/contexts);
  3rd back-to-back notify found none. Fix v0.4.4: BT_CONN_TX_MAX=10 +
  BT_L2CAP_TX_BUF_COUNT=10; console_emit_bin gains rc check → clean stream
  ABORT on failed data frame (integrity: no silent corruption possible).
  Forensics line proved itself (caught the failure the bench hid).
- 3b root cause #4 (the ACTUAL pool): ENOMEM came from att.c:356 `att_pool`
  = CONFIG_BT_ATT_TX_COUNT (default 3) — "buffers used for sending anything
  over ATT". Connect burst = MTU rsp + CCC writes + 3 dir notifies > 3.
  v0.4.5: BT_ATT_TX_COUNT=10. (CONN_TX_MAX/L2CAP_TX_BUF_COUNT were the
  wrong pools; kept at 10 as harmless depth.)

### 2026-09-22 — M1 STEP 3b CLOSED: **M1 DECISION GATE PASSED** ✓✓

Bench (JP, 14:34 UTC, unchanged App 1.46 on S22): connect → subscribe →
D (3 lines, clean) → run 0: 9 frames, s_done 253 ms → run 1 (classic
39372 B): 165 frames, s_done 4621 ms (8.5 KB/s). Both runs accepted by
the app's own validation → downloads completed without retry. The
Zephyr platform speaks native product protocol end-to-end.

**The four-layer debugging saga (all fixed today, all documented):**
1. UUID namespace: 16-bit vs full 128-bit (app resolves by string)
2. ATT MTU 23 cap → 247 (L2CAP_TX_MTU + ACL_RX_SIZE 251)
3. Wrong pools guessed (CONN_TX_MAX / L2CAP_TX_BUF_COUNT — kept, harmless)
4. THE pool: att_pool = BT_ATT_TX_COUNT default 3 → 10 (att.c:356)
Plus: notify rc now checked everywhere (text = log, data frame = clean
stream abort) — silent TX failures are extinct.

**NEXT: 3c = module refactor** (JP-requested: main.c is dense at ~700
lines; split into led_state / ble_lifecycle / pull_proto / fake_store,
CMake target_sources) — then the real storage port (SPI flash
RawRunStore), which turns fake_runs into real runs.

### 2026-09-22 — M1 STEP 3c REQUIREMENT: module refactor (JP-authorized evening run)

Language decision (JP re-asked, settled 09-22): firmware stays **C** —
modules-as-statics give the boundaries; C++ costs (no-exceptions/RTTI,
static-init order, ecosystem divergence, port fidelity) outweigh gains
for singleton modules. Door open for host-side C++ tests later.
Split (behavior-preserving, bench = one app round tomorrow):
  store.h (interface) + store_fake.c   — fake runs, blob_read, CRC, sizes
  led_state.c                           — LED-as-link-state (own 1 Hz timer,
                                          decoupled from demo counter — visual
                                          behavior identical, documented)
  ble_lifecycle.c                       — adv/restart/conn cbs/connected flag
  pull_proto.c                          — product GATT (ABC8/9/A) + D/s engine
  demo_svc.c                            — 8d5a counter/button demo
  main.c                                — wiring only, APP_VERSION 0.5.0
Future: flash port implements store.h, swaps one CMake line (store_fake→
store_flash). Also: prj.conf BT_CONN_TX_MAX (deprecated) → BT_BUF_ACL_TX_COUNT
if symbol verified in 4.4.

### 2026-09-22 — M1 STEP 3c SHIPPED (evening run, JP away): v0.5.0 modular

Built clean (216 targets, 0 warnings, FLASH 21.6% / RAM 47.4%) + flashed
by ZioClaw. Board runs v0.5.0 overnight; JP's morning bench = one
App-1.46 round (D + both runs + CRC) = 3c equivalence proof.
Split: store.h/store_fake.c · led_state.c (own 1 Hz timer — decoupled
from demo counter, visual identical) · ble_lifecycle.c (adv/restart/
conn cbs) · pull_proto.c (0x5347 svc + D/s engine + ad payloads) ·
demo_svc.c · main.c = wiring only (34 lines).
Deltas (both deliberate, both documented in-code):
  - LED render moved to its own timer (ownership; same visuals)
  - BT_CONN_TX_MAX [deprecated] → BT_BUF_ACL_TX_COUNT=10; Zephyr static
    assert then REQUIRED BT_BUF_EVT_RX_COUNT(12) > ACL_TX(10) — pool
    depth is a family affair, enforced at compile time (gcc.h:87).
    Interesting: the deprecated symbol never triggered the assert —
    further proof it wasn't the real pool.

### 2026-09-23 — M1 STEP 3c CLOSED: equivalence PASS ✓

JP bench (06:44 UTC): app data cleared (no delete UI exists —
LocalStorage.delete() built but never wired, app-side TODO), fresh
download round on v0.5.0 modular: run 1 = 165 frames, s_done ms=4624
(vs 4621 on v0.4.5 monolith) — behavior-preserving proven. 3c done.

**PARKED (JP request, app-era agenda): pull throughput tuning.** The
4.6 s for 39372 B is POLICY not physics: 165 × STREAM_GAP_MS(25 ms)
= 4.13 s ceiling by design (5.81 S22-wedge equilibrium). MTU 247 @
App-1.46 HIGH-priority CI (11.25-15 ms) could theoretically drain
2×-4× faster. When tuning: (a) lower pacing toward CI, (b) FIRST
convert data-frame ENOMEM policy abort→retry/backoff (faster pacing
WILL empty the ATT pool; today's abort would kill streams), (c) bench
with S22 wedge vigilance (TC-2026-08-26-001 taxonomy applies).
- PARKED split into TWO items (JP, 09-23 06:48 — settle ≠ stream time):
  (a) stream pacing (25 ms policy, needs ENOMEM retry first) — see above;
  (b) CONNECT_SETTLE_MS 4 s (NordicBleDownloader.kt:52, blind sleep
      post-connect) → event-driven readiness: onMtuChanged + onPhyUpdate
      + discovery + CCC flags instead of sleep. History: 12 s A/B showed
      NO benefit (1.37); root cause was the GATT buffer clone (1.39);
      settle survived as belt-and-braces against CORDIO's queue — SDC +
      att_pool=10 likely tolerant (today's D-after-CCC already proves
      immediate traffic). Quick knob A/B first (4s→1s→500ms), then the
      event-driven fix. Also present: 500 ms inter-run sleep (:377).

### 2026-09-23 — M1 STEP 4a REQUIREMENT: flash bring-up + read path (JP GO 10:28)

Hardware fact settled (JP): bench = STOCK Nicla Sense ME with 2 MB
MX25R1635F (stock Zephyr DTS correct, jedec c2 28 15). Custom carrier
(8 MB MX25R6435F) deferred until system proven — M0 doctrine holds.
Scope:
- enable flash (overlay+Kconfig), boot smoke = size/page print
- store_flash.c implements store.h over the Opt-A 2MB map VERBATIM
  (pre-roll 0x0-0x13FFF, 8 slots @0x14000, config 0x1FC000, index
  0x1FD000); READS run metadata from the index RunEntry (production
  path), READS blob bytes from slots
- synthetic writer: boot-if-empty writes 2 runs (2048 + 39350 payload,
  NEW timestamps so the app sees them as missing without data-clear)
- 4a bench: App 1.46 downloads FLASH-RESIDENT runs, CRC OK
- store_fake.c stays in tree unused; retires at 4b (lifecycle)
- tests start here: host-side C test of blob layout/CRC + bleak smoke
  (first bench test pass after flash proves out)

### 2026-09-24 — M1 STEP 4a BENCH 1: legacy store read PASS (best case)

JP boot (12:26): flash 2048 KiB confirmed; RRS2 index found — written by
ARDUINOFW itself (NOR survived all firmware changes; synthetic writer
correctly DORMANT). 5 legacy entries parsed: real bench runs (39-40 KB,
ts=0 x3 pre-sync era + ts≈1787654533/1787662173 ≈ Sep 21-22 recordings).
App "0 missing" = correct: phone already holds these exact (id,ts) from
September downloads. Read path validated against PRODUCTION data written
by the old firmware. NEXT bench: clear app data → download all 5 → app
CRC against ARDUINOFW-computed trailer CRCs = cross-firmware byte proof.
NOTE for 4b: jedec,spi-nor reports 32 pages x 64 KB (uniform erase
blocks) — lifecycle erase granularity must respect this (or verify SFDP
4K-sector support) before slot prep code assumes 4096.
- 4a rc1: run_list_json[160] truncated 5-entry legacy JSON (sized for
  2 fake entries; boot log shows cut mid-{"id":3). App jsonDecode threw →
  getRunList [] → "0 missing" despite empty local store. Fix: 512 B
  (ArduinoFW char_run_list parity). Boot print of the FULL list = the
  regression check.
- 4a DIMENSIONING POLICY (JP, 12:42 — binding): every buffer sized from
  the WORST CASE of its production domain, never from current test data;
  each invariant becomes a host test (the 160-B bug is the archetype).
  Capacity math: slots bound live runs — stock 2MB = 8 (JSON worst ~420 B,
  512 buffer OK), carrier 8MB = 32 (JSON worst ~1.7 KB — bump or
  ArduinoFW-style graceful truncation; decide at 4b/carrier). "Tens of
  runs" accumulate phone-side only. NOTE: ArduinoFW's own 512 B
  char_run_list silently truncated past ~9 entries — we size by
  construction instead.

### 2026-09-24 — M1 STEP 4a CLOSED: legacy cross-firmware read PASS ✓✓

Bench (JP, 12:47): 5/5 legacy ArduinoFW runs downloaded through the
Zephyr store, "not a single glitch" — app CRC verification passed on
trailers written by the OLD firmware. Read path (RRS2 index parse, slot
addressing, blob streaming) validated against production data on real
silicon. 4a done. (Two fixes en route: run_list_json 160→512; flash API
name.) Boot print of full run list = standing regression check.

**OPEN at 4b — slot geometry (JP challenge, valid):** 8 × 244 KiB slots
= run-length safety (~315 s max @ ~790 B/s observed), typical 50 s run
= 40 KB uses 1/6 of slot. Alternatives: 12×163 KiB (~3.5 min) or
16×122 KiB (~2.6 min). Phone drains device per session → history beyond
a ski morning has low value; oversized-run failure is catastrophic.
DECIDE at 4b (lifecycle locks geometry). Carrier 8 MB = 32 slots same
SLOT_SIZE regardless.
- 4b DESIGN REVISION (JP, 13:49 — product input, binding): retained history
  is VALUABLE (forgotten/dead phone, >8-run slalom sessions) — "low value"
  judgment retracted. JP packing insight → **circular run log** proposal:
  variable-length runs packed sequentially, sector-padded, reclamation
  ONLY at head of log (oldest run's extent — zero fragmentation by
  construction), erase-ahead in POST_RUN keeps hot path program-only,
  logical max-run cap ~244 KB (policy, not geometry). Capacity: ~45
  typical runs on 2 MB (vs 8 today / 16 best-slot). RunEntry page_start/
  page_end = the fields this was kept for. PRECONDITIONS to verify first:
  (1) 4 KB sector erase through Zephyr driver (64 KB page report must be
  resolved — SFDP/driver config; else padding 24 KB avg = math sours),
  (2) wrap-stub at region end (bounded by run cap, explicit in index).
  DECISION: circular log vs fixed slots — JP's verdict opens 4b.
- 4b PRECONDITION RESOLVED (driver source): 64 K page report =
  CONFIG_SPI_NOR_FLASH_LAYOUT_PAGE_SIZE default 65536 (generic); driver
  supports 4096 ("the sector size") + erase path auto-picks largest type
  fitting the requested span. Chip MX25R1635F has 4 K SE. Fix: set
  LAYOUT_PAGE_SIZE=4096; empirical proof = boot self-test (erase 1 sector
  @0x1FE000 ArduinoFW self-test region, verify 0xFF, restore, report).
  Circular log design UNBLOCKED pending bench print.
- 4b PRECONDITION CLOSED (JP bench 14:06): "flash: 2048 KiB, 512 pages
  x 4096 B" + "selftest: 4K erase OK (layout page=4096)" — 4 KB erase
  proven on silicon (save/erase/verify/restore @0x1FE000). Run list now
  complete (5 entries). CIRCULAR RUN LOG GREEN-LIT.
  4b plan: (1) host tests of wrap/reclaim arithmetic FIRST, (2) log in
  store_flash.c (cursor+runway, sector padding, head reclamation,
  page_start/page_end load-bearing), (3) console 'w <bytes>' = synthetic
  run through REAL lifecycle, (4) sensors queue behind.
  FLAGGED to JP: 4b fresh-start erases the 5 legacy slot runs (saved on
  phone) — accepted pending his word.
- 4a review (JP, 14:21): run_list_json 512 = correct for SLOT domain
  (8×~56+2 ≈ 450 B), insufficient for circular log (~45×56 ≈ 2.6 KB).
  4b opening commit = MAX_INDEXED_RUNS=64 policy constant → JSON (~3.6K)
  + entries cache (~2.1K) sized from it by construction + founding host
  test "worst entry × MAX fits both". NEW insight recorded: index format
  is DEVICE-INTERNAL (app never sees it — only blob + D/s frozen) → 4b
  index goes log-native: page_start/page_end for real, no slot byte.
  Tiny-run edge: cap is policy not geometry (region÷min-run unbounded).

### 2026-09-24 — M1 STEP 4b BUILD STARTS (solo run, JP away; "more comments" = standing policy)

Scope locked (all from JP's design push): circular run log, MAX_INDEXED_RUNS=64
policy constant, log-native device-internal index, console 'w' producer
through real lifecycle, host tests of wrap/reclaim/sizing BEFORE silicon.
Legacy 5 slot-runs: fresh-start accepted (on phone; erased progressively).
Structure: src/store_layout.h = PURE arithmetic (target+host shared),
src/store_flash.c = hardware + chunked non-blocking producer,
tests/test_layout.c = gateway-gcc asserts (founding suite).

### 2026-09-24 — M1 STEP 4b SHIPPED (solo run): v0.7.0 circular run log

HOST TESTS FIRST (founding suite, tests/test_layout.c, msys64 gcc 16.2
on JP-PC): 21 checks ALL GREEN — span padding, wrap boundary (exact-fit
no-wrap), JSON budget (founding: MAX_INDEXED_RUNS × worst entry fits),
erase-work bookkeeping, full fill/rotate simulation (typical: live=48 =
region capacity; max-budget: live=8, no deadlock; tiny: pins at cap 64).
Sim reclaim bug caught in self-review BEFORE running (virgin prep-erase
wrongly dropped entries — rewritten to extent-intersection drops).

FIRMWARE (229 targets, 0 warnings, FLASH 23.6% / RAM 61.9%): store_flash.c
rewritten as the log — SGL1 log-native index (16B hdr + 16B entries,
device-internal), chunked NON-BLOCKING producer (ERASING→WRITING→
CLOSING, one sector/chunk per work pass), extent-intersection reclamation
(mirrors the host test exactly), crash windows (a)/(b) documented,
BUILD_EPOCH+uptime timestamps (real 2026 dates), 'w <len>' console
producer, JSON sized from MAX_INDEXED_RUNS by construction.
JP bench script: D(empty) → w 40960 ×N → D grows → w 249834 ×8 forces
reclaim (watch "rec: reclaim" lines) → app download CRC → reboot
persistence ("store: N runs, cursor…").

### 2026-09-25 — REMOTE BENCH INFRA (JP directive 08:31): dual transport + bleak

JP (away): advance without his presence. DECIDED — do BOTH:
(a) SERIAL command input, ArduinoFW parity: one parse path (the existing
cmd_work single-slot capture), two transports; serial-originated replies
echo to serial, 's' frames as hex lines (ArduinoFW emit format); ZioClaw
drives the device via pyserial over COM3 (zcmd.py tool) — full protocol
loop without phone/radio.
(b) BLE from PC via bleak (UM790 has BT): zble.py = automated bench seed
(scan/connect/ABCA/D/s/reassemble/CRC-verify in Python).
FUTURE (JP note): final tests = ZioClaw controlling the PHONE — ADB path
(S22 via USB), or dedicated bench phone purchase.
- REMOTE BENCH INFRA (JP 08:46, agreed): node connection BULLETPROOF —
  watchdog script checking status + acting (JP's proposal). KIT STAGED
  (tools/): node-watchdog.ps1 (FUNCTIONAL tunnel probe via TCP to
  127.0.0.1:18789 — not just process-alive, the 09-25 failure mode;
  kill-stale + respawn with 15s keepalives; log + rotation; node-host
  restart hook noted) + register-watchdog.bat (logon Task Scheduler,
  no admin). LIMITATION documented: no boot-without-logon (true service
  = admin, decide if bench ever runs headless). Install = one bat run
  when node returns.

### 2026-09-26 — REMOTE BENCH LOOP COMPLETE (solo run, node restored)

Watchdog INSTALLED + RUNNING (functional probe every 30 s, log at
%USERPROFILE%\node-watchdog.log; schtasks query blocked in node ctx —
registered via bat, started directly, verified by log).

**Debug saga (all remote, zserial/zcmd/zflash loop):**
1. v0.7.0 boot MPU FAULT (never boot-tested when shipped — lesson:
   never claim "flashed" without a boot capture). Bisect r1 (thread off)
   + r2 (console off) BOTH still faulted → nm symbolized PCs into
   z_idle/z_interrupt stacks below z_main_stack → ROOT CAUSE: stack
   overflow — persist_index 1040 B + rec 4 KB chunk as LOCALS. FIX: both
   static (the 4a lesson, re-learned). v0.7.2 boots clean.
2. console_getchar/getline path = 3rd wild-pointer fault → ABANDONED.
   FIX: own UART RX ISR (peripheral_uart pattern), single-slot capture
   shared with BLE writes. v0.7.3 — zcmd D → {"d_end":0} ALIVE.
3. zcmd spaced args mangled by node-exec quote-leak → underscore
   convention (w_40960).
4. Producer blob offset bug: payload at +22 (BLOB_OVERHEAD = size math)
   instead of +16 (sizeof header) → trailer 6 B late, ramp tail where
   trailer belongs. FIX v0.7.4: offsets use sizeof(run_header).
**VERIFICATION (fully remote): w_40960 → lifecycle record → s tail probe
shows C332 + 0F91F6C2 → python CRC32(ramp 40960) = 0xC2F6910F LE MATCH.**
Runs 0/1 on flash are pre-fix malformed (noted; new runs correct).
zble: RZ616 adapter scans near-blind (1 anon device) — Windows BLE on
MediaTek weak; BLE bench stays on phone, serial is the remote path.

### 2026-09-26 — M1 STEP 4b RADIO BENCH: **APP DOWNLOAD PASS — FULLY REMOTE** ✓✓✓

JP's earlier CRC error = the malformed pre-fix runs (confirmed by theory
+ wipe). After 'x' wipe + 3 fresh w_40960 (v0.7.5, serial-verified CRC
0xC2F6910F match): APP 1.46 DOWNLOAD OF ALL 3 RUNS — Not Downloaded
3→0, Saved Runs 5→8, runs #0/#1/#2 saved (displayed ts = BUILD_EPOCH
compile-constant, off-by-one-day cosmetic; 22 pseudo-frames = ramp
decompression, consistent with fake era). No errors.

**THE SESSION WAS DRIVEN ENTIRELY BY ZIOCLAW:**
- PHONE CONTROL: adb (force-stop/launch/tap/swipe) + uiautomator dump
  (Flutter = no semantics — useless) + SCREENSHOTS analyzed by vision:
  scan FAB (778,1697) → SGC-Dev row (400,1638) → Download (540,1123)
  → verdict screens. The "must control the phone" capability: PROVEN.
- FILE CHANNEL (gateway←PC): watchdog tunnel -R 9999→PC:8080 +
  http.server (log to FILE or its dead console kills it) + docker
  host-net curl (root) into /root/.openclaw/xfer bind-mount → vision.
  Canvas ship still 401; this is the working replacement.
- Watchdog PRODUCTION HEAL: killed tunnel self-recovered ~90 s.
- schtasks create = Access denied even for JP (investigate later);
  watchdog runs from an open PS window meanwhile (JP's window).
- VS Code nrfutil hotplug holds COM3 (close nRF sidebar when benching).
- register-watchdog.bat needs the direct-start fallback added (TODO).

### 2026-09-27 — BOOT-PROOF HARNESS (JP GO 08:40)

Goal: PC reboot with NOBODY logged in → full remote bench self-restores.
Plan: (1) watchdog absorbs http.server (one script, both daemons);
(2) schtasks /sc onstart /ru SYSTEM for watchdog (elevated one-time —
the 09-26 access-denied was the missing admin); (3) node host at boot
(same mechanism or openclaw service install — inspect first);
(4) prove with real reboot test.

### 2026-09-28 — CASE: wrap erase bug + erase-policy redesign (JP order 14:20)

**Found (JP code review, question about store_record_start's erase lines):**
the WRAPPED erase sweep is composed wrong. erase_from = max(addr, erased_end)
= erased_end (high, ≥ cursor always) while erase_to = LOG_START+span+stub
(low) → erase_to < erase_from → walker erases ZERO sectors → record programs
at LOG_START over first-lap bits (corrupt, CRC-fail at pull); reclamation
overlaps() gets an inverted range → no victims dropped (index keeps entries
pointing at overwritten flash); debug "erase %lu B" underflows. Never fired
on bench (3×40 KB in 1.9 MB — wrap needs ~46 typical / 8 max runs). Host
suite inherited the flaw: simulate() mirrors the same math and checks only
counts, never erase-before-write.

**Requirement change (JP, verbatim intent):** "the sweep erases exactly what
the incoming record will occupy" is WRONG for production — run length is
unknown at start (athlete's descent). Correct: free space for a MAX-LENGTH
run right AFTER a run ends; leftover runway is bonus. This is also the
enabler for the future streaming sensor producer (no length at start).

**New policy (SGL2):**
- At CLOSE (post-run), at BOOT, and after WIPE: top-up erase until
  coverage U(cursor): [cursor, cursor+TARGET) when remaining ≥ TARGET,
  else [cursor, LOG_END) ∪ [LOG_START, LOG_START+TARGET) — two segments
  (stub fully cleaned + bottom reserved for the wrapping max-run; a plain
  ring-interval runway is INSUFFICIENT near the end — proven by counter-
  example: remaining=100K, span=244K → bottom needs 244K, interval gives
  144K). TARGET = run_span(MAX_RUN_BUDGET-BLOB_OVERHEAD) = 244 KiB = 61
  sectors. All absolute math, no ring arithmetic anywhere.
- State: two frontiers persisted: erased_hi (from cursor), erased_lo
  (bottom reserve). Wrap transition at close: hi := lo; lo := LOG_START.
- Record start: NO erase, NO reclamation. Coverage guard (defense,
  printk+fail if violated — span must fit [cursor,hi) or [LOG_START,lo)).
- Reclamation moves to top-up planning: victims = entries intersecting
  the erase segments. Persist at top-up COMPLETION (crash window (c):
  mid-top-up crash → old index lists victims, some erased → loud CRC
  fail at pull, catalog otherwise intact — same philosophy as (a)/(b)).
- sgl_header format change (adds erased_lo) → magic bumped SGL1→SGL2;
  old index = load fail → fresh log (loud, not silent).

**Fix scope (this case):** store_layout.h (TARGET + runway_plan pure fns),
tests/test_layout.c (runway_plan unit tests + bitmap lifecycle simulation:
every programmed byte must land in an erased sector, wrap included — the
test that would have caught the original bug), store_flash.c (states
IDLE→WRITING→CLOSING→TOPUP→IDLE; boot/wipe top-up; header essay), FW →
v0.8.0. Consistency audit of whole store vs intended result (JP order).

**RESOLVED & HARDWARE-PROVEN (same day, v0.8.0 → v0.8.1, commits 7b82b2d/96b327d):**
- Host: SGL2 suite ALL GREEN — runway_plan unit tests + bitmap lifecycle
  simulation, 712 records (200 typical + 12 max + 500 seeded random w/
  boundary hunters), asserts EVERY programmed byte lands in an erased
  sector, wraps included. Capacity note: standing runway trades peak
  live set (48→41 typical) for the guaranteed max-run space.
- Remote build saga: THREE stalls at cmake/Kconfig steps — root cause
  stdin inheritance (detached builder waits forever on the node host's
  silent stdin). FIX: `<NUL` on the start chain. (Also: tasklist blind
  in node exec, Get-Process/WMI fine.)
- Bench (COM3, ZioClaw-driven): boot fresh topup 61 sectors + zero-work
  fast path on second boot; typical run math byte-exact (cursor 0x1f000,
  delta 11 sectors, hi = cursor+TARGET); x wipe → 61-sector post-wipe
  topup; 8 max runs → cursor to 0x1ca000, TWO-SEGMENT topup fired
  (111 sectors = 50 stub + 61 bottom) with reclaims id0/id1 exactly at
  the victims; **WRAP RUN id=8 clean at LOG_START through the bottom
  reserve** (coverage guard passed, hi:=lo transition exact); post-wrap
  readback: header real (02/00/ts/125/…/3), payload alignment exact,
  trailer CRC 0x28D0D0AC == locally computed ramp CRC.
- **Regression caught & fixed by the readback itself:** v0.8.0 lost the
  run-header write (SGL1 wrote it at erase-phase exit; the rework
  dropped it) — stream showed 16×FF header with CRC still green (payload-
  only). v0.8.1 writes the header on first WRITING pass. LESSON: CRC
  green ≠ blob good; verify the header too.
- FW v0.8.1 on bench. Flash 23.9%, RAM 70%.

### 2026-09-28 — CASE: SGL3 runtime run budget (JP GO 18:41)

**Requirement (JP):** max run duration is USER-SETTABLE, tied to discipline:
SL 90 s / GS+SG 120 s / DH 150 s / Custom 60–600 s. Documented rate math:
100 Hz (F01), raw 20 B/frame, compressed avg 8.0 B/frame (v2.4 design) →
budget at 10 B/frame ceiling = 1 KB/s → budgets 90/120/150/60–600 KB.
Rationale: production runs have unknown length at start → the wrap tail is
dead up to TARGET (JP's correction 17:54) → shrink TARGET to the real domain.
Discipline→seconds mapping lives APP-side; device stores BYTES (bounds:
1 sector ≤ budget ≤ 600 KB absolute ceiling; default DH 150 KB until the
app sets one).

**Design:** sgl_header gains run_budget (SGL2→SGL3 magic, old index =
fresh). runway_plan_topup() takes the target as a PARAMETER (fully
parametric now). Setter 'b <bytes>' (bench) + store_set_run_budget()
re-tops the runway when idle. 'D' listing gains {"free":N} — athlete-
facing capacity estimate: floor((LOG_SIZE − live_spans − target)/
span(budget)). MAX_INDEXED_RUNS stays 64 for the 2 MB era (JSON buffer +
entries RAM ≈ +15 KB makes 254 untenable on 64 KB Nicla); on the 8 MB
custom board (nRF52833, 128 KB RAM) raise toward the 254-entry sector
geometry. 8 MB bring-up = move LOG_END + DTS (store is parameterized).

**Tests:** host suite parametrized by target; discipline sweep
(92 160/122 880/153 600/614 400 B budgets) each running the full lifecycle
sim; capacity formula asserted.

**DYNAMIC WRAP parked** (JP verdict "too complicated", 17:54): record-
straddling rejected — read path loses single-linear-read invariant,
two-range extents everywhere, mid-sector wrap points. Documented trade:
8×244 KiB tiles the 2 MB log EXACTLY (dynamic wrap would fit the 8th max
run); runway policy already supports it if ever revisited.

**RESOLVED & BENCH-PROVEN (same day, v0.9.0, commit d9decff):**
- Host: SGL3 suite ALL GREEN, full lifecycle sweep at all four discipline
  budgets (SL 92 160 / GS-SG 122 880 / DH 153 600 / custom 614 400 B) —
  712 records each, erase-before-program invariants hold; mixed-size live
  set bounded BY BYTES (live+T ≤ LOG_SIZE), not count (count bound was
  wrong for mixed sets — test fixed, lesson: assert the structural
  invariant, not a derived number).
- Bench v0.9.0: fresh SGL3 boot with budget 153600 (DH default) + 38-sector
  boot topup; 'b 92160' → SL, zero-work re-top (target shrank under existing
  coverage — clamp path exercised); w 90000 (full SL run) accepted, cursor
  math exact (hi = cursor + 23 sectors); D listing shows {"free":19} —
  athlete capacity estimate live (fresh 20 − 1 live = 19 ✓ formula match).
- Build-environment addendum: SECOND freeze cause isolated — VS Code nRF
  Kconfig extension python holds the shared zephyr/.cache lock during
  configure; symptom = freeze right after "Found devicetree overlay".
  Remedy: taskkill the extension python (restarts on demand) + relaunch.
  (First cause was stdin: <NUL. Both now in the remote-build recipe.)
- FW v0.9.0 on bench. FLASH 24.0%, RAM 70.1%.

### 2026-09-28 — CASE: phone-driven test session — 2 app-contract bugs found & fixed, wedge re-sighted

**Session:** JP connected App 1.46 ("no run missing" = 0/0 stale), handed phone
control to ZioClaw (adb + uiautomator + screenshots + file-channel pulls —
semantics NOW EXTRACTABLE from Flutter via uiautomator, better than 09-26).

**Bug 1 (v0.9.1, 2ca973c/45a67e9):** ABC8/ABC9 listing surfaces were built
ONCE AT BOOT — runs recorded after boot invisible to the app until reboot.
Symptom: phone connected, read ABC8=0/ABC9=[] while serial D showed 1-2 runs.
Fix: rebuild_listing() on every GATT read (skip while store_busy). PROVEN
LIVE: post-flash, post-boot runs immediately visible ("2 on device · 2 not
downloaded"). Why it hid so long: every bench flow rebooted between record
and list; production (record → same session download) would have hit it on
day one.

**Bug 2 (v0.9.2, 72da97c):** the {"free":N,"d_end":N} merged line broke the
V2 native downloader — it string-matches {"d_end":N} EXACTLY as terminator;
merged line → "D: directory timeout" retry loop (byte-level proof from
logcat: all 3 notifications received, then 10 s silence → drop 0x13).
Fix: d_end PURE (app-frozen), free estimate serial-only. LESSON: app wire
contract = exact-string discipline; extensions go on NEW lines/chars, never
merged into frozen ones.

**Wedge re-sighting (NOT new, parked TC-2026-08-26-001):** with directory
fixed, native FT streams run and RESUME (84.2/87.9 KB reached, retry
resumed mid-run) but streams still die stochastically (84240 B = 96%,
then 49 KB...) until attempt budget exhausts. Same S22 signature as
ArduinoFW era: stochastic phone-side stall, ~244 B chunks, longer runs =
more exposure. Zephyr port inherits the same agenda (settle/pacing tuning),
not a storage-arc regression.

**FW v0.9.2 on bench** (FLASH 24%, RAM 70%). Phone flow verified to:
connect → live listing → download streams + resume. Completion blocked only
by the known wedge.

### 2026-09-28 (late) — CASE CONT'D: download loop → root causes + END-TO-END PASS

**JP report:** downloads loop forever (~81 KB, disconnect, restart).

**Diagnosis (serial capture during live loop):**
- Device EXONERATED with hard proof: frames continued streaming past the
  phone's reported death (seq 369 @ off 88560 vs phone deaf at 84240; all
  bt_gatt_notify == 0; no device-side abort). Phone-side deafness (the S22
  wedge) mid-stream.
- Wedge persists at 25 ms AND 50 ms cadence (deaths at 84 KB / 43 KB) —
  cadence lowers exposure but is not a cure.
- **The infinite loop's true engine: the V2 native downloader restarts at
  off=0 on every stream timeout (serial: 'stream start id=0 off=0' ×4) —
  NO resume-on-timeout.** Stochastic death × no convergence = loop.
  App-side fix needed (NordicBleDownloader: resume from received-bytes
  offset like the FBP path did).
- v0.9.3 (a58ddeb): cadence 25→50 ms anyway (5.46-proven profile, halves
  exposure; 90 KB ≈ 19 s).

**Bricks & burns (all mine, all ledgered):**
- Two-builds-one-dir race (v093 "frozen" was alive, log buffering;
  v093b launched over it) → mangled hex → silent boot. Fix discipline:
  NEVER launch a build while another may live; verify by process list,
  clean-slate when in doubt.
- SECOND self-wipe via 'x': node-invoke TIMEOUT does not kill the PC-side
  process — the zcmd ran on, sent 'x', held COM3. LESSON ×2: 'x' needs
  device-side confirmation. v0.9.4 PLAN: 'x' prints "confirm within 3 s
  with X"; scripts updated accordingly.
- COM3 contention: VS Code nRF sidebar grabs the port after extension
  restarts; my own long zcmd captures self-collide — stagger captures.

**END-TO-END PASS (v0.9.3, 2 × 40960 B runs):** phone fresh cycle →
live listing 2/2 → native download BOTH runs single pass at 50 ms →
"Not Downloaded 0", Saved Runs 8→10. The 40 KB size converges (matches
09-26 history); 90 KB awaits app-side resume.

### 2026-09-29 — CASE CLOSED: the download loop was the APP's 10 s wall clock

**JP's energy-saver hypothesis: tested & falsified cleanly** (low_power=0,
doze off, app whitelisted — identical 43/87 KB death). The arithmetic then
cracked it: deaths at 43 KB@50ms ≈ 84 KB@25ms ≈ 9 s; 40 KB passes = 8.5 s;
logcat timeout fired at exactly 10.004 s. NordicBleDownloader.kt line 50:
STREAM_TIMEOUT_MS = 10_000 — a WALL-CLOCK set at stream start, designed for
FW 5.77's unpaced 20+ KB/s (39 KB ≈ 2 s). The Zephyr port's paced 9.6/4.8
KB/s made any run needing >10 s total loop forever (no-resume by design).
The "phone deaf" observation = app thread post-deadline while the device
kept streaming (serial-verified). S22 wedge: may exist, but was NOT the
loop's engine.

**Fix (JP GO 05:29, commit c423329):**
- App 1.47: 2 s IDLE watchdog (reset per frame — flowing streams never
  fire) + dynamic OVERALL deadline = bytes / 5 KB/s floor × 1.3 margin
  (90 KB → 23.4 s).
- FW v0.9.4: cadence back to 25 ms (JP: it worked all week; 20 ms one
  constant away). 'x' wipe = two-step confirm (x arms 3 s, X executes;
  BLE path unchanged) — bought by two ZioClaw self-wipes.
**VERDICT:** 90 KB run @ 25 ms on v1.47: Not Downloaded 0, Saved Runs
10→11, no errors. End-to-end PASS.

### 2026-09-29 (06:22) — JP GO: settle 4s→0.5s, cadence 25→20ms

Fresh-memory tuning (small changes + tests, not deferred):
- App: CONNECT_SETTLE_MS 4000→500 — the idle watchdog (1.47) makes a bad
  start self-correcting in 2 s; the preemptive settle no longer buys safety.
- FW: STREAM_GAP_MS 25→20 (12 KB/s) — 25 ms proven clean; pushing one notch.
  Idle watchdog unaffected; dynamic overall floor (5 KB/s) still covers.
- Measure: device s_done ms for a 90 KB run (expect ~7.5 s vs 9.4 s),
  phone-side per-run cycle time (settle savings).
- PARKED for later: batch-runs-per-connection (biggest lever for
  many-small-runs: 20 runs × 6.5 s connect overhead = 130 s > streaming).

**MEASURED (v0.9.5 + v1.48, logcat timestamps, 90 KB run, attempt 1):**
settling 500 ms → stream start 0.72 s after settle → DONE at 9.06 s stream
(90022 B). Full per-run cycle incl. connect+settle+directory ≈ 9.8 s.
Projections: 6×40 KB day ≈ 28 s (was ~65); full log 20×90 KB ≈ 3.3 min.
SURPRISE: 20 ms pacing did NOT raise the effective rate (9.9 KB/s ≈ the
25 ms rate) — the ceiling is per-connection-interval notify dispatch
(CI 15-30 ms), not STREAM_GAP_MS. Cadence lever exhausted; further speed
needs unpaced queue-push (5.77 style) or CI/high-priority games — PARKED.
Batching's value also shrank (per-run overhead now 0.7 s, was ~6.5).
PASS criteria met: no watchdog trips, single attempt, CRC saved (Runs 12).

### 2026-09-29 (13:05) — CASE: M3 step 1 — recording state machine (JP GO, design reviewed)

**Requirements (JP-reviewed, amendments absorbed):**
- States: IDLE → ARMED ('a'; 120 s timeout — JP) → LOGGING ('g' synthetic;
  production = 2.0 m drop detector) → POST_RUN (store close + TOPUP) → IDLE.
  End: 'e' synthetic (production = 5 s flatline < 2 m) OR budget reached.
- ACQUISITION runs at 100 Hz from ARM through LOGGING (JP: even in armed
  state); start-detector check DECIMATED to 10 Hz (every 10th frame).
- Truncate-at-budget + close (JP): data preserved to budget; run marked
  truncated — cal_accuracy bit 7 in the RunHeader (frozen-format-safe;
  app parses it already). App-side warning display = TODO (parked).
- Synthetic frames at the DOCUMENTED packing average (8.0 B/frame, v2.4)
  → 90 s = 72 KB. Real 3-packet-type packing = sensor step.
- Pre-roll DEFERRED to sensor step: flash ring in reserved 0x0000–0x13FFF
  (F02 port; RAM ring forbidden — v4.60 Cordio lesson). ARMED discards
  synthetic frames.
- Serial: 'a'/'g'/'e' + {"rec":"IDLE|ARMED|LOGGING|POST_RUN"} on its own
  line (never merged into frozen lines — d_end lesson).
- NO listing-during-run requirement (JP: nobody lists mid-descent).

**Design:** store gains the promised streaming append API —
store_record_begin() / store_record_append(buf,len) [RAM-buffered, page-
flushed; returns accepted bytes; budget-capped] / store_record_end()
[synchronous trailer+persist]. The async k_work machine keeps only TOPUP;
'w' re-pointed onto the same API (ramp producer moves to pull_proto).
recorder.c owns the state machine + the 10 ms k_timer→work acquisition
loop (sysworkq concurrency pattern). Version 0.10.0.

**Acceptance (bench):** 90 s @ 100 Hz × 8 B = 72 KB run; ZERO prep/erase
lines during LOGGING (hot-path doctrine under true timing); {"free":N}
ticks down; truncation test: budget 60 KB + 90 s logging → 60 KB run with
bit-7 flag; phone download CRC-clean.

### 2026-09-29 (afternoon) — M3 step 1 SHIPPED through v0.10.2

- v0.10.0: recorder.c state machine (a/g/e) + store streaming append API
  (begin/append/end, RAM-buffered, budget-capped) — first synthetic run
  CRC-VERIFIED byte-perfect (0x6FCCDAC2, 6541 contiguous frames, first
  frame seq 513 = ARMED-phase acquisition proving 100.6 Hz idle pacing).
- FINDING 1 (v0.10.1): k_timer→SHARED sysworkq lost ~25% of 100 Hz ticks
  during LOGGING (phone listing reads in GATT context coalesce pending
  works; ARMED idle = 100.6 Hz, LOGGING = ~75 Hz). Fix: DEDICATED
  workqueue (1 KB stack, prio 4) → measured 9548 frames / ~95.5 s =
  TRUE 100.0 Hz sustained. LESSON: acquisition never shares a queue
  with BLE.
- FINDING 2 (v0.10.2): two-thread race — console thread ('e') and rec_q
  tick both drove transitions: double end() (budget_reached +
  manual_end prints interleaved, one run, out-of-order printks across
  threads) + recorder STUCK in POST_RUN. Fix: SINGLE-WRITER — commands
  become atomic intents executed inside the rec_q work; tick
  distinguishes closed-record (-1) from budget cap. Verified: clean
  ARMED→LOGGING→POST_RUN→IDLE cycle, run 6 recorded + listed.
- NOTE: partial final frame (payload 26774 = 3346×8 + 6) — end() flushes
  a partial fill; harmless for synthetic, revisit when real frames land
  (frame-aligned close).
- PENDING acceptance: truncation test (b 60000 + 90 s → auto-close at
  budget + TRUNCATED flag in cal bit7) + phone download of runs 5/6.
- Commits: d009998, 4975255, ecff986, 3c84106, 25f394e.

### 2026-09-29 (15:15) — JP directives: BLE closes on ARM; RAM-ring reconsidered

- **ARM tears down BLE** (v0.10.3): on arm intent → adv stop + link
  disconnect (suppressed re-advertise while suspended; conn tracked via
  ref); on return to IDLE (run_ready or arm_timeout) → re-advertise.
  Rationale (JP): saves memory + keeps acquisition as close to 100 Hz as
  possible. Product reality: nobody lists mid-descent (confirmed 12:58).
- **RAM pre-roll ring REOPENED as sensor-step item** (JP): the v4.60
  Cordio lesson was heap/fragmentation-era; Zephyr shows ~30% RAM free
  and a STATIC ring has no fragmentation exposure. 1000 × ~10 B packed
  ≈ 10 KB static is plausible on the 64 KB part (nRF52833 board doubles
  headroom); flash-ring fallback stays documented. DECIDE when the real
  frame packing lands (sizes known), with link-time RAM as the gate.

### 2026-09-30 (18:45) — Item 1 CLOSED: monotonic ts + truncation proven

- **v0.10.5** (2b3ced2 + 21e19db): ts = max(last+1, computed), seeded from
  loaded index; streaming entry reuses the begin-time stamp. VERIFIED LIVE:
  runs 8/9/10 stamped 1790321516/17/18 = previous max +1 each (uptime had
  reset at reflash — guard held). Fixes the backward-jumping stamps that
  confused the app's downloaded-marking ("4 not downloaded" stale card).
  Proper fix = ABC0 epoch sync (next).
- **Truncation-at-budget VERIFIED** (two budgets): 60 000 → run closes at
  exactly 60 022 total; 8 192 → 8 214; captured printks: budget_reached →
  "stream done payload=8192 [TRUNCATED]" → IDLE → BLE resume → advertising
  up. Flag lives in the index ENTRY (cal bit 7) — the HEADER cannot carry
  it (NOR can't set bits post-write); app-facing truncated display = future
  protocol extension (own line, when app updates).
- CORRECTION: "partial final frame" pending item was an arithmetic error —
  payloads are frame-aligned by construction (3344x8 exactly). Item
  dissolved.
- Host suite ALL GREEN on the ts change.
- Bench: FW v0.10.5, budget restored to 92160 (SL). NEXT: ABC0 epoch sync.

### 2026-09-30 (19:30) — JP code review round: 4 comment/design corrections

1. recorder.c sysworkq comment was stale: the listing-load finding was
   measured PRE-BLE-suspend; production no longer has that load source.
   Comment now historical + architectural rationale.
2. PRE-ROLL ARCHITECTURE now documented in recorder.c (from F02 + v6.0):
   small RAM staging buffer, pop-2/push-1 per 10 ms tick (net -1, full
   buffer drains ~20 s), flash ring 4000×20 B at 0x0000-0x13FFF, ARM cap
   3000 slots, 1000 frames (10 s) retained at start, interleaved drain
   during run. Implementation lands with BHI260 (real frames).
3. cal bit 7: implemented in the INDEX ENTRY (proven); the HEADER cannot
   carry it (NOR no bit-set post-write). Comment clarified.
4. "never RAM" superseded (JP 09-29) — and sharpened by (2): only the
   small staging window is RAM; the ring itself is flash.
Commits: 52aad27 (v0.10.6, comments only — flash on next build).

### 2026-09-30 (21:20) — M3 step 2 DESIGN: BHI260 bring-up (JP GO "item 2")

**Survey results (ground truth, all verified):**
- Stock Zephyr dts has NO sensor nodes — we add the BHI260 node via
  app.overlay. Pins: SPI1 (Zephyr) = SCK P0.11 / MOSI P0.27 / MISO P0.28;
  BHI INT = P0.14 (from our main.cpp wake-pin comment). CS pin = the one
  open item (candidates P0.24/29/30 — resolve empirically at ping, the
  Arduino variant isn't on this machine).
- **BMP390 needs NO separate driver: it sits behind the BHI260 hub**
  (virtual pressure sensor, same SPI host interface). One interface,
  everything: RV quaternion, LACC, pressure, temperature.
- **NO firmware blob: bhy2_boot_from_flash()** — the Nicla's BHI260AP
  runs its firmware from its OWN internal flash (verified in
  Arduino_BHY2 BoschSensortec::begin).
- The bhy2 C API is pure-C portable (bhy2.c/h, bhy2_hif.c/h,
  bhy2_parse.c/h + common/ — vendored from Arduino_BHY2 GitHub).
- ArduinoFW recipe (our main.cpp): RV @100 Hz → Q14 ×16384; LACC
  @100 Hz → int16 mm/s²; baro virtual @100 Hz → Pa/2 uint16 (hPa×50);
  temp @1 Hz header-only. Rates EXPLICIT (the v4.59 1000 Hz-default
  FIFO-flood lesson). Frame → BitPacker (ours, portable) → avg 8 B.

**Phased plan (JP-paced, one phase per session-ish):**
- **A — hub alive:** vendor bhy2 API + Zephyr SPI HAL shim (read/write/
  delay_us over spi_transceive) + overlay node; boot sequence init →
  soft reset → boot_from_flash → chip/product/rom id on serial. Accept:
  correct IDs printed (chip 0x83 family), boot status sane.
- **B — virtual sensors @ 100 Hz:** configure RV/LACC/pressure (rates
  explicit); FIFO poll in the recorder's dedicated workqueue; serial
  dump proof: quaternion+linacc+baro values at 100 Hz ±1% (F01 gate).
- **C — real frames:** BitPacker port to C + recorder integration
  (synth_frame → packed real frames); CRC-verified run through the store
  + phone download; budget math unchanged (doc-avg 8 B/frame).
- **D — pre-roll ring:** staging buffer + pop-2/push-1 drain + flash
  ring at 0x0000-0x13FFF (the documented architecture, 2026-09-30
  review); start detector gets real decimated baro (10 Hz) input.
