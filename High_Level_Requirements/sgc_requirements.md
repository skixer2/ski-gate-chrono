# SGC — High-Level Requirements (v6.0 — Piezo-Button Pole-Mount)

*2026-09-10 — v6.0: Arming/wake/factory = sealed piezo button (LDC1612 removed from board 2026-08-22; reed switch never built). F04 drop-only start trigger. F02/F05 corrected to Flash pre-roll reality (10 s, pop-2/push-1). F12 immediate SLEEP + System Off after 1 h. F38 MTU 500+. F14 no beeper (DNP). F51/H08 N/A (BMM150 unused). H10/I09 Qi → sealable USB-C. F03a proposed (one-press arms both). JP directives, doc-only — code untouched.*
*2026-08-11 — v5.9: AD-017 pole-mount pivot. H04/H09/I06 → v2 only. H05/H06 relaxed for pole mount. H14/I12 added for reed switch arming.*
*2026-06-09 — v5.7: Post-review fixes — F58–F61, P09, H11–H12, I10, course_gates schema marked v2 only. Silver tier removed.
*2026-06-09 — v5.6: F04 dual start detection (speed OR drop). F52–F57 marked v2 only (RFID unpopulated). F41 SETUP/white reserved for v2.*
*2026-06-08 — v5.5: Cross-arm proximity arming — changed from button-press to forearms-together detection. Athlete brings forearms together at start gate; LDC1612 on each arm detects the approaching passive copper/iron target disc embedded in the opposite arm's strap. H04 updated for cross-arm range (~30 mm approach → near-contact). I06 rewritten as cross-arm proximity. F12/F13/F42 verification language updated. Cross-Arm Arming section rewritten.*

*2026-06-06 — Coherence audit fixes: F41 LED +SETUP/white, stale F41 tag removed, I06 clarified.*

---

## REQ-FUNC — Device Functional Requirements

