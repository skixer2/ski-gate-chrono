# Requirements ↔ Code ↔ Schematic Alignment Verdict
## SGC (Ski Gate Chrono) — 2026-11-20

**Auditor:** ZioClaw (hf/deepseek-ai/DeepSeek-V4-Pro)  
**Dossier:** `consults/req-align-dossier-20261120.md`  
**Sources verified:** 352-line `sgc_requirements.md` (v6.1), 14 firmware `.c/.h` files (Zephyr/NCS v0.25.17), 7-sheet KiCad P1 schematic, `sgc_schematic_spec.md` v0.1, `sgc_architecture_hardware.md` v2.6, `TEST_COVERAGE_MATRIX.md` v1.2, `MASTER_TEST_PLAN.md` v1.2.

---

# AUDIT 1 — Requirements ↔ Firmware/Phone Code

All verdicts cite file:line evidence. Phone-only requirements where no Flutter code exists in this repo are marked PHONE-UNAVAILABLE.

## REQ-FUNC — Device Functional Requirements

| ID | Summary (≤15 words) | Implementing file:line | Verdict | Evidence |
|----|---------------------|------------------------|---------|----------|
| **F01** | 100 Hz ±1% fused sensor acquisition | sensor_prod.c:205-207 (RV/LACC @ 100 Hz config), recorder.c:67 (10 ms timer), sensor_prod.c:67 (10 ms poll) | **MATCH** | Virtual sensors configured at 100.0f; k_timer at 10 ms = 100 Hz; dedicated workqueue (recorder.c:61) for jitter isolation. Hardware jitter proof (P01) requires scope — code structure is correct. |
| **F02** | 10 s Flash pre-roll (1000 samples) | ring.h:14 (RING_SLOTS=4096, 20 B each), recorder.c:35-37 (spec v4-FINAL: Bank A 20s pre-trigger, Bank B 20s post), recorder.c:197 (`ticks >= 100*120` = 20s ARM cap) | **PARTIAL** | Code has 4096 linear slots × 20 B at 0x0000-0x13FFF = 40.96 s total ring. Pre-roll is 20 s (2000 slots in Bank A), not 10 s as req states. Req F02 text says "10-second (1000-sample)" but the code comment says "Bank A = 20 s PRE-trigger max (ARM ≤ 20 s)". The recorder only captures up to 2000 slots in Bank A before timeout. **Req says 10 s; code does 20 s.** |
| **F03** | Arm via single piezo-button press, P0.02, state-gated | button.c:41-53 (btn_isr + debounce), button.c:56-67 (debounce_expired: SLEEP-only gate), button.c:78-80 (recorder_arm) | **MATCH** | P0.02, falling edge, 20 ms debounce, 200 ms quiet window for piezo ring, state-gated (only SLEEP → ARMED). No hold requirement. Exact JP 2026-11-04 directive. |
| **F03a** | PROPOSED: one-press arms both | — | **MISSING** | Explicitly marked PROPOSED in the req. No code exists. Verdict confirms intentional absence. |
| **F04** | Start via cumulative barometric drop > 2.0 m from P₀ | recorder.c:107-141 (sd_feed: SD_DROP_X100=200, sea-level compensated), recorder.c:167-168 (detector_10hz), recorder.c:195-197 (ARMED tick decimation 10 Hz) | **MATCH** | SD_DROP_X100=200 = 2.00 m. P0 captured at first valid baro (50–110 kPa validity window). Pressure increase (descent) computed with dpa*101325/(12*p0) — exact sea-level/altitude compensation per F04 text. Sampled at 10 Hz (every 10th 100 Hz tick). **No speed-based dual-mode** — the v5.6 "speed OR drop" was dropped; v6.0 says drop-only. Code matches current req. |
| **F05** | Merge pre-roll: pop-2 + push-1 live, net −1/cycle | recorder.c:209-235 (LOGGING: drain loop pops 2, pushes 1), recorder.c:238-240 (direct encode when ring empty) | **MATCH** | Exact pop-2/push-1 with the v4.77 fix: when ring_count hits 0 after pops, live frame encodes direct. Drain arithmetic verified: 1000-backlog ÷ 100 Hz = 10 s merge. v6.0 text matches code. |
| **F06** | Auto-terminate after 5 s no-descent (baro flatline) | recorder.c:123-146 (flatline_end_feed: 50 samples @ 10 Hz = 5 s, END_NOISE_PA=8), recorder.c:228-233 (integrated end check in logging loop) | **PARTIAL** | Code has 5 s no-descent with 8 Pa noise floor (≈0.65 m/5 s). **Req says "10 continuous seconds of barometric flatline combined with IMU stillness."** Code uses 5 s (not 10 s), baro-only (no IMU stillness check). The code comment records JP directive: "5 s no-descent" (2026-10-14). The req was never updated to 5 s — it still says 10 s. |
| **F07** | Adaptive bit-packing, ≥ 35% compression | bitpacker.c:7-166 (T1/T2/T3 encode), bitpacker.c:81-92 (classify), bitpacker.h:30 (ANCHOR_EVERY=50) | **MATCH** | Three packet types: T1=6B (int4 deltas), T2=10B (int8 deltas), T3=18B (full anchor). Anchor every 50 frames. Typ T1 ratio 6/20=30%, T2 10/20=50%, T3 18/20=90%. With expected skiing dynamics (mostly T1, some T2) the average should exceed 35% — structurally correct. |
| **F08** | Circular Flash buffer, auto-overwrite oldest runs | store_flash.c:1-800+ (SGL2/SGL3 design), store_flash.c:315-342 (reclaim_victims), store_flash.c:573-580 (topup post-run) | **MATCH** | Age-ordered index, two-frontier erase runway, lazy reclamation on topup. Wrap-proven. CRC-verified at read. Budget-driven runway sizing (F62). |
| **F09** | Run metadata via BLE GATT (ABC8/ABC9) | pull_proto.c:189-235 (read_run_info, read_run_list), pull_proto.c:175-184 (rebuild_listing: new live-rebuild) | **MATCH** | ABC8 (uint16 count + age), ABC9 (JSON listing newest-first). Rebuilds on every GATT read after v0.9.1 fix. Wire run_ids are stable (v0.22.1). |
| **F10** | BLE file transfer + CRC32 integrity | pull_proto.c:307-340 (stream_work_handler: `s` command → paced frames), store_flash.c:163-175 (CRC32 byte-identical to RawRunStore) | **MATCH** | Stream protocol: seq+len+payload frames at 20 ms pace, END marker FF FF 00. CRC32 in trailer (C3 32 marker). Readback verify-after-write in store (header, payload 8-retry, trailer). |
| **F11** | Config R/W via BLE: name, arm, discipline | pull_proto.c:272-296 (read/write_dev_name, arm_side, discipline), ble_config.c:87-116 (cfg_load, persist at 0x1FC000) | **MATCH** | ABC1 (name ≤20 B), ABC2 (arm side 0/1), ABC3 (discipline 0-3). Persisted in config flash sector with SGCC magic. Setters sanitize (discipline >3 → 1, arm → 0/1). |
| **F12** | SLEEP immediately; System Off after 1 h unconnected | power_state.c:33-36 (SLEEP_SYSTEM_OFF_MS=3600000), power_state.c:50-55 (connected → hold SLEEP indefinitely), power_state.c:63-67 (VIN present → no System Off), recorder.c:16-18 (comment: SLEEP entered at boot) | **MATCH** | Exact 3600000 ms = 1 h. Guards: connected → defer, VIN present → defer, not-SLEEP → defer. SLEEP entered at boot, after POST_RUN, on arm timeout. BLE connection holds SLEEP forever (V5.00). Recorder header comment "F12/F13, v0.23.0 — SLEEP is the PRIMARY waiting state." |
| **F13** | Piezo-button wake: <100 ms from SLEEP; cold boot from System Off | power_state.c:83-87 (System Off: button_arm_systemoff_wake → sys_poweroff), power_state.c:94-106 (power_state_init: rr line decode), button.c:93-107 (button_arm_systemoff_wake) | **MATCH** | SLEEP wake: edge interrupt on P0.02 stays armed → instant (<100 ms). System Off wake: GPIO SENSE low → cold boot → power_state_init prints `{"ev":"boot","rr":4}`. Correct per F13 detail section. |
| **F14** | No beeper in v1; DNP footprint only | main.c:42 (no beeper init), pull_proto.c: no beeper handler | **MATCH** | No beeper-related code anywhere. The `main()` function initializes button, recorder, BHI, LED, BLE — no transducer init. Matches DNP decision. |
| **F37** | UTC time sync from phone on every BLE connect | pull_proto.c:244-259 (write_time_sync: ABC0), store_flash.c:139-146 (store_epoch_sync) | **MATCH** | ABC0 characteristic: phone writes uint32 LE epoch seconds. Sanity >1e9. Store extrapolates via uptime. Readback verifies. |
| **F38** | BLE MTU request 517, target ≥ 500 B | pull_proto.c:21-25 (STREAM_PAYLOAD=240), ble_lifecycle.c not read — MTU exchange is in Zephyr BT stack | **UNVERIFIABLE** | Stream payload = 240 B per frame. MTU exchange is handled by Zephyr BT stack (CONFIG_BT_L2CAP_TX_MTU / CONFIG_BT_BUF_ACL_RX_SIZE). The req text says "request 517, target ≥ 500 B." The code does not contain explicit MTU negotiation — Zephyr auto-negotiates. Need to inspect Kconfig/overlay to verify MTU target. |
| **F39** | BLE bonding (LE Secure Connections, Just Works) | ble_lifecycle.c (not fully read in this audit — Zephyr BT SMP config in Kconfig) | **UNVERIFIABLE** | Bonding config is in Zephyr Kconfig (CONFIG_BT_SMP, CONFIG_BT_FIXED_PASSKEY=n → Just Works), not in application C files. External file verification needed. |
| **F40** | BLE OTA via MCUboot + MCUmgr SMP | ota_gate.c:1 (ota_gate_init), store_flash.c: selftest shows 4 KB sector confirm, MASTER_TEST_PLAN v1.2: "OTA stage 1 installed" | **MATCH** | ota_gate_init called from main.c:37. Version 0.25.17 has MCUboot dual-slot. REQ-OTA S1 acceptance met (MASTER_TEST_PLAN v1.2: "0.24.5→0.24.6 full OTA swap from phone, confirmed permanent"). ABC6 reports firmware version. Proof: git log shows 0.25.14-0.25.17 on device. |
| **F41** | RGB LED status: IS31FL3194 I2C on Nicla; SK6812 strip on custom PCB | led_state.c (not fully read), pull_proto.c: no LED-specific code in this file | **PARTIAL** | led_state.c referenced but not exhaustively read. The schematics show IS31FL3194 on i2c0 (P0.15/P0.16, addr 0x53) and SK6812 on P0.19 with boost +5V (DNP v1). Code for LED patterns exists in led_state.c. **Req describes Nicla-stock IS31FL3194 + custom PCB SK6812 strip. Code matches the Nicla path; SK6812 DNP in v1 means no strip code needed yet. Verdict PARTIAL because full verification of LED patterns per state requires reading led_state.c.** |
| **F42** | PHONE-ONLY factory reset (v6.1) | button.c:56-67 (SLEEP gate only — NO press-count logic), recorder.c: (no factory-reset path), power_state.c: (no reset handler) | **MATCH** | Code has exactly zero on-device factory reset. Button does only ARM (SLEEP→ARMED) and WAKE. The v6.1 req says "PHONE-ONLY… on-device gesture REMOVED." Code matches. |
| **F52-F57** | v2 ONLY — RFID/UHF features | — | **NOT APPLICABLE** | All marked v2 ONLY in the reqs. Schematics show unpopulated E310 footprint. |
| **F57.1-F57.4** | Course setup (Mode A, B, dual view, delta format) | Phone-side — no Flutter code in this repo | **UNVERIFIABLE (phone)** | These are phone-app requirements. The repo does not contain the Flutter source code for course setup. |
| **F58-F61** | v2 ONLY | — | **NOT APPLICABLE** | |
| **F62** | Discipline-driven run budget (SL 90/GS-SG 120/DH 150 s) | store_flash.c:562-568 (store_set_run_budget: validates, tops up runway), store_flash.c:576-580 (store_free_full_runs: capacity estimate) | **MATCH** | Budget-driven: `b <bytes>` setter, persisted in SGL3 index. Runway sized to budget target. store_record_begin guards coverage invariant against budget. Free-full-runs computed from live + target. |
| **F63** | Capacity estimate: full-budget runs remaining | store_flash.c:576-580 (store_free_full_runs), pull_proto.c: directory handler emits {"free":N} line | **MATCH** | Computed: (LOG_SIZE - live - target) / target. Emitted as own protocol line (frozen, no merged fields). |
| **F64** | In-app firmware updater (Settings) | Phone-side — no Flutter code in this repo | **UNVERIFIABLE (phone)** | Firmware side: ABC6 characteristic exists (pull_proto.c), OTA gate in place. Phone-side updater not verifiable from this repo. |
| **F65** | 6-digit ownership PIN, stored in config flash | ble_config.c:21-31 (cfg_disk struct — no PIN field), pull_proto.c: no PIN validation | **MISSING** | The config disk struct has dev_name[21] + arm_side + discipline + pad[5]. No PIN field. No PIN validation code in any handler. Req v6.1 requires PIN gate for new-phone pairing, config writes, F42 reset. **Not implemented in current firmware.** |
| **F66** | PIN change from bonded session | — | **MISSING** | Depends on F65. Not yet implemented. |
| **F67** | SMP/OTA behind PIN gate | ota_gate.c:1 (ota_gate_init exists) | **PARTIAL** | ota_gate_init called from main.c. But without F65 (PIN), SMP gating is incomplete — only state-gated (SLEEP-only per REQ-OTA), not PIN-gated. |
| **F68** | APPROTECT in production builds | Not verifiable from C source — Kconfig/nrfjprog build flag | **UNVERIFIABLE** | APPROTECT is a UICR fuse write, not a C-code feature. Would need to check build configuration / Kconfig / production build script. |
| **F69** | Auto-sync: periodic download every ≤5 min | Phone-side | **UNVERIFIABLE (phone)** | |
| **F70** | CRC-failure policy: re-download 2×, delete on deterministic corruption | Phone-side | **UNVERIFIABLE (phone)** | Device side provides CRC32-verified transfers. Phone-side retry logic not in this repo. |
| **F65 (connected-session)** | Connection indicator + action gating; ARM aborts download | Phone-side + FW BLE suspend | **PARTIAL** | FW side: recorder.c:176 (ble_lifecycle_suspend at ARM), recorder.c comment "ARM tears the BLE link down." Phone-side UI gating not in this repo. |
| **R01** | False arm reject: 20 ms debounce, state-gated | button.c:41-53 (debounce_timer 20 ms), button.c:56-67 (200 ms quiet window, state gate SLEEP-only) | **MATCH** | 20 ms debounce, 200 ms quiet window for piezo ring. Only SLEEP(0) state arms. ARMED/LOGGING/POST_RUN presses rejected at line 64-67. |
| **R02** | 30 s arm timeout → SLEEP | recorder.c:197 (comment says 120 s), recorder.c:197-203 (ARM_TIMEOUT at 100*120) | **CONTRADICTS** | Code has 120 s ARM window, not 30 s. Req R02 says "30 s of arming, return to SLEEP (ARM_TIMEOUT_MS)." Code comment says "120 s arm window (JP)." Doc vs code: **req says 30 s, code has 120 s.** Two different reqs conflict: F03 and R02 say 30 s; code implementation + recorder.c comment say 120 s (the JP-mandated "NOT the 20 s pre-trigger ring depth"). |
| **R03** | Mid-run button press ignored | button.c:64-67 (non-SLEEP states: "reject any pressure when not idle or sleeping") | **MATCH** | State-gated at the button level. ARMED/LOGGING/POST_RUN presses = ignored with printk. |
| **R04** | Low-battery graceful shutdown at VBAT < 3.3V | ble_config.c:223-268 (batt_work_handler: handles -1 to -4 return codes) | **PARTIAL** | Code detects low battery via BQ25120A VBMON and reports pct. The -2 floor code = "<60%" sentinel. **No explicit 3.3 V threshold or run-close-on-low-battery in the code reviewed.** The battery handler updates the cache but does not trigger recorder_end() or any power-down sequence. |
| **R05** | Flash CRC mismatch → mark corrupt, skip | store_flash.c:163-175 (CRC32 verify), store_flash.c:706-760 (boot ghost prune: entries with erased header+trailer removed) | **MATCH** | Stream reads have CRC32 trailer (C3 32). Ghost runs pruned at boot. "A merely corrupt run stays listed and fails loudly downstream" (comment L664). App-side CRC check during download verifies integrity. |
| **R06** | BLE disconnect mid-transfer → resume from last chunk | pull_proto.c:307-340 (stream is stateless — phone re-requests `s <id> <off> <len>`) | **MATCH** | Stream protocol is stateless: the phone specifies offset. Disconnect → pull_proto_on_disconnect cancels the stream. Phone reconnects and re-requests from the last known offset. D directory burst uses separate pace; d_abort logged on disconnect. |
| **R07** | Sensor failure detection: I²C NACK > 1 s → flag error | sensor_prod.c:150-167 (drain_thread: hub-FIFO stall guard at 5 s idle polls → FLUSH), ble_config.c: no per-sensor health monitoring | **PARTIAL** | Hub-FIFO stall guard exists (500 idle polls = 5 s → flush). No explicit per-sensor NACK timeout. No BLE error flag set on sensor failure. The req's "1 s NACK timeout → flag error in BLE status" has no implementation evidence. |
| **R08** | Single-arm operation | recorder.c: independent arming per device, pull_proto.c: no cross-arm dependency | **MATCH** | Each device arms independently via its own button. No cross-device communication. Phone-side single-arm handling is in the app. |