| ID | Requirement | Verification |
|---|---|---|
| **F01** | The device shall acquire 9-axis fused sensor data (quaternion + linear acceleration) at 100 Hz ± 1% | Oscilloscope on interrupt pin, sample count vs. elapsed time |
| **F02** | The device shall maintain a **10-second (1000-sample) linear Flash pre-roll** while armed. The ARM phase fills up to 3000 slots (30 s cap); at run start the most recent 1000 frames (10 s) are retained as pre-start history. Storage is Flash (FlashRing, 4000×20 B slots at 0x0000–0x13FFF), **not RAM** — the RAM-ring design was abandoned after nRF52 heap pressure (v4.60). *Code refs: flash_layout.h, ARM_FILL_CAP 3000; unit tests S05/S06.* | S05 ring-fill + S06 ring-drain green; dump a run: 10 s of pre-start timestamps precede start, contiguous, no gaps |
| **F03** | The device shall arm upon a **single press of the sealed piezo button** (P0.02, falling edge, 20 ms debounce), independently on each device (device mounts on the ski pole). *The inductive/LDC1612 approach is abandoned: LDC1612 physically removed from the board (2026-08-22); inductance sensing on metallic poles proved unmanageable.* | Bench: press button in SLEEP → ARMED within 1 s, LED chase confirms; accidental press bounded by 30 s arm timeout |
| **F03a** | ⚠️ **PROPOSED — NOT IMPLEMENTED** — Pressing the button on ONE device shall arm BOTH devices (e.g., arm flag broadcast in the advertising payload; peer devices scan-and-arm). Removes the need to press two buttons at the start gate | Design note only; deferred until pull transfer (v1.x) is validated |
| **F04** | The device shall detect run start via **cumulative barometric drop**: vertical drop > 2.0 m from arming pressure P₀. P₀ captured at arming; cumulative drop resets to zero on arm. *The dual-mode descent-speed trigger (v5.6 option A) is dropped — only the drop condition is retained.* | Pressure chamber or field: verify start triggers after 2.0 m descent from P₀; flat ground → no start |
| **F05** | On run start the device shall merge the 10-s Flash pre-roll into the run while simultaneously logging live data: per 10 ms cycle **2 backlog samples are popped while 1 new live sample is pushed** (net −1/cycle) — the 1000-sample backlog fully merges in ~10 s with no dropped samples. *(The old "500-sample drain in 2.5 s" figure ignored ongoing acquisition and is withdrawn — the code has always pushed live samples during drain.)* | File inspection: pre-start timestamps precede start, no chronological gaps; S06 drain unit test green |
| **F06** | The device shall auto-terminate logging after 10 continuous seconds of barometric flatline (±0.3 m/s) combined with IMU stillness | Post-run: verify file is closed, device enters low-power sleep |
| **F07** | The device shall apply adaptive bit-packing (3 packet types) and store compressed data to 2 MB SPI Flash | Decode stored file, verify compression ratio ≥ 35% vs. raw 20B/sample |
| **F08** | The device shall implement a **circular Flash buffer**: when full, the oldest run is automatically overwritten. BLE advertises Flash % used and oldest run age | Fill Flash with 12+ runs, verify 13th overwrites 1st without error |
| **F09** | The device shall expose run metadata (count, timestamps, sizes, Flash %) via BLE GATT service | BLE scanner / phone app: read characteristics |
| **F10** | The device shall transfer selected run files to the phone via BLE with CRC32 integrity verification | Download file, compare CRC; inject bit errors, verify rejection |
| **F11** | The device shall expose read/write configuration parameters via BLE: device name, left/right arm designation, discipline (SL/GS/SG/DH), mount type (arm/pole — reserved for future use) | Phone writes parameter, reads back, verify persistence across reboot |
| **F12** | The device shall enter low-power **SLEEP immediately** when idle — SLEEP is the primary waiting state (entered at boot, after POST_RUN, and on arm timeout; slow 2 s advertising). After **1 h** of unconnected SLEEP it enters **System Off** (deep shutdown, button-wake only). While BLE-connected the device stays in SLEEP. *Supersedes the 5-minute-inactivity rule (code: SLEEP_SYSTEM_OFF_MS = 3600000).* | Boot → status JSON shows st:SLEEP; leave unattended 1 h → serial {"ev":"sm","from":"SLEEP","to":"SYSTEM_OFF"}; button press wakes from System Off |
| **F13** | The device shall wake from sleep on a **piezo button press** (P0.02 GPIO sense, wake source WAKE_BUTTON). System-On SLEEP wake is instant (< 100 ms, no reboot, no sensor re-init); System Off wake = cold boot via button sense (rr:4) | Press button in SLEEP → ARMED without reboot; press button in System Off → clean cold boot |
| **F14** | The device shall **not include an audible beeper in v1**. The transducer footprint is kept **DNP (not populated)** on the custom PCB, reserved in case users request it. Arming feedback is visual only (LED, F41) | PCB inspection: beeper footprint present, unpopulated |
| **F37** | The device shall receive the current UTC date and time from the phone on every BLE connection, before any other GATT interaction | Connect phone, disconnect, wait 1 hour, reconnect — verify time is re-synced |
| **F38** | The device shall negotiate the largest BLE ATT MTU available — **request 517, target ≥ 500 B effective payload per transfer request** (pull transport: a ≥ 500 B logical chunk = 2×244 B notifications if the stack caps MTU at 247, as observed on Cordio). Fall back to LE 1M PHY minimum; prefer LE 2M PHY (P04). *Aligns with PULL_TRANSFER_REDESIGN.md IR-3.* | BLE sniffer / serial log: verify MTU exchange on connect and negotiated value |
| **F39** | The device shall support BLE bonding (LE Secure Connections, Just Works pairing) — bond on first connection, encrypt subsequent sessions. Bonding info persists across sleep (RAM retained) | Pair phone, disconnect, reconnect — verify encryption without re-pairing |
| **F40** | The device shall support BLE OTA firmware update via Nordic DFU service. Firmware images are pushed from the phone | Flash known-good firmware, verify device boots new version; inject corrupted image, verify device rejects and retains previous version |
| **F41** | The device shall include an RGB LED for visual status. **Onboard (Nicla stock):** IS31FL3194 I2C driver on Wire1 (P0.15/P0.16, 0x53) — off = sleep, blue breathing = BLE advertising. **Custom PCB only:** 5× SK6812-mini strip on P0.19 with sequential flowing-point animation (blue slow flowing = uncalibrated, solid blue chase = calibrated ≥2, green fast chase = armed, red fast chase = logging, yellow rapid blink = low battery/error). Co-located with the surface transducer inside the enclosure (light pipes through translucent polycarbonate). *White SETUP mode reserved for v2 (RFID).* | Visual: verify each color/pattern matches state machine state; verify LED changes from flowing to chase after figure-8 calibration |
| **F42** | The device shall detect a **5-presses-within-3-s sequence** on the piezo button as a factory reset trigger: clear BLE bonding, reset device name to default, erase all run data from Flash, restart. *(Supersedes the 20 s inductive/magnetic hold — no inductive sensor exists. Code: FACTORY_PRESS_COUNT=5, PRESS_WINDOW_MS=3000.)* | Bench: 5 quick presses → LED red 3× + reboot; reconnect → bonding lost, name = default, run count = 0; verify < 5 presses does nothing |
| **F52** | ⚠️ **v2 ONLY** — UHF RFID reader footprint on PCB (Impinj E310), unpopulated in v1. Reserved for future gate identification if pressure-only detection proves insufficient | PCB inspection: verify footprint present, no IC populated |
| **F53** | ⚠️ **v2 ONLY** — RFID inventory rounds at discipline rate: SL 5 Hz, GS 10 Hz, SG/DH 20 Hz. Single-tag inventory ≤ 15 ms | Bench: scope RF enable pin (v2 only) |
| **F54** | ⚠️ **v2 ONLY** — RSSI-based nearest-tag selection when multiple gate tags are within range | Bench: two tags at known distances (v2 only) |
| **F55** | ⚠️ **v2 ONLY** — Gate crossing events (tag ID, max RSSI, timestamp) logged alongside sensor frames | Decode run file (v2 only) |
| **F56** | ⚠️ **v2 ONLY** — RFID operation must not impact 100 Hz sensor acquisition | Scope: 100 Hz interrupt cadence (v2 only) |
| **F57** | ⚠️ **v2 ONLY** — SETUP mode: UHF RFID at 1 Hz, tag IDs → BLE notify to phone. LED = white. Exit on phone command or 5 min inactivity | Activate SETUP from phone (v2 only) |

### Wake-from-Sleep Detail (F13)

SLEEP is the primary waiting state — entered immediately at boot, after POST_RUN, and on arm timeout. The device uses **System ON low-power sleep**: RAM retained, 32 kHz LFCLK and RTC running, BLE advertising at a slow 2 s interval.

When the athlete presses the piezo button:

1. P0.02 falling edge (GPIO SENSE) wakes the CPU
2. State machine transitions SLEEP → ARMED (P₀ captured)
3. LED confirms arming (green chase, F41)

Wake latency < 100 ms — no reboot, no sensor re-initialization. RTC time preserved.

After **1 h** of unconnected SLEEP the device enters **System Off** (T4): full shutdown, no advertising, no serial — only a button press (GPIO SENSE low) wakes it into a clean cold boot (rr:4). A BLE connection holds SLEEP indefinitely (no System Off while connected, V5.00).

---

## REQ-PHONE — Phone Application Functional Requirements

| ID | Requirement | Verification |
|---|---|---|
| **F15** | The phone shall pair both arms and associate left/right run files by RTC timestamp proximity (±3 s) | Two devices, simultaneous runs, verify correct pairing |
| **F16** | The phone shall align left/right arm timelines via quaternion dot-product cross-correlation with < 10 ms precision | Synthetic offset injection: verify recovered offset matches ground truth |
| **F17** | The phone shall decompress, store, and display run data (orientation traces, linear acceleration, barometric altitude) | Visual inspection of plotted data |
| **F18** | The phone shall detect pole/gate impacts from linear acceleration spikes and annotate them on the run trace | Controlled bench strikes at known times, verify detected timestamps ± 1 sample |
| **F19** | The phone shall display pole impact timestamps in a table with **right-pole times left-aligned and left-pole times right-aligned** to mimic the slalom course layout. *Rationale: the athlete passes each gate on the opposite side — e.g. starts right of the first gate, leaves it to the left, striking it with the left arm. Then crosses toward the second gate, leaves it to the right, striking it with the right arm. So: left arm hits right gate, right arm hits left gate. When viewing the phone after the run, the athlete imagines seeing the course from the front — the gate side where the pole struck appears on the opposite side of the display: right-gate strike → left-aligned column, left-gate strike → right-aligned column.* | Visual: alternating L/R columns align as stair-step pattern |
| **F20** | The phone shall expose to the user read/write profile parameters: athlete name, ski club name, group name, age, category, and push-to-cloud toggle (true/false). These are used for cloud upload attribution | Enter values, close app, reopen, verify persistence |
| **F21** | The phone shall allow reading and modifying device parameters (F11) via BLE | Change device name on phone, verify device advertises new name |
| **F22** | The phone shall allow comparing two runs side-by-side, with gate timestamps of the first overlaid on the second (right gate left-aligned, left gate right-aligned) | Load two runs, verify aligned gate table renders correctly |
| **F23** | The phone shall push **only gate hit timestamps and barometric data** (altitude + vertical speed at 10 Hz — not raw 100 Hz sensor data) to the cloud when the push-to-cloud flag is true. Raw 100 Hz quaternion + acceleration files remain local on the phone | Toggle flag on, sync run, verify only timestamp rows + barometric_data rows appear in cloud database; verify raw 100 Hz files do NOT leave the phone |
| **F24** | The phone shall retrieve the cloud database endpoint URL from a hardcoded bootstrap address | DNS / HTTP redirect test: change bootstrap target, verify phone follows |
| **F25** | The phone shall allow users of type "trainer" to view all gate times of athletes in their assigned groups | Trainer login → group athlete list → select athlete → view runs + gate times |
| **F26** | The phone shall estimate crossing times **for missed gates** (no impact or RFID registered) via a kinematics-driven pipeline: rotation-speed zero-finding (0.5 Hz low-pass filtered, 0.3 rad/s threshold), local-frame coordinate transform per zero pair (X̂ = zero-to-zero, Ẑ = up ⊥ X, Ŷ = X̂×Ẑ), left/right classification from Y half-plane, and time estimation via geometric interpolation (Case A: pole projects between zeros) or statistical fallback (Case B: learned spatial hit percentage A). Gates with detected impacts or RFID reads use hardware timestamps — the pipeline does not override them | Inject known gate positions in synthetic data, verify estimated timestamps within ± 50 ms of ground truth |
| **F27** | Estimated (guessed) gate crossing times shall be displayed with a trailing **\*** to distinguish them from real impact-detected times | Visual: real times appear normal, guessed times appear as `1.234*` |
| **F28** | The phone shall detect **bananas** (two consecutive gates on the same side) from the alternating L/R pattern and flag them in the display | Feed a known banana course, verify both same-side gates are correctly identified and rendered without breaking the alternating layout |
| **F29** | The run display shall show **barometric altitude on the left** with the **gate timestamp pane immediately to its right**, so the altitude profile provides context for gate times — especially useful when some gates were missed (guessed) | Visual inspection: altitude trace visible alongside timestamps, gate times align vertically with altitude points |
| **F30** | The phone shall calculate **vertical speed** (m/s) from the barometric pressure trace — decimated to 10 Hz (one value per 100 ms) by averaging raw 100 Hz samples over each window, then differentiating the low-pass-filtered pressure | Compare against ground truth (e.g. ski-slope video with known vertical drop and timing) — vertical speed integral must match total altitude change within ±5% |
| **F31** | The phone shall render the vertical speed trace as a graph over the run duration. Gate positions shall be marked with thin vertical lines: **green for right gates, red for left gates**, enabling immediate visual correlation between speed changes and gate crossings | Verify graph displays correctly; green/red gate markers align with detected/guessed gate timestamps; speed peaks and dips align with terrain features |
| **F32** | The phone shall allow comparing vertical speed graphs between two runs (side-by-side or overlaid), with the same reference athlete selection as F22 | Load two runs, verify speed graphs align to a common time/distance axis, differences visually highlighted |
| **F33** | The phone shall render the barometric altitude trace as a graph over the run duration, with gate positions marked as thin vertical lines (green = right gate, red = left gate, per F31 convention) | Verify altitude graph displays correctly; gate markers align with detected/guessed timestamps; altitude profile matches known course elevation |
| **F34** | The phone shall allow comparing barometric altitude graphs between two runs (side-by-side or overlaid), enabling coaches to see line choice and terrain approach differences — e.g. one athlete taking a higher line through a section | Load two runs, verify altitude graphs align to a common time axis, line-choice divergences visible |
| **F35** | The phone shall allow trainers to set a **group-level default visibility** for all runs: `full` (visible to all group members), `athlete_only` (visible to the athlete + their linked parents/friends + trainers), or `denied` (visible only to trainers of the group) | Set group to `denied`, verify a parent in the group cannot see any run data; set to `full`, verify a fellow athlete can see it |
| **F36** | The phone shall allow trainers to **override visibility per individual run** (spot permission), overriding the group default. Useful when a specific run contains sensitive or strategic data | Set group default to `full`, override one run to `denied`, verify only trainers see that run while other runs remain visible |
| **F43** | The phone shall queue cloud uploads when offline and sync automatically when internet connectivity is restored. Queued uploads persist across app restarts | Disable internet, log runs, re-enable — verify all queued runs appear in cloud |
| **F44** | The phone shall display gate numbers (Gate 1, Gate 2, ...) alongside each timestamp in the gate table (F19). Gates are numbered sequentially from start to finish | Visual: each row shows gate number, L/R indicator, and timestamp |
| **F45** | The phone shall allow the athlete to assign a user-editable name/label to each run (e.g., "Race simulation", "Training run 3"). Default name = date + run_number if not set | Enter custom name, save, verify it appears in run list and cloud |
| **F46** | The phone shall allow trainers and master users to delete runs — from the device (Flash), from local phone storage, and from the cloud. Deletion prompts confirmation. Athletes and parents/friends cannot delete | Delete a run as trainer, verify it disappears from device run list, phone storage, and cloud |
| **F47** | The phone shall export run data (gate timestamps, barometric altitude, vertical speed) as CSV and JSON files for external coaching tools. Export includes run metadata (name, date, athlete, course) | Export a run, open CSV in spreadsheet, verify timestamps and data match display |
| **F48** | The phone shall allow browsing and selecting runs stored on the cloud (not just locally) for comparison views (F22, F32, F34). The user may select one run for solo viewing or two runs for simultaneous comparison — runs can be from the same or different athletes | Login, browse cloud runs across group, select two runs from different athletes, verify comparison renders correctly |
| **F49** | When operating in single-arm mode (R08), the phone shall use the device's configured arm side (F11) for gate classification. For GS/SG/DH: gate side is determined directly from arm side. For SL: when comparing two athletes' runs, the phone shall compare same-side arm data only | Compare two SL runs of different arm sides, verify each run shows only its own side's gates; compare two same-side SL runs, verify full comparison |
| **F50** | The phone shall implement a data deletion procedure: upon authenticated request, permanently delete all athlete data (runs, timestamps, barometric_data, profile) from the cloud. Display a prominent, explicit warning to the athlete listing exactly what will be deleted before confirming. This action is irreversible | Initiate deletion, verify warning dialog appears, confirm, verify all cloud records for the athlete return 404 |
| **F51** | ⚠️ **NOT APPLICABLE (v1)** — Magnetometer calibration display dropped: the BMM150 is not used in the v1 product and there is no magnetometer calibration gating. (Quaternion fusion-accuracy gating remains, see P05.) | — |
| **F57.1** | The phone shall support **Mode A (New Course)** course setup: trainer walks the course sequentially from START to FINISH, tapping the phone at each gate position to record it. Each tap captures the phone's barometric pressure (ΔP from START) and GPS position. The phone auto-increments the gate counter. START is always gate 0 — the trainer taps once to confirm START, then walks to and taps each subsequent gate | Walk a known course, tap at each gate, verify recorded gate count = physical gate count; verify gate numbers increment sequentially from 0 |
| **F57.2** | The phone shall support **Mode B (Update Existing Course)** course setup: phone detects the nearest existing gate via dual-signal matching — GPS proximity (±5 m) combined with pressure delta comparison (±50 Pa). The trainer can perform three actions at the detected gate: **Move** (re-record GPS + pressure at the same gate number), **Delete** (remove the gate from the course, renumber subsequent gates), or **Add** (insert a new gate after the current one, shift subsequent gate numbers). Partial update of any subset of gates is supported — the trainer does not need to re-walk the entire course | Load a saved course, move gate 3 to a new position, verify coordinates and ΔP updated; delete gate 5, verify gate 6 → gate 5 (renumbered); add a gate after gate 2, verify new gate 3 inserted, old gate 3 → gate 4 |
| **F57.3** | The phone shall provide a **dual course view**: a graphical map view (when GPS is available, showing gate positions on a map) and a text list view (always available, showing gate number + ΔP + altitude). A toggle button switches between the two views. The text list is the fallback when GPS signal is poor or unavailable | Toggle between map and text list views; disable GPS on phone, verify text list still shows all gates; verify toggle button visible in both modes |
| **F57.4** | The phone shall store the course map in a **delta-based format**: START is at gate index 0 with ΔP = 0 (reference pressure). Each subsequent gate stores its pressure delta from START pressure (not from the previous gate). GPS positions are stored as relative vectors (ΔGPS: Δlat, Δlon from the previous gate), minimizing cumulative GPS drift error in the vector chain. Altitude is stored per gate for display purposes | Save a course, inspect stored data: verify gate 0 has ΔP = 0 and no ΔGPS; verify gate N has ΔP = P_N − P_START and ΔGPS = GPS_N − GPS_{N−1}; verify manual calculation matches stored values |
| **F58** | ⚠️ **v2 ONLY** — Phone tracing mode: sets devices to SETUP mode (F57), waits for tag IDs from NFC tap or RFID BLE notify. Either event triggers GPS + tag ID + gate number recording. *v1 course setup uses pressure + GPS sequential recording (see F57.1–F57.4 and sgc_architecture_decisions.md AD-007).* | Enter tracing mode (v2 only) |
| **F59** | ⚠️ **v2 ONLY** — Duplicate tag ID detection: same pole within 5 s → merged. *v1 has no pole tags — not applicable.* | Touch same pole twice (v2 only) |
| **F60** | ⚠️ **v2 ONLY** — Cloud persistence of course gate positions via RFID tag IDs. *v1 uses pressure ΔP + ΔGPS course map format (see sgc_architecture_phone.md §15).* | Complete setup (v2 only) |
| **F61** | ⚠️ **v2 ONLY** — Spatial correlation of run tag IDs with stored gate GPS positions. *v1: gate detection uses pressure + IMU, GPS is sanity-check only.* | Run known course (v2 only) |