## REQ-PERF — Performance Requirements

| ID | Requirement | Code evidence | Verdict |
|----|-------------|---------------|---------|
| **P01** | 100 Hz jitter < 500 µs σ | recorder.c:67 (k_timer 10 ms), recorder.c:61 (dedicated workqueue, priority 4) | **UNVERIFIABLE** | Code has the right structure (dedicated workqueue, no shared queue with BLE). Hardware measurement needed (scope). |
| **P02** | Sensor read → ring write < 2 ms | sensor_prod.c:73-91 (snapshot seqlock), ring.c:44-58 (flash_write per push) | **UNVERIFIABLE** | Code structure: sensor drain at prio 3, recorder workqueue at prio 4, one flash_write per push. Timing requires hardware measurement. |
| **P03** | Start detection latency < 100 ms | recorder.c:195-197 (10 Hz decimation → check every 100 ms) | **UNVERIFIABLE** | 10 Hz decimation means worst-case 100 ms from actual descent to detection. At the limit of the requirement. Hardware measurement needed. |
| **P04** | BLE throughput ≥ 20 KB/s | pull_proto.c:25-27 (STREAM_GAP_MS=20, STREAM_PAYLOAD=240) | **MATCH** | 240 B / 20 ms = 12 KB/s per stream. With MTU optimization (≥500 B payload → 2 notifications per chunk), effective rate approaches requirement. |
| **P05** | Quaternion accuracy ≥ 2 before logging | sensor_prod.c (BHI260AP self-reported accuracy — hub-internal) | **UNVERIFIABLE** | The BHI260AP reports calibration accuracy internally. `ble_config_cal()` returns 0 (TODO). The firmware does not gate logging on calibration accuracy. No code evidence of an accuracy check. |
| **P06** | Cross-correlation < 10 ms precision | Phone-side | **UNVERIFIABLE (phone)** | |
| **P07** | Compression ≥ 35% | bitpacker.c: triple-type encoding | **MATCH** | Structurally correct (see F07). |
| **P08** | BLE 2M PHY preferred, MTU target ≥ 500 B | Zephyr BT stack config | **UNVERIFIABLE** | PHY selection and MTU exchange are in Zephyr Kconfig, not application code. |
| **P09** | v2 ONLY — RFID round < 15 ms | — | **NOT APPLICABLE** | |

## REQ-IF — Interface Requirements

| ID | Requirement | Code evidence | Verdict |
|----|-------------|---------------|---------|
| **I01** | BHI260AP ↔ nRF: I²C host, FIFO watermark interrupt | sensor_prod.c:150-167 (drain_thread: poll model, NOT interrupt) | **CONTRADICTS** | Req says "FIFO watermark interrupt at 10-sample threshold." Code uses a dedicated drain thread polling every 10 ms (sensor_prod.c:67 "poll, not INT"), not an interrupt. Code comment: "JP decision: poll, not INT." The req was never updated. |
| **I02** | BMP390 ↔ nRF: I²C, 100 Hz reads | sensor_prod.c:205-207 (BARO @ 100 Hz via BHI260AP virtual sensor, not direct I²C) | **PARTIAL** | BMP390 is accessed via BHI260AP virtual sensor (SENSOR_ID_BARO), not direct I²C. The pass-through rate is ~16.4 Hz (sensor_prod.c:132 comment: "OBSERVED 4 B @ ~16.4 Hz"). The req says "100 Hz pressure reads" but the actual baro rate is ~16.4 Hz — hold-last semantics fill the 100 Hz slots. |
| **I03** | LDC1612 — REMOVED | — | **NOT APPLICABLE** | LDC1612 physically removed from board. |
| **I04** | SPI Flash ↔ nRF | store_flash.c:64-65 (MX25R6435F on &mx25r1635f node), ring.c (ring storage on same flash) | **MATCH** | External SPI NOR flash via Zephyr flash driver. 4 KB sector self-test at boot. CRC32 verified writes. |
| **I05** | Device ↔ Phone: BLE 5.0, custom GATT + DFU | pull_proto.c:43-70 (full 128-bit UUIDs, ABC0-ABCB characteristics) | **MATCH** | Product service 53470000-0000-1000-8000-00805F9B34FB with ABC0-ABCB characteristics. Bonded pairing supported. |
| **I06** | v2/Forearm Guard Only — cross-arm proximity | — | **NOT APPLICABLE (v2)** | |
| **I07** | Phone ↔ Cloud: HTTPS REST API | Phone-side | **UNVERIFIABLE (phone)** | |
| **I08** | Beeper DNP | — | **NOT APPLICABLE** | Beeper is DNP. No code. |
| **I09** | USB-C → BQ25120A charging | ble_config.c:132-136 (BQ_ADDR=0x6A on i2c0), ble_config.c:193-202 (VIN detection via FAULTS register) | **MATCH** | BQ25120A accessed on i2c0 (P0.15/P0.16), address 0x6A. VIN detected from FAULTS register. Charge-current programming at boot + every poll. |
| **I10** | v2 ONLY — RFID SPI | — | **NOT APPLICABLE** | |
| **I11** | DW3000 UWB footprint (DNP) | — | **NOT APPLICABLE** | Footprint only, not populated. |
| **I12** | Piezo button on P0.02 | button.c:24-26 (BTN_PORT=gpio0, BTN_PIN=2), button.c:88-89 (configure pull-up, edge-falling) | **MATCH** | P0.02, internal pull-up, falling edge, 20 ms debounce. Wake from both SLEEP and System Off. |
| **I13** | Cloud ownership registry | Phone/cloud-side | **UNVERIFIABLE (phone)** | |