---

## REQ-PERF — Performance

| ID | Requirement | Target |
|---|---|---|
| **P01** | 100 Hz loop jitter | < 500 µs standard deviation |
| **P02** | Sensor read → ring buffer write latency | < 2 ms total (leaving ≥ 6 ms for 2× Flash drain) |
| **P03** | Start detection latency (descent → first logged sample) | < 100 ms |
| **P04** | BLE file transfer throughput | ≥ 20 KB/s effective (full Flash offload < 100 s) |
| **P05** | Quaternion fusion accuracy (after calibration) | BHI260AP self-reported accuracy field ≥ 2 (of Bosch's 0–3 scale: 0=unreliable, 1=low, 2=medium, 3=high) before logging permitted |
| **P06** | Cross-correlation T=0 precision | < 10 ms (1 sample) |
| **P07** | Compression ratio (bit-packed vs. raw 20B/sample) | ≥ 35% on typical slalom/GS run |
| **P08** | BLE PHY | LE 2M PHY preferred; fall back to LE 1M. MTU: request 517, target ≥ 500 B payload (247 negotiated fallback — see F38) |
| **P09** | ⚠️ **v2 ONLY** — UHF RFID inventory round latency (single tag) | < 15 ms |

---

## REQ-HW — Hardware & Environmental

| ID | Requirement | Target |
|---|---|---|
| **H01** | Operating temperature range | −20°C to +40°C |
| **H02** | Battery life (active logging, cold-derated) | ≥ 8 hours continuous at −10°C. Reference session: 3 hours, 10 runs |
| **H03** | Enclosure ingress protection | IP67 sealed enclosure with sealed push button (piezo pad, P0.02) and sealable USB-C port (tethered cap). No sound port — no beeper in v1 (F14) |
| **H04** | ⚠️ **v2/Forearm Guard Only** — Inductive trigger detection through polycarbonate shell | Reliable cross-arm proximity detection: target disc approach from ~30 mm down to near-contact. Coil sensitivity designed for forearm-to-forearm distance at start gate — athlete brings forearms together, LDC1612 detects approaching copper/iron disc in opposite strap. v1 pole mount uses reed switch arming instead (see H14) |
| **H05** | Total module thickness (pole mount v1) | ~25 mm (device + enclosure). Not critical for pole-mount form factor. Forearm guard thickness (<16 mm) deferred to v2 |
| **H06** | Total module weight (single device) | ≤ 50 g (electronics + enclosure + strap). Pole mount; weight not critical for shaft attachment |
| **H07** | Onboard storage capacity | ≥ 10 runs per arm before sync required |
| **H08** | ⚠️ **NOT APPLICABLE (v1)** — BMM150 magnetometer is not used; no ferromagnetic-placement constraint. (Historical rationale — Qi coil / transducer shielding — dropped together with H10/I09.) | — |
| **H09** | ⚠️ **v2/Forearm Guard Only** — The device shall withstand accelerations from gate pole impacts without damage or sensor decalibration | **200 g** shock test (typical slalom pole strike 100-200g at the grip); verify functional post-impact. v1 pole mount: device on back side of pole is shielded — pole shaft absorbs impact energy |
| **H14** | The device shall arm via a **sealed piezo push button** (v1 pole mount) on GPIO P0.02 (internal pull-up, falling edge = press, 20 ms debounce). The same button provides wake from SLEEP/System Off (F13) and the 5-press factory reset (F42). *Supersedes the reed-switch concept (never implemented) and LDC1612 arming (removed from board 2026-08-22).* | Bench: press → ARMED; scope: 20 ms debounce effective |
| **H10** | The device shall be rechargeable via a **sealable USB-C connector** (GCT USB4085-GF-A, tethered IP67 silicone cap, CC 5.1 kΩ sink pull-downs, TVS ESD). VBUS → BQ25120 charger. *Qi wireless charging was dropped — space constraints and coil alignment difficulties. See sgc_usb_c_charging_spec.md (HW v4.2).* | Charge via USB-C, verify charging; cap fitted → IP67 maintained |
| **H11** | ⚠️ **v2 ONLY** — The PCB shall include an unpopulated Impinj E310-based UHF RFID module footprint with SPI routing and ceramic antenna keepout. *v1: footprint only, no IC populated.* | Module footprint present; no IC soldered |
| **H12** | ⚠️ **v2 ONLY** — UHF RFID frontend vs. BMM150 magnetometer non-interference. *v1: RFID unpopulated — not applicable.* | BMM150 calibration with RFID active/inactive (v2 only) |
| **H13** | The custom PCB shall include an **unpopulated** Qorvo DW3000 UWB module footprint (5×5 mm QFN, IEEE 802.15.4z) with ceramic chip antenna keepout zone and SPI traces routed to the nRF52832. The DW3000 shall be connected to the shared SPI bus with a dedicated CSn line. VDD rail is routed but not loaded — power-gated via GPIO-controlled MOSFET for zero leakage when unpopulated. Antenna keepout zone shall be > 15 mm from BMM150 and > 10 mm from UHF RFID ceramic antenna. ⚠️ **UWB is NOT a v1 feature. No DW3000 IC is populated. No UWB firmware is written. No UWB testing is required. This is a board layout hedge ONLY — if UWB infrastructure ever materializes, the v2 PCB can populate the DW3000 without a complete board redesign.** | Visual PCB inspection: verify DW3000 QFN footprint, antenna keepout zone, SPI traces, and CSn pad present; verify unpopulated position does not affect board operation. BMM150 + RFID calibration: verify accuracy unchanged with DW3000 footprint unpopulated |

---

## REQ-IF — Interfaces

| ID | Requirement | Description |
|---|---|---|
| **I01** | BHI260AP ↔ nRF52832 | I²C host interface, FIFO watermark interrupt at 10-sample threshold |
| **I02** | BMP390 ↔ nRF52832 | I²C, 100 Hz pressure reads |
| **I03** | ~~LDC1612 ↔ nRF52832~~ — **REMOVED**: LDC1612 physically removed from the board (2026-08-22); no inductive sensing in v1. Pin P0.02 reassigned to the piezo button (I12) | — |
| **I04** | SPI Flash ↔ nRF52832 | SPI, circular run storage with per-file CRC32 |
| **I05** | Device ↔ Phone | BLE 5.0, LE 2M PHY preferred, custom GATT + Nordic DFU service (run list, file transfer, device config R/W, OTA firmware). Bonded pairing with LE Secure Connections |
| **I06** | ⚠️ **v2/Forearm Guard Only** — Left arm ↔ Right arm | **No active radio link.** Each arm arms independently on its own LDC1612 sensor. A passive copper/iron foil disc is embedded in each strap. When the athlete brings forearms together at the start gate, each LDC1612 detects the approaching foil disc on the **opposite** arm's strap via cross-arm inductive proximity. No cross-arm BLE, no scan windows, no radio latency. "Cross-arm" refers to the physical proximity between forearms, not an inter-device link. v1 pole mount uses reed switch arming (see I12) — each device arms independently on its own reed switch |
| **I12** | **Piezo button ↔ nRF52832** (v1 pole mount) | GPIO P0.02 with internal pull-up, falling edge = press, 20 ms debounce, edge interrupt. Wake from System-On SLEEP and from System Off (GPIO SENSE low). Functions: arm (F03), factory reset 5 presses / 3 s (F42), wake (F13). *Same physical pin previously assigned to LDC1612 INTB / reed switch.* |
| **I07** | Phone ↔ Cloud | HTTPS REST API, endpoint URL retrieved from hardcoded bootstrap address |
| **I08** | ~~Beeper ↔ nRF52832~~ — **DNP footprint only** (no beeper in v1, F14); reserved for a future user-requested transducer | — |
| **I09** | Charging input: **USB-C (GCT USB4085) → BQ25120 charger**. Qi receiver dropped (space/alignment — see H10). Tethered IP67 cap, ESD protection | — |
| **I10** | ⚠️ **v2 ONLY** — UHF RFID Reader ↔ nRF52832 | SPI, reader IC controlled by nRF52832. *v1: footprint only, no reader populated.* |
| **I11** | DW3000 UWB ↔ nRF52832 | ⚠️ **FOOTPRINT ONLY — NOT POPULATED, NOT TESTED, NO FIRMWARE.** SPI (shared bus with Flash + RFID), dedicated CSn line. Power-gated (GPIO-controlled MOSFET on VDD_UWB rail, default OFF). Antenna: ceramic chip antenna footprint, tuned for UWB channel 5 (6.5 GHz) or channel 9 (8 GHz). Reserved purely to avoid PCB redesign if UWB is adopted in v2+ |

### Arming: Sealed Piezo Button (v1 Pole Mount) — Why BLE Is Not Needed

Each device mounts on a ski pole (poles are typically metallic — a further reason inductive sensing was abandoned). The athlete presses the device's sealed button at the start gate:

1. Single press (20 ms debounce) → SLEEP → ARMED, P₀ captured
2. Each device arms independently — no cross-device link, no radio latency
3. An accidental press is benign: the 30 s arm timeout (ARM_TIMEOUT_MS) returns the device to SLEEP

Each device operates independently; single-device operation is supported (R08) — gate detection remains functional from one pole's sensor data.

**F03a (PROPOSED — not implemented):** one-press-arms-both via a flag in the advertising payload (peer scan-and-arm). Design direction recorded for v1.x.

### Cross-Arm Arming: LDC1612 (⚠️ historical — abandoned)

*Superseded twice: cross-arm LDC1612 proximity (v5.5) → reed switch (v5.9, never built) → sealed piezo button (current). LDC1612 is physically removed from the board.*

---

## REQ-ROB — Robustness & Edge Cases

| ID | Requirement | Description |
|---|---|---|
| **R01** | False arm reject | Button presses are debounced (20 ms); no hold requirement. An accidental arm is bounded by the 30 s arm timeout. The factory-reset sequence (5 presses in 3 s, F42) is deliberately distinct from the single-press arm |
| **R02** | Aborted start timeout | If barometric descent does not follow within 30 s of arming, return to SLEEP (ARM_TIMEOUT_MS) |
| **R03** | Mid-run button press ignored | Once LOGGING, button input is masked until the run ends (end-detector terminates logging) |
| **R04** | Low-battery graceful shutdown | At VBAT < 3.3V: close current file, write metadata, enter low-power sleep |
| **R05** | Flash write failure recovery | CRC mismatch on readback → mark run as corrupt, skip in BLE list |
| **R06** | BLE disconnect mid-transfer | Resume from last acknowledged chunk on reconnect |
| **R07** | Sensor failure detection | If any sensor stops responding (I²C NACK timeout > 1 s), flag error in BLE status and refuse logging |
| **R08** | Single-arm operation | If only one arm arms/logs, the phone accepts single-arm runs (cross-correlation skipped, gate detection still functional) |

---

## REQ-DB — Cloud Database Schema

### Schema Overview

```
ski_clubs ──< users >──< users2athletes (many-to-many, self-ref)
 │
 ├──< users2groups >── groups (inter-club)
 │
 ├── runs ──< timestamps
 │
 └── courses ──< course_gates
```

- **users2athletes** is a many-to-many self-referencing join on `users`: a kid may have many people following them (multiple parents, multiple trainers), and a parent/friend can follow many kids.
- **groups** are inter-club (regional teams, national squads, etc.) — no direct FK to `ski_clubs`. An athlete can belong to many groups, some club-related, others not.
- Only a **trainer** who is a member of a group can add/remove other members to that group.

### Tables

| Table | Fields | Notes |
|---|---|---|
| **ski_clubs** | id, name, activation_start_date, activation_end_date | Club license window |
| **users** | id, name, age, category, activation_start_date, activation_end_date, user_type | user_type ∈ {athlete, trainer, parent, friend, **master**} |
| **groups** | id, name, activation_start_date, activation_end_date, default_visibility | **Inter-club** training groups (regional teams, etc.). No FK to ski_clubs. `default_visibility` ∈ {full, athlete_only, denied} — default visibility for all runs in this group; overridable per run |
| **users2groups** | user_id, group_id, role | Many-to-many: user ↔ group. role ∈ {trainer, athlete}. Only trainers can add/remove members |
| **users2athletes** | user_id, athlete_id, relationship | **Many-to-many**: follower → athlete. relationship ∈ {trainer, parent, friend}. A kid can have multiple followers; a follower can follow multiple kids |
| **runs** | id, start_datetime, athlete_id, run_number, visibility, name, format_version, lock_version, course_id | `name` = user-editable label (nullable, defaults to date + run_number). `format_version` = uint8, incremented when bit-packing format changes (ensures backward compatibility). `lock_version` = uint32, incremented on every write for optimistic concurrency control. `course_id` = FK → courses.id, nullable. Set by phone on first cloud upload when a matching course is found (±2h from run start, same group via trainer). Null if no match — user can assign manually later |
| **timestamps** | id, timestamp_ms, arm_side, run_id, guessed | Gate timestamps relative to T=0. `arm_side` ∈ {L, R} identifies which pole was struck. `guessed` ∈ {false, true} — true = estimated from orientation data (no impact detected), displayed with `*` on phone |
| **barometric_data** | id, run_id, time_offset_ms, altitude_m, speed_mps | Altitude and vertical speed at 10 Hz (one row per 100 ms). Calculated by the phone from barometric pressure (F30). Uploaded alongside timestamps to the cloud. A single table for both altitude and speed — enables graphing and comparison of position-over-time and speed-over-time from the same dataset |
| **courses** | id, name, created_by, created_at | Course metadata. `created_by` references users.id (trainer or master who set the course) |
| **course_gates** | id, course_id, gate_number, delta_p, delta_lat, delta_lon, altitude, set_by | Gate position recorded during course setup. `delta_p` = ΔP from START pressure (Pa). `delta_lat/lon` = ΔGPS from previous gate. `set_by` = trainer user_id. *v1: no tag_id or NFC/RFID. Uses delta-based format: START at index 0 with ΔP=0, all pressure deltas from START pressure, GPS positions as relative vectors from previous gate. See F57.1–F57.4 and sgc_architecture_decisions.md AD-007.* |

### Access Control

| user_type | Can view | Can manage links |
|---|---|---|
| **athlete** | Own runs only (timestamps + barometric_data) | — |
| **master** (adult athlete, self-responsible) | Own runs only (timestamps + barometric_data). If in a group with visibility = `full`, other group members can also see | Can add parents and friends to himself (via users2athletes) |
| **trainer** | All athletes in assigned groups — timestamps + barometric_data (via users2groups + users2athletes) | Can add/remove athletes to their groups; can add parents to their athletes |
| **parent** | Linked athletes only — timestamps + barometric_data (via users2athletes). Cannot see runs where visibility = `denied` | Can add friends to their linked athletes |
| **friend** | Linked athletes only — timestamps + barometric_data (via users2athletes). Cannot see runs where visibility = `denied` | — |

### Relationship Management Rules

1. **Trainer → Athlete:** Trainer adds themselves to an athlete via users2athletes (relationship = trainer). Trainer can also add parents to that athlete.
2. **Parent → Athlete:** Added by a trainer (or by the athlete themselves if master). Parent can add friends to that athlete.
3. **Friend → Athlete:** Added by a parent (or by the athlete themselves if master). View-only access to the athlete's runs.
4. **Master → Self:** An adult athlete (master) is responsible for themselves. They can add parents and friends to their own athlete record without needing a trainer.

---

## V-Model Traceability

```
REQ-FUNC (F01–F61, F57.1–F57.4)  ◄─────────────── ACCEPTANCE TESTS (field: snow, full system)
       │                                              ▲
REQ-PERF (P01–P09)  ◄─────────────── PERFORMANCE TESTS (bench: scope, jitter)
       │                                              ▲
REQ-HW  (H01–H14)   ◄─────────────── ENVIRONMENTAL TESTS (cold chamber, shock)
       │                                              ▲
       ▼                                              │
  SYSTEM DESIGN           ────────────►  SYSTEM TESTS (BLE service, state machine)
       │                                              ▲
       ▼                                              │
  ARCHITECTURE MODULES    ────────────►  INTEGRATION TESTS (sensors + Flash + BLE)
       │  ├── sgc_architecture_hardware.md  ← HW block diagram, pin map, power tree
       │  ├── sgc_architecture_devices.md   ← Device firmware module interfaces
       │  └── sgc_architecture_phone.md     ← Phone-side processing pipeline
       │                                              ▲
       ▼                                              │
  MODULE DESIGN           ────────────►  UNIT TESTS (bit-packer, ring buffer, GATT)
       │  ├── sgc_bom.md                     ← Bill of materials
       │  └── PCB schematic + layout         ← (pending)
       │                                              ▲
       ▼                                              │
  IMPLEMENTATION
  - Device: C++ / Arduino + mbedOS (Nicla prototyping → custom PCB)
  - Phone: Dart / Flutter
  - Cloud: REST API + PostgreSQL (or SQLite for prototype)
```

---

## Change Log

| Date | Version | Changes |
|---|---|---|
| 2026-06-02 | v2 | Fused-data architecture, interleaved Flash drain, circular buffer, cloud DB schema, full phone feature set |
| 2026-06-03 | v3 | **Renumbered consecutively** F01–F25, H01–H10. **Schema:** users2athletes → many-to-many; groups → inter-club (no FK to ski_clubs); runs → removed arm_side, run_number = daily phone-set counter; added master user type. **Access control:** parents added by trainers, friends added by parents, master self-manages links. Only group trainers can add/remove group members. **F19 (was F15):** rationale corrected — right hand crosses body to hit left gate in GS/SG/DH. **F13 (was F20):** wake-from-deep-sleep feasibility documented (LDC1612 INTB → nRF52 GPIO DETECT). **F14 (was F37):** beeper moved to device section, linked to I08. **Cross-arm arming:** clarified that I06 is a passive inductive target, not BLE — no radio latency issues. |
| 2026-06-03 | v3.1 | **F23:** cloud upload = timestamps only (no raw sensor data). **F26:** missed-gate detection via quaternion orientation trace. **F27:** guessed times displayed with `*`. **F28:** banana detection (two consecutive same-side gates). **F29:** display layout — barometric altitude left, gate timestamps right. **timestamps table:** added `guessed` boolean, clarified this is the only table uploaded to cloud. |
| 2026-06-03 | v3.2 | **F30:** vertical speed from barometric pressure, decimated to 10 Hz. **F31:** vertical speed graph with green/red gate markers. **F32:** speed graph comparison. **F33:** barometric altitude graph with gate markers. **F34:** altitude graph comparison. **DB:** `vertical_speeds` → `barometric_data` (altitude_m + speed_mps). |
| 2026-06-03 | v3.3 | **F19:** rationale corrected — athlete passes gate on opposite side, no body-crossing. **F35:** group-level visibility (full / athlete_only / denied). **F36:** per-run visibility override. **runs:** added `visibility` field. **groups:** added `default_visibility` field. |
| 2026-06-03 | v4 | **Charging:** Qi wireless (no magnetic connector). **Beeper:** surface transducer (IP67). **Shock:** 200g. **New device reqs:** F37 (time sync), F38 (MTU), F39 (BLE bonding), F40 (OTA DFU), F41 (RGB LED), F42 (factory reset). **New phone reqs:** F43 (offline sync), F44 (gate numbers), F45 (run naming), F46 (run deletion), F47 (data export), F48 (cloud run browsing), F49 (single-arm SL), F50 (GDPR deletion). **HW:** H02 session = 3h/10 runs, H08 qualified for Qi coil/transducer shielding, H09 200g. **Perf:** P08 BLE 2M PHY. **IF:** I05 BLE bonding + DFU, I08 surface transducer, I09 Qi coil. **DB:** runs table + name, format_version, lock_version. Stale refs fixed: P02, P07, R04. |
| 2026-06-04 | v5 | **UHF RFID gate detection:** F52 (read passive tags on poles during LOGGING), F53 (discipline-set from phone — SL 5 Hz, GS 10 Hz, SG/DH 20 Hz), F54 (RSSI-based nearest-tag), F55 (gate events logged), F56 (no impact on 100 Hz). **Pole setup:** F57 (device SETUP mode — RFID 1 Hz, LED white, tag ID → BLE), F58 (phone tracing mode — sets devices to SETUP, listens for NFC tap or BLE notify), F59 (duplicate tag detection), F60 (course_gates cloud table), F61 (post-run spatial correlation). **F11:** discipline field added. **P09:** RFID round < 15 ms. **H11:** UHF RFID reader IC + ceramic antenna. **H12:** non-interference with BMM150. **I10:** RFID ↔ nRF52832 via SPI. **H05:** enclosure IS protective shell. **Schema:** users2athletes self-ref, courses + course_gates tables. |
| 2026-06-05 | v5.1 | **DW3000 UWB FOOTPRINT ONLY (not a v1 feature):** H13 (unpopulated Qorvo DW3000 QFN footprint + antenna keepout on custom PCB, power-gated, GPIO CSn). **I11** (DW3000 ↔ nRF52832 via shared SPI). ⚠️ NO DW3000 IC populated. NO firmware. NO testing. NO power draw. Purely a PCB_layout hedge — if UWB snow-cannon infrastructure is built, v2 PCB populates the same footprint without redesign. |
| 2026-06-05 | v5.2 | **Coherence check fixes (HLR ↔ SD, dual-reviewed with Grok):** F23 updated to include barometric_data in cloud upload (was stale: said "timestamps only"). Minor traceability gaps closed (F15 arm pairing, F27 guessed-time *, F49 single-arm SL, F51 calibration display, F59 duplicate tag detection). |
| 2026-06-06 | v5.3 | **F26 overhaul:** Replaced time-gap/max-ω estimation with kinematics-driven pipeline — rotation-speed zero-finding → 0.5 Hz LPF → local-frame coordinate transform → Y half-plane L/R classification → Case A (geometric interpolation) / Case B (statistical A%). Removes `MissedGateEstimator` and `GateClassifier` as standalone modules; replaced by single `GateTimeEstimator` in phone architecture. Updated sgc_architecture_phone.md §5, §7; sgc_system_design.md §5 module decomposition + data flow. |
| 2026-06-06 | v5.4 | **Coherence fixes:** C1: removed stale F41 tag from `runs.format_version`. W5: F41 LED now includes SETUP/white state. I6: I06 rewrote to remove ambiguity — "opposite strap" → "button membrane of each strap". No cross-arm radio. | 
| 2026-06-08 | v5.5 | **Cross-arm proximity arming:** Replaced button-press mechanism with forearms-together proximity detection. Each strap embeds a passive copper/iron target disc; LDC1612 on each arm detects the approaching disc on the opposite arm. No mechanical button, no moving parts — preserves IP67. F12/F13/F42 verification updated. H04 range changed from 1-2 mm to ~30 mm approach distance. I06 rewritten as cross-arm proximity. F41 LED upgraded to 5× SK6812-mini strip with sequential flowing-point animation. Cross-Arm Arming section rewritten. |
| 2026-06-09 | v5.8 | **v1 course setup requirements:** Added F57.1–F57.4 for phone-side course setup (Mode A: New sequential recording, Mode B: Update with GPS+ΔP dual-signal detection + Move/Delete/Add, dual course view, delta-based course map format). Updated course_gates DB comment with delta-format clarification. Updated F58 cross-reference to point to F57.1–F57.4. V-Model traceability extended to F57.1–F57.4. |
| 2026-08-11 | v5.9 | **AD-017 pole-mount pivot:** H04, H09 marked v2/forearm guard only. H05, H06 relaxed for pole mount. H14 (reed switch arming) added. I06 marked v2; I12 (reed switch interface) added. Cross-arm arming section split into v1 reed switch / v2 LDC1612. V-Model updated (H01–H14). |
| 2026-09-10 | v6.0 | **Piezo-button era sync (JP directives — DOC-ONLY, code untouched):** F02/F05 rewritten to Flash pre-roll reality (10 s = 1000 samples; ARM cap 30 s/3000 slots; drain = pop-2 + push-1 live, net −1/10 ms → ~10 s merge). F03 arm = single piezo-button press (LDC1612 removed from board 2026-08-22; inductive dropped — metallic poles unmanageable). F03a PROPOSED: one-press-arms-both via advertising flag. F04 drop-only start trigger (> 2.0 m from P₀; descent-speed mode removed). F12 SLEEP immediately + System Off after 1 h (SLEEP_SYSTEM_OFF_MS=3600000; BLE holds SLEEP). F13 button wake (System-On instant; System Off cold boot). F14 no beeper — DNP footprint only. F38/P08 MTU: request 517, ≥ 500 B payload target (pull redesign IR-3; 247 fallback = 2×244 B notifications). F42 factory reset = 5 presses in 3 s (FACTORY_PRESS_COUNT). F51 N/A (BMM150 unused). H03 sealed button + USB-C cap. H08 N/A. H10 Qi → sealable USB-C (GCT USB4085 → BQ25120). H14/I12 reed → piezo button. I03 LDC removed. I08 beeper DNP. I09 USB-C charging path. R01/R02/R03 button-era wording. Wake-from-Sleep + Arming sections rewritten; cross-arm section marked historical. |

---

*Next: system_design — state machine, BLE GATT service definition, module decomposition.*