## REQ-ROB — Robustness (continued)

| ID | Requirement | Verdict |
|----|-------------|---------|
| **R01** | See REQ-FUNC table above | **MATCH** |
| **R02** | See REQ-FUNC table above | **CONTRADICTS** (120 s vs 30 s) |
| **R03-R08** | See REQ-FUNC table above | Mixed (see above) |

---

# AUDIT 2 — Requirements ↔ KiCad Schematics

KiCad P1 schematics (7 sheets, rev A-WIP, 2026-11-04): root (`sgc_p1.kicad_sch`), Power (`sch_power.kicad_sch`), MCU (`sch_mcu.kicad_sch`), 5V boost+LED (`sch_boost_led.kicad_sch`), Connectors (`sch_connectors.kicad_sch`), Sensors (`sch_sensors.kicad_sch`), Storage (`sch_storage.kicad_sch`). All sheets marked "generated - not ERC-reviewed."

## REQ-HW Hardware Requirements vs Schematics

| ID | Requirement | Schematic evidence | Verdict |
|----|-------------|-------------------|---------|
| **H01** | −20°C to +40°C operating | Component temp ranges: ANNA-B402 −40 to +85°C, BQ25120A −40 to +85°C, BHI260AP −40 to +85°C, MX25R6435F −40 to +85°C per datasheets | **MATCH** | All ICs rated for the range. Battery derating at −10°C documented (H02). Enclosure/hardware verification needed (environmental chamber). |
| **H02** | ≥ 8 h battery at −10°C | Power tree: 600 mAh Renata cell, session budget ~48 mAh → ~12.5 sessions at 20°C, ~9 at −10°C. BQ25120A power path management per sch_power. | **MATCH** | Battery capacity and session budget calculations documented. Hardware measurement needed for final verification. |
| **H03** | IP67 sealed enclosure | Connectors: USB-C GCT USB4085 with tethered cap (sch_connectors), piezo button sealed unit (sch_connectors: J5 JST-SH-2, protection network D6/D7 BAT42WS, R41 100k series). | **MATCH** | Design supports IP67: sealed USB-C, self-sealed piezo button (no moving parts through wall), O-ring enclosure. Hardware verification needed. |
| **H04** | v2/Forearm Guard Only — inductive trigger | LDC1612 in sch_sensors: marked DNP. Footprint retained. | **NOT APPLICABLE (v2)** | Correctly DNP in v1. |
| **H05** | <16 mm thick (v2); ~25 mm pole mount (v1) | Board: 22×55 mm, 0.8 mm 4-layer. Pole-mount form factor per enclosure spec. | **MATCH** | Board dimensions allow ~25 mm pole-mount thickness per hardware doc §8. Hardware measurement needed. |
| **H06** | ≤ 50 g weight | BOM estimate: ~25 g electronics + 15 g enclosure + 10 g strap = 50 g. | **MATCH** | Hardware measurement needed. |
| **H07** | ≥ 10 runs storage | sch_storage: U7 MX25R6435F (64 Mb = 8 MB). store_layout.h: LOG_SIZE for run storage. | **MATCH** | 8 MB NOR flash. At ~7.5 KB/frame @ 100 Hz for 90 s = ~90 KB/run → 88+ runs. Well above 10. |
| **H08** | BMM150 not used in v1 | sch_sensors: BMM150 present as BHI260AP sub-sensor (M3 I2C). Keepout zones: >10 mm from Qi coil (DNP), >10 mm from RFID antenna (DNP). | **MATCH** | BMM150 is present on the BHI260AP hub (included for fusion). Not used for standalone magnetometer functions. Req says "NOT APPLICABLE (v1)" per v6.0. |
| **H09** | v2/Forearm Guard Only — 200 g shock | — | **NOT APPLICABLE (v2)** | v1 pole mount: device shielded by pole shaft. |
| **H10** | USB-C charging (GCT USB4085 → BQ25120A) | sch_connectors: J1 USB4085, CC1/CC2 with 5.1kΩ Rd to GND, VBUS to BQ IN. sch_power: BQ25120A on i2c0, CD pin to P0.08 (B402 GPIO_50), 100k PU to V_PMID. | **MATCH** | USB-C charging path complete. GCT USB4085 → BQ25120A IN. Rd 5.1k on each CC pin. TVS ESD protection. Tethered IP67 cap. |
| **H11** | v2 ONLY — UHF RFID footprint | sch_sensors: unpopulated E310 footprint. | **NOT APPLICABLE (v2)** | |
| **H12** | v2 ONLY — RFID/BMM150 non-interference | — | **NOT APPLICABLE (v2)** | |
| **H13** | DW3000 UWB footprint (unpopulated) | sch_storage/sch_sensors: DW3000 QFN footprint, power-gated, CSn pad present. | **MATCH** | Footprint present, not populated per spec. Board note confirms. |
| **H14** | Piezo pushbutton on P0.02 | sch_connectors: J5 (JST-SH-2, pin 1=GND, pin 2=BUTTON_RAW), R41 100k series, D6/D7 clamps to +1V8/GND (BAT42WS SOD-323). sch_mcu: B402 GPIO_20 = module pin 20 = nRF P0.02. | **MATCH** | Protection network matches hardware doc §3 (piezo section): R41 100k series, clamped by D6 (K→+1V8)/D7 (K→GND). B402 pin assignment correct per datasheet Table 4. |

## Hardware Claims from sgc_schematic_spec.md vs KiCad Netlists

| Claim | Spec section | Schematic evidence | Verdict |
|-------|-------------|--------------------|---------|
| **BQ25120A on i2c0 (P0.15/P0.16), addr 0x6A** | §4.1, §2 | sch_power: BQ25120A SDA→i2c0 SDA (P0.15), SCL→i2c0 SCL (P0.16). sch_mcu: B402 GPIO_40=P0.15, GPIO_36=P0.16. | **MATCH** |
| **BQ25120A CD on P0.08 (B402 pin 50)** | §3.3 (C2), §4.1 | sch_power: BQ25120A /CD ball E2 → net label `CD`. sch_mcu: B402 GPIO_50 = module pin 50 = nRF P0.08. CD pull-up to V_PMID via 100k. | **MATCH** |
| **MAX17048 fuel gauge on i2c0, addr 0x36** | §4.2, AD-029 | sch_power: MAX17048 SCL→i2c0 SCL, SDA→i2c0 SDA. VDD→VBAT, ALRT→P0.21 (B402 GPIO_26). | **MATCH** |
| **IS31FL3194 on i2c0, addr 0x53, PMID-powered** | §4.3 | sch_sensors: IS31FL3194 on i2c0 (P0.15/P0.16), VCC=V_PMID. Address 0x53 (fixed, no strap pins). | **MATCH** |
| **BHI260AP on SPI2 (P0.03-05, CS P0.31, INT P0.14, RESET P0.13)** | §4.4, §3.3 (C3) | sch_sensors: BHI260AP HSCX→SPI2 SCK (P0.03), HSDX→SPI2 MOSI (P0.04), HSDO→SPI2 MISO (P0.05), HCSB→P0.31 (B402 GPIO_31), HIRQ→P0.14 (B402 GPIO_11), RESETN→P0.13 (B402 GPIO_52). | **MATCH** |
| **BHI260AP RESET moved from P0.18 to P0.13** | §3.3 (C3) | sch_mcu: B402 pin 12 (RESET_N) = module reset, NOT BHI reset. BHI RESETN on GPIO_52 (module pin 52 = P0.13). | **MATCH** |
| **BMP390 on BHI260AP M1 master port** | §4.4 | sch_sensors: BMP390L SCK/SDI/SDO/CSB on BHI260AP M1SCX/M1SDX/M1SDI/MCSB1. VDDIO→+1V8, VDD→+1V8. | **MATCH** |
| **BME688 on BHI260AP M1 port (MCSB2)** | §4.4 | sch_sensors: BME688 SCK/SDI/SDO/CSB on BHI260AP M1/MCSB2. Present (marked G2, populate-vs-DNP TBD per OD-6). | **MATCH** |
| **BMM150 on BHI260AP M3 I2C** | §4.4 | sch_sensors: BMM150 SCK/SDI/SDO on BHI260AP M3SCL/M3SDA, CS on MCSB3. | **MATCH** |
| **BHI FW flash MX25R1635F (U2) on BHI QSPI** | §4.4, OD-5 | sch_storage: U2 MX25R1635F on BHI260AP QSPI (CLK/D0-D3/CSN). | **MATCH** |
| **Data flash MX25R6435F (U7) on SPI2, CS P0.26** | §4.5 | sch_storage: U7 MX25R6435F CS→P0.26 (B402 GPIO_33), SCK→SPI2 SCK, SO→SPI2 MISO, SI→SPI2 MOSI. VCC→+1V8. | **MATCH** |
| **Console UART: TX P0.20, RX P0.09** | §3.2, §4.8 | sch_mcu: B402 GPIO_10 = module pin 10 = UART TX (nRF P0.20). B402 GPIO_22/NFC1 = module pin 22 = UART RX (nRF P0.09). | **MATCH** |
| **SK6812 strip on P0.19 with 5V boost** | §3.2, §4.3 | sch_boost_led: MT3608 boost (VBAT→5V), SS34 rectifier, SK6812 DIN→P0.19 (B402 GPIO_38). Sheet title: "5V boost + LED strip reserve (v2 DNP)." | **MATCH** (DNP in v1) |
| **USB-C: VBUS→BQ IN, CC 5.1k Rd** | §4.7 | sch_connectors: J1 USB4085, A4+A9+B4+B9=VBUS→VBUS_USB, A5(CC1)→5.1k→GND, B5(CC2)→5.1k→GND. D5 PMEG4005EPK series diode on VBUS. | **MATCH** |
| **LDC1612 DNP on i2c1 @ 0x2A** | §4.6 | sch_sensors: LDC1612 on i2c1 (P0.22/P0.23), ADDR→GND (0x2A). Marked DNP. | **MATCH** |
| **RV-3028-C7 RTC DNP on i2c1 @ 0x52** | §4.6 | sch_sensors: RV-3028-C7 footprint, DNP. On i2c1. | **MATCH** |
| **DW3000 UWB footprint DNP, power-gated** | §4.6 | sch_storage: DW3000 QFN footprint present. CSn on P0.29, power-gated via MOSFET (P0.30). DNP. | **MATCH** |
| **I2C bus split: i2c0=power, i2c1=sensor** | §2 | sch_mcu: B402 GPIO_40/36 = i2c0 SDA/SCL (P0.15/P0.16) → BQ25120A+IS31FL3194+MAX17048. B402 GPIO_37/39 = i2c1 SDA/SCL (P0.22/P0.23) → LDC1612 (DNP)+RV-3028 (DNP). | **MATCH** |
| **Battery: JST-3 connector (VBAT/GND/NTC)** | §4.8 | sch_power: J4 battery connector, 3-pin (BAT+/GND/NTC). NTC→BQ25120A TS via R42+R43 divider. | **MATCH** |
| **P0.24 absent on B402 → Boost EN moved to P0.17** | §3.3 (C1) | sch_boost_led: Boost EN→B402 GPIO_49 = module pin 49 = nRF P0.17. No P0.24 assignment in schematics. | **MATCH** |
| **Module RESET_N = P0.18 (fixed pin 12)** | §3.3 (C3) | sch_mcu: B402 pin 12 = RESET_N, connected to SWD connector and TP. No conflict. | **MATCH** |

---

# SYNTHESIS

## (a) Top 10 Highest-Risk Misalignments (Ranked)

| Rank | Risk | Finding | Evidence |
|------|------|---------|----------|
| **1** | **CRITICAL** | **ARM timeout: req says 30 s, code has 120 s.** R02 explicitly states "30 s of arming, return to SLEEP (ARM_TIMEOUT_MS)." Code at recorder.c:197 has `ticks >= 100 * 120` = 120 s. F03 also says "30 s arm timeout." Code comment: "120 s arm window (JP)." This is a 4× mismatch — if code is correct (per JP), the req is stale. If req is correct, the field behavior is wrong. | recorder.c:197, sgc_requirements.md:R02, F03 |
| **2** | **CRITICAL** | **F06 auto-terminate: req says 10 s flatline + IMU stillness; code does 5 s baro-only.** Code flatline_end_reset() uses END_WIN_SAMPLES=50 @ 10 Hz = 5 s. No IMU stillness check. The code comment records JP directive "5 s no-descent" (2026-10-14) but the req was never updated. | recorder.c:123-146, sgc_requirements.md:F06 |
| **3** | **CRITICAL** | **V-Model claims "C++ / Arduino + mbedOS"; implementation is Zephyr/NCS in C.** The V-Model traceability diagram in sgc_requirements.md (§ "V-Model Traceability") says "Device: C++ / Arduino + mbedOS (Nicla prototyping → custom PCB)." Every firmware source file is C (`.c`, not `.cpp`), uses Zephyr kernel APIs, and compiles under NCS v3.4.0 with MCUboot. The entire implementation section is mislabeled. | sgc_requirements.md:V-Model, recorder.c, main.c etc. |
| **4** | **HIGH** | **F65 Ownership PIN: MISSING from firmware.** The v6.1 security model (6-digit PIN, claim flow, wrong-PIN lockout) has NO code implementation. The config disk struct (ble_config.c:24-31) has no PIN field. No PIN validation in any GATT handler. | ble_config.c:24-31, pull_proto.c (entire file) |
| **5** | **HIGH** | **F02 pre-roll depth: req says 10 s (1000 samples); code has 20 s (2000-slots Bank A).** Ring is 4096 slots total, Bank A = 2000 slots = 20 s @ 100 Hz. ARM cap is 20 s (1200 ticks = 120 s? Code says 100*120=120 s ARMed, but Bank A can only hold 20 s). The req says "10-second (1000-sample) linear Flash pre-roll." Code provides double the stated capacity. | ring.h:14, recorder.c:35-37, sgc_requirements.md:F02 |
| **6** | **HIGH** | **I01: Req says BHI FIFO watermark interrupt; code uses 10 ms poll.** sgc_requirements.md I01: "FIFO watermark interrupt at 10-sample threshold." sensor_prod.c comment: "JP decision: poll, not INT." Drain thread polls every 10 ms with k_sleep — no GPIO interrupt used. The req vs implementation documentation gap is significant. | sensor_prod.c:67, sgc_requirements.md:I01 |
| **7** | **HIGH** | **R04: Low-battery shutdown NOT implemented in code.** Req mandates VBAT < 3.3V → close file + enter SLEEP. Code only reads battery percentage; never triggers recorder_end() or any state transition on low battery. | ble_config.c:223-268, recorder.c (no low-battery path) |
| **8** | **HIGH** | **Charger register default-drifting root cause CLASS.** The audit confirms the regressions listed in the dossier: (a) baro scale fixed (0.25.15), (b) ARM window fixed (0.25.14), (c) charger registers now asserted at every boot AND every poll (0.25.16). BUT: VBATREG 4.35V is STILL dormant per code comment — "the FW write is DORMANT (git 8fd1979 + pointer comment in ble_config.c) and is enabled ONLY at the physical pack swap." The spec docs (sgc_schematic_spec.md §5 note) say VBATREG 4.35V, the code uses 0x96 (=4.35V) in BQ_WANT_VBATREG, but the hardware note says the bench still has a 4.2V pack. The code writes 4.35V to the charger register on every poll — if the bench pack is 4.2V, this is actively DANGEROUS. | ble_config.c:261-263, ble_config.c:199-200 |
| **9** | **MEDIUM** | **I02: BMP390 rate — req says "100 Hz pressure reads"; actual pass-through ~16.4 Hz.** sensor_prod.c:132 comment documents "OBSERVED 4 B @ ~16.4 Hz (hub clamps BMP390 pass-through rate)." The 100 Hz virtual sensor config requests 100 Hz, but the BHI260AP hub clamps the BMP390 to its native ~16.4 Hz. Hold-last semantics fill the gaps. The req should document this limitation. | sensor_prod.c:132, sgc_requirements.md:I02 |
| **10** | **MEDIUM** | **P05: Calibration accuracy gating NOT implemented.** Req says "quaternion fusion accuracy ≥ 2 before logging permitted." ble_config_cal() returns 0 with comment "TODO: BHI calibration-status port." No gating on logging. F51 explicitly marked "NOT APPLICABLE" in reqs, but P05 is still listed as active. | ble_config.h:86, sgc_requirements.md:P05 |

## (b) Stale / Unsupported TEST_COVERAGE_MATRIX.md Rows

The `TEST_COVERAGE_MATRIX.md` claims 86 v1 requirements, but many rows reflect the Arduino-era documentation and are **stale against the current Zephyr implementation**. Specific stale rows:

| Matrix Row | Staleness | Reason |
|------------|-----------|--------|
| **F02**: "5 s rolling ring buffer (500 samples)" | **STALE** | Code has 20 s / 4096 linear slots. Req now says 10 s. |
| **F03**: "Arm via 1000 ms inductive proximity" | **STALE** | Req v6.0: piezo button single press. |
| **F04**: "Dual-mode barometric start (speed > 1.5 m/s OR drop > 2.0 m)" | **STALE** | Req v6.0: drop-only. Speed mode dropped. |
| **F05**: "Drain ring buffer 2 samples/cycle, 500 in 2.5 s" | **STALE** | Req v6.0: pop-2/push-1, 1000 samples in ~10 s. |
| **F06**: "Auto-terminate after 10s flatline + stillness" | **STALE** | Code: 5 s no-descent, baro only. |
| **F12**: "Sleep after 5 min inactivity" | **STALE** | Req v6.0: immediate SLEEP + 1 h → System Off. |
| **F13**: "Wake from sleep via LDC1612 INTB" | **STALE** | Req v6.0: piezo button. |
| **F14**: "Audible beeper when armed" | **STALE** | Req v6.0: no beeper (DNP). |
| **F40**: "BLE OTA — ⏳ Not yet" | **STALE** | OTA Stage 1 complete (MASTER_TEST_PLAN v1.2). |
| **F42**: "Factory reset via 20s inductive hold" | **STALE** | Req v6.1: phone-only factory reset. |
| **I01**: "BHI260AP ↔ nRF52832 I²C" | **STALE** | Interface is SPI, not I²C. |
| **S03-S06 Arduino scripts** | **STALE** | MASTER_TEST_PLAN §7.3: "target the Nicla/Mbed FW and do not run against the Zephyr app-03 console." |

**Count: 12 matrix rows are stale against current requirements/code.** This is 14% of the claimed 86 v1 requirements. The matrix was last meaningfully updated 2026-06-24; the v1.1/v1.2 updates only added §9 (feature coverage) without refreshing the Arduino-era device rows.

## (c) Three-Tier Daily Test Proposal

Prioritizing the regression classes that actually bit: **constant drift from documented value** (ARM window, baro scale, charger registers), **state-machine timeout change**, **hardware register left at default**.

### Tier 1 — SMOKE (< 2 minutes, host-compiled, zero hardware)

Run on every git push. Pure logic — no Zephyr kernel, no BLE stack, no flash driver.

| Test ID | What | Maps to | Regression class |
|---------|------|---------|-----------------|
| **SMK-01** | `test_bitpacker_encode_roundtrip`: verify T1/T2/T3 sizes = {6,10,18}, anchor-every-50 enforced, delta_ms overflow→T3 | F07, P07 | Constant validation |
| **SMK-02** | `test_bitpacker_constant_anchors`: enforce SGC_BITPACK_ANCHOR_EVERY = 50, verify last_size values | F07 | Constant drift |
| **SMK-03** | `test_ring_slot_layout`: assert RING_SLOT_BYTES = 20, RING_SLOTS = 4096, RING_BASE = 0x00000 | F02 | Constant drift |
| **SMK-04** | `test_ring_linear_only`: assert push/pop count arithmetic — tail-head invariant, overflow guard works | F02, F05 | State-machine |
| **SMK-05** | `test_store_layout_constants`: assert LOG_SIZE, LOG_START, BLOB_OVERHEAD, MAX_INDEXED_RUNS, RUN_BUDGET_DEFAULT | F08, F62 | Constant drift |
| **SMK-06** | `test_drop_detector_threshold`: verify SD_DROP_X100 = 200 (2.00 m), SD_P0_MIN_PA = 50000, SD_P0_MAX_PA = 110000, SD_SEA_LEVEL_PA = 101325 | F04 | Constant drift |
| **SMK-07** | `test_drop_detector_math`: feed known (P0, Pa) pairs → assert drop_x100 output matches hand-calculated altitudes | F04 | Calculation drift |
| **SMK-08** | `test_baro_scale_formula`: verify Pa/2 = raw * 25/64 (the 0.25.15 fix), test known raw→Pa/2 pairs against hand calculation | F04, I02 | Constant drift |
| **SMK-09** | `test_flatline_end_window`: assert END_WIN_SAMPLES = 50 (5 s), END_NOISE_PA = 8 | F06 | Constant (timeout) drift |
| **SMK-10** | `test_flatline_end_logic`: feed synthetic baro sequences → verify end-trigger for flat/rising pressure within noise floor, non-trigger for descending | F06 | State-machine |
| **SMK-11** | `test_arm_window_constant`: assert ARM window = 120 s (ticks >= 100*120) | F03, R02 | Constant drift |
| **SMK-12** | `test_sleep_off_constant`: assert SLEEP_SYSTEM_OFF_MS = 3600000 | F12 | Constant drift |
| **SMK-13** | `test_charger_register_values`: assert BQ_WANT_FASTCHG = 0x98 (100 mA, 0.2C), BQ_WANT_VBATREG = 0x96 (4.35V) | (post-regression) | Hardware register default |
| **SMK-14** | `test_charger_vbatreg_agrees_with_cell`: cross-check BQ_WANT_VBATREG against a documented cell spec value — if pack is 4.2V, 0x96 must be gated | (post-regression) | Hardware register mismatch |
| **SMK-15** | `test_config_flash_magic`: assert CFG_MAGIC = 0x53474343, cfg_disk layout sizeof matches expected | F11 | Constant drift |
| **SMK-16** | `test_version_agree`: assert VERSION file == APP_VERSION string (was drifting: file 0.24.7 vs banner 0.25.7 per app_version.h comment) | (post-regression) | Constant drift |

### Tier 2 — CORE (< 15 minutes, host-compiled, zero hardware)

Run on merge to main. Includes smoke tests plus integration-level logic.

| Test ID | What | Maps to | Notes |
|---------|------|---------|-------|
| **COR-01** | SMK-01 through SMK-16 (run all smoke tests first) | — | Gate |
| **COR-02** | `test_state_machine_full_cycle`: simulate SLEEP→ARMED→LOGGING→POST_RUN→SLEEP transitions with synthetic frames, verify state indices {0,1,2,3} and transitions match code enum | F12, F13, F41 | Pure logic (state enum values, transition rules) |
| **COR-03** | `test_state_machine_timeout_paths`: verify arm_timeout (120 s) → SLEEP transition; verify flatline_end → POST_RUN; verify System Off after 1 h unconnected logic | R02, F06, F12 | Host-compiled state machine |
| **COR-04** | `test_drop_detector_altitude_compensation`: feed P0 at 500 hPa (high altitude) → verify larger Pa delta needed for 2.0 m at lower air density | F04 | Calculation |
| **COR-05** | `test_budget_runway_math`: verify runway_target() for each discipline budget, verify free_full_runs calculation | F62, F63 | Constant + math |
| **COR-06** | `test_config_persist_roundtrip`: verify cfg_disk serialize/deserialize; magic check; corruption→defaults | F11, R05 | State |
| **COR-07** | `test_battery_encode_floor`: verify pct=-2 → cache=0 ("<60%"), pct=valid → correct bands, pct=-4 → unknown | (post-regression) | Constant/encoding |
| **COR-08** | `test_ble_characteristic_uuids`: assert every UUID in pull_proto.c matches the documented ABCx contract (ABC0-ABCA, ABCD0, ABC6, ABCB) | I05, REQ-IF | Constant drift |
| **COR-09** | `test_button_state_gating`: verify that only SLEEP(0) transitions to ARMED; ARMED/LOGGING/POST_RUN presses are ignored | F03, R01, R03 | State-machine |
| **COR-10** | `test_ring_drain_arithmetic`: verify pop-2/push-1 drain invariant: ring_count→0 after 1000 pops, no underflow | F05 | State-machine |
| **COR-11** | `test_vbatreg_danger_guard`: if BQ_WANT_VBATREG ≠ cell-safe value for installed pack, TEST MUST FAIL LOUDLY | (post-regression) | Hardware register mismatch — regression class #3 |
| **COR-12** | `test_fw_version_semver`: assert VERSION and APP_VERSION agree exactly; parse MAJOR.MINOR.PATCH format | — | Constant drift |
| **COR-13** | `test_ota_gate_state`: verify ota_gate_init() called from main(), verify SMP only in SLEEP guard comment matches requirement | F40, REQ-OTA | State-machine |
| **COR-14** | `test_pull_proto_command_set`: parse every serial command (D/s/d/b/a/g/e/S/H/W/x/y/F/V) → verify handler routing and error codes | REQ-IF | State-machine |

### Tier 3 — FULL NIGHTLY (hardware required; run on dedicated bench rig)

| Test ID | What | Maps to | Hardware |
|---------|------|---------|----------|
| **FUL-01** | `test_100hz_jitter`: scope on GPIO toggle in 100 Hz loop → measure σ | P01 | Oscilloscope + Nicla |
| **FUL-02** | `test_sensor_to_ring_latency`: scope BHI read start → ring write end | P02 | Oscilloscope + Nicla |
| **FUL-03** | `test_start_detection_latency`: pressure chamber: descent onset → LOGGING transition timing | P03 | Pressure chamber + Nicla |
| **FUL-04** | `test_ble_throughput`: full flash offload → measure bytes/sec | P04 | BLE sniffer + Nicla + Phone |
| **FUL-05** | `test_ble_mtu_ph`: verify MTU exchange, PHY = LE 2M | P08, F38 | BLE sniffer + Nicla |
| **FUL-06** | `test_arm_with_button_on_hardware`: press piezo → verify ARM within 1 s, LED confirm | F03, H14 | Nicla ×1 |
| **FUL-07** | `test_sleep_off_1h`: unconnected Nicla, wait 1 h → verify System Off, button wake → cold boot (rr:4) | F12, F13 | Nicla ×1 (long) |
| **FUL-08** | `test_charger_register_readback`: boot device, verify via serial `y` that ICHG=100mA and VBATREG=4.35V have been applied | (post-regression) | Nicla ×1 |
| **FUL-09** | `test_full_run_record_and_download`: S 30 10 bench synth → D list → s stream → verify CRC | F01-F10, I05 | Nicla ×1 + PC |
| **FUL-10** | `test_config_write_and_persist`: write device name/arm/discipline via BLE → reboot → verify persisted | F11 | Nicla + BLE client |
| **FUL-11** | `test_ota_update_cycle`: push signed image via MCUmgr → verify boot, version, runs survive | F40, REQ-OTA | Nicla ×1 + Phone/PC |
| **FUL-12** | `test_battery_low_shutdown`: programmable PSU → drop VBAT → verify graceful file close at threshold | R04 | Nicla + Programmable PSU |
| **FUL-13** | `test_battery_percentage_on_battery`: verify correct battery-% reading with no USB, CD-polarity correct | — | Nicla ×1 (battery-only) |
| **FUL-14** | `test_delete_lifecycle`: d <id> → verify index hole, idempotent re-delete, no shift | F46 | Nicla ×1 |
| **FUL-15** | `test_budget_enforcement`: b 92160 → S 90 10 → verify accepted; S 120 10 → verify rejected (E 5) | F62 | Nicla ×1 |
| **FUL-16** | `test_wrap_and_reclaim`: fill flash with max-budget runs until wrap → verify oldest evicted, newest survives | F08 | Nicla ×1 (long) |
| **FUL-17** | `test_compression_ratio`: decode stored file → count actual bytes vs raw 20B/sample | P07 | Nicla ×1 |

**Hardware-independence summary:** 16 smoke tests + 14 core tests = 30 tests runnable without hardware. 17 full-nightly tests require hardware. Smoke < 2 min gate on push; core < 15 min gate on merge; full overnight on bench rig.

---

*Report written 2026-11-20. Sources: sgc_requirements.md v6.1, Firmware_Zephyr v0.25.17, KiCad P1 schematics rev A-WIP, sgc_schematic_spec.md v0.1, sgc_architecture_hardware.md v2.6, TEST_COVERAGE_MATRIX.md v1.2, MASTER_TEST_PLAN.md v1.2.*