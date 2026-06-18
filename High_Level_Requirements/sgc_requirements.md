# SGC — High-Level Requirements (v5.7)

*2026-06-09 — v5.7: Post-review fixes — F58–F61, P09, H11–H12, I10, course_gates schema marked v2 only. Silver tier removed.
*2026-06-09 — v5.6: F04 dual start detection (speed OR drop). F52–F57 marked v2 only (RFID unpopulated). F41 SETUP/white reserved for v2.*
*2026-06-08 — v5.5: Cross-arm proximity arming — changed from button-press to forearms-together detection. Athlete brings forearms together at start gate; LDC1612 on each arm detects the approaching passive copper/iron target disc embedded in the opposite arm's strap. H04 updated for cross-arm range (~30 mm approach → near-contact). I06 rewritten as cross-arm proximity. F12/F13/F42 verification language updated. Cross-Arm Arming section rewritten.*

*2026-06-06 — Coherence audit fixes: F41 LED +SETUP/white, stale F41 tag removed, I06 clarified.*

---

## REQ-FUNC — Device Functional Requirements

| ID | Requirement | Verification |
|---|---|---|
| **F01** | The device shall acquire 9-axis fused sensor data (quaternion + linear acceleration) at 100 Hz ± 1% | Oscilloscope on interrupt pin, sample count vs. elapsed time |
| **F02** | The device shall maintain a 5-second rolling RAM ring buffer (500 samples) at all times while armed | Timestamped buffer dump, verify 500-sample depth |
| **F03** | The device shall arm the ring buffer upon detecting a continuous **1000 ms ± 100 ms** inductive proximity trigger, independently on each arm | Bench: metal target on LDC1612, measure arm latency |
| **F04** | The device shall detect run start via **dual-mode barometric trigger**: (A) vertical descent > 1.5 m/s sustained for 200 ms, OR (B) cumulative vertical drop > 2.0 m from arming pressure P₀. **Whichever fires first.** P₀ captured at arming; cumulative drop resets to zero on arm. No athlete toggle needed — both conditions monitored simultaneously | Pressure chamber or field: descending elevator / actual slope; flat start → verify drop mode triggers after 2.0m descent |
| **F05** | The device shall flush the 5-second pre-start buffer to Flash while simultaneously logging live data — draining 2 historical samples per 10 ms cycle, completing the 500-sample drain in 2.5 s (250 cycles) without dropping samples | File inspection: verify pre-start timestamps precede start, no chronological gaps |
| **F06** | The device shall auto-terminate logging after 10 continuous seconds of barometric flatline (±0.3 m/s) combined with IMU stillness | Post-run: verify file is closed, device enters low-power sleep |
| **F07** | The device shall apply adaptive bit-packing (3 packet types) and store compressed data to 2 MB SPI Flash | Decode stored file, verify compression ratio ≥ 35% vs. raw 20B/sample |
| **F08** | The device shall implement a **circular Flash buffer**: when full, the oldest run is automatically overwritten. BLE advertises Flash % used and oldest run age | Fill Flash with 12+ runs, verify 13th overwrites 1st without error |
| **F09** | The device shall expose run metadata (count, timestamps, sizes, Flash %) via BLE GATT service | BLE scanner / phone app: read characteristics |
| **F10** | The device shall transfer selected run files to the phone via BLE with CRC32 integrity verification | Download file, compare CRC; inject bit errors, verify rejection |
| **F11** | The device shall expose read/write configuration parameters via BLE: device name, left/right arm designation, discipline (SL/GS/SG/DH), mount type (arm/pole — reserved for future use) | Phone writes parameter, reads back, verify persistence across reboot |
| **F12** | The device shall enter low-power sleep after 5 minutes of inactivity (no arming, no BLE connection). RTC keeps running, RAM is retained | Stopwatch: arm device at 4:59 — still armed; wait 5:01 — verify sleep; bring forearms together → verify instant wake without reboot |
| **F13** | The device shall wake from sleep upon inductance switch activation | Bring forearms together, verify device wakes to IDLE within 2 s. *Feasibility: LDC1612 INTB pin → nRF52832 GPIO interrupt. The nRF52 stays in System ON low-power sleep (WFE) — RAM and RTC preserved, no reboot needed. Wake latency is negligible (< 100 µs from INTB to CPU active).* |
| **F14** | The device shall include an audible beeper to signal that the RAM ring buffer is armed (active). *Hardware: surface transducer driven through the enclosure wall (PWM → piezo element bonded to inner enclosure surface). Preserves IP67 — no sound port needed.* | Arm trigger → beep; disarm → no beep; field: audible through helmet |
| **F37** | The device shall receive the current UTC date and time from the phone on every BLE connection, before any other GATT interaction | Connect phone, disconnect, wait 1 hour, reconnect — verify time is re-synced |
| **F38** | The device shall negotiate BLE ATT MTU to ≥ 247 bytes on connection to enable 244-byte file transfer chunks. Fall back to LE 1M PHY minimum; prefer LE 2M PHY for ≥ 20 KB/s throughput (P04) | BLE sniffer: verify MTU exchange on connect |
| **F39** | The device shall support BLE bonding (LE Secure Connections, Just Works pairing) — bond on first connection, encrypt subsequent sessions. Bonding info persists across sleep (RAM retained) | Pair phone, disconnect, reconnect — verify encryption without re-pairing |
| **F40** | The device shall support BLE OTA firmware update via Nordic DFU service. Firmware images are pushed from the phone | Flash known-good firmware, verify device boots new version; inject corrupted image, verify device rejects and retains previous version |
| **F41** | The device shall include an RGB LED for visual status. **Onboard (Nicla stock):** IS31FL3194 I2C driver on Wire1 (P0.15/P0.16, 0x53) — off = sleep, blue breathing = BLE advertising. **Custom PCB only:** 5× SK6812-mini strip on P0.19 with sequential flowing-point animation (blue slow flowing = uncalibrated, solid blue chase = calibrated ≥2, green fast chase = armed, red fast chase = logging, yellow rapid blink = low battery/error). Co-located with the surface transducer inside the enclosure (light pipes through translucent polycarbonate). *White SETUP mode reserved for v2 (RFID).* | Visual: verify each color/pattern matches state machine state; verify LED changes from flowing to chase after figure-8 calibration |
| **F42** | The device shall detect a **20-second continuous inductive hold** as a factory reset trigger: clear BLE bonding, reset device name to default, erase all run data from Flash, restart | Hold cross-arm proximity 20 s, verify LED flashes red 3×, reconnect → verify bonding lost, name = default, run count = 0 |
| **F52** | ⚠️ **v2 ONLY** — UHF RFID reader footprint on PCB (Impinj E310), unpopulated in v1. Reserved for future gate identification if pressure-only detection proves insufficient | PCB inspection: verify footprint present, no IC populated |
| **F53** | ⚠️ **v2 ONLY** — RFID inventory rounds at discipline rate: SL 5 Hz, GS 10 Hz, SG/DH 20 Hz. Single-tag inventory ≤ 15 ms | Bench: scope RF enable pin (v2 only) |
| **F54** | ⚠️ **v2 ONLY** — RSSI-based nearest-tag selection when multiple gate tags are within range | Bench: two tags at known distances (v2 only) |
| **F55** | ⚠️ **v2 ONLY** — Gate crossing events (tag ID, max RSSI, timestamp) logged alongside sensor frames | Decode run file (v2 only) |
| **F56** | ⚠️ **v2 ONLY** — RFID operation must not impact 100 Hz sensor acquisition | Scope: 100 Hz interrupt cadence (v2 only) |
| **F57** | ⚠️ **v2 ONLY** — SETUP mode: UHF RFID at 1 Hz, tag IDs → BLE notify to phone. LED = white. Exit on phone command or 5 min inactivity | Activate SETUP from phone (v2 only) |

### Wake-from-Sleep Detail (F13)

The device uses **System ON low-power sleep** (ARM WFE), not System OFF. RAM is retained, the 32 kHz LFCLK keeps running, and the RTC peripheral continues counting. The LDC1612 is configured for low-duty-cycle monitoring (~10 Hz, ~50 µA). When the athlete brings forearms together (cross-arm proximity):

1. LDC1612 detects the approaching copper/iron target disc on the opposite arm → threshold crossing → asserts INTB
2. INTB triggers nRF52832 GPIO interrupt → CPU wakes from WFE
3. State machine transitions from SLEEP → IDLE
4. BLE advertising resumes

Wake latency is negligible (< 100 µs from INTB to CPU active) — no reboot, no sensor re-initialization. Total time from forearms-together to BLE advertising: < 100 ms (dominated by BLE stack initialization, which may already be initialized if RAM was retained). RTC time is preserved.

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
| **F51** | The phone shall display the device's magnetometer calibration status (BHI260AP accuracy field 0–3) on the device screen. Show a visual indicator: 🔴 0–1 = calibrate now (do figure-8 motion), 🟢 2–3 = ready. Prevent arming/logging if accuracy < 2 (P05) | Connect uncalibrated device, verify red indicator; perform figure-8, verify transitions to green; attempt to arm while red, verify phone warns/refuses |
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
| **P08** | BLE PHY | LE 2M PHY preferred; fall back to LE 1M. MTU ≥ 247 bytes negotiated on connect |
| **P09** | ⚠️ **v2 ONLY** — UHF RFID inventory round latency (single tag) | < 15 ms |

---

## REQ-HW — Hardware & Environmental

| ID | Requirement | Target |
|---|---|---|
| **H01** | Operating temperature range | −20°C to +40°C |
| **H02** | Battery life (active logging, cold-derated) | ≥ 8 hours continuous at −10°C. Reference session: 3 hours, 10 runs |
| **H03** | Enclosure ingress protection | IP67 (sealed, no mechanical buttons or ports). Beeper uses surface transducer bonded to inner enclosure wall — no sound port |
| **H04** | Inductive trigger detection through polycarbonate shell | Reliable cross-arm proximity detection: target disc approach from ~30 mm down to near-contact. Coil sensitivity designed for forearm-to-forearm distance at start gate — athlete brings forearms together, LDC1612 detects approaching copper/iron disc in opposite strap |
| **H05** | Total module thickness | < 16 mm (Nicla + battery + Qi coil, closely stacked). The enclosure shell is integral to the IP67 seal — no additional protective layer is needed; the enclosure itself contains the device thickness. |
| **H06** | Total module weight (single arm) | Not critical; ≤ 40 g acceptable for prototype |
| **H07** | Onboard storage capacity | ≥ 10 runs per arm before sync required |
| **H08** | No ferromagnetic materials near the BMM150 magnetometer | BMM150 calibration must remain stable. The Qi charging coil and surface transducer may contain magnets — these must be shielded or placed > 10 mm from the BMM150 |
| **H09** | The device shall withstand accelerations from gate pole impacts without damage or sensor decalibration | **200 g** shock test (typical slalom pole strike 100-200g at the grip); verify functional post-impact |
| **H10** | The device shall be rechargeable via **Qi wireless charging** (no exposed contacts, fully sealed, compatible with IP67) | Place on Qi pad, verify charging LED (F41) lights. Charge from 0% to 100%, verify full charge within 3 hours |
| **H11** | ⚠️ **v2 ONLY** — The PCB shall include an unpopulated Impinj E310-based UHF RFID module footprint with SPI routing and ceramic antenna keepout. *v1: footprint only, no IC populated.* | Module footprint present; no IC soldered |
| **H12** | ⚠️ **v2 ONLY** — UHF RFID frontend vs. BMM150 magnetometer non-interference. *v1: RFID unpopulated — not applicable.* | BMM150 calibration with RFID active/inactive (v2 only) |
| **H13** | The custom PCB shall include an **unpopulated** Qorvo DW3000 UWB module footprint (5×5 mm QFN, IEEE 802.15.4z) with ceramic chip antenna keepout zone and SPI traces routed to the nRF52832. The DW3000 shall be connected to the shared SPI bus with a dedicated CSn line. VDD rail is routed but not loaded — power-gated via GPIO-controlled MOSFET for zero leakage when unpopulated. Antenna keepout zone shall be > 15 mm from BMM150 and > 10 mm from UHF RFID ceramic antenna. ⚠️ **UWB is NOT a v1 feature. No DW3000 IC is populated. No UWB firmware is written. No UWB testing is required. This is a board layout hedge ONLY — if UWB infrastructure ever materializes, the v2 PCB can populate the DW3000 without a complete board redesign.** | Visual PCB inspection: verify DW3000 QFN footprint, antenna keepout zone, SPI traces, and CSn pad present; verify unpopulated position does not affect board operation. BMM150 + RFID calibration: verify accuracy unchanged with DW3000 footprint unpopulated |

---

## REQ-IF — Interfaces

| ID | Requirement | Description |
|---|---|---|
| **I01** | BHI260AP ↔ nRF52832 | I²C host interface, FIFO watermark interrupt at 10-sample threshold |
| **I02** | BMP390 ↔ nRF52832 | I²C, 100 Hz pressure reads |
| **I03** | LDC1612 ↔ nRF52832 | I²C, continuous inductance monitoring. INTB pin → nRF52 GPIO for sleep wake (F13) |
| **I04** | SPI Flash ↔ nRF52832 | SPI, circular run storage with per-file CRC32 |
| **I05** | Device ↔ Phone | BLE 5.0, LE 2M PHY preferred, custom GATT + Nordic DFU service (run list, file transfer, device config R/W, OTA firmware). Bonded pairing with LE Secure Connections |
| **I06** | Left arm ↔ Right arm | **No active radio link.** Each arm arms independently on its own LDC1612 sensor. A passive copper/iron foil disc is embedded in each strap. When the athlete brings forearms together at the start gate, each LDC1612 detects the approaching foil disc on the **opposite** arm's strap via cross-arm inductive proximity. No cross-arm BLE, no scan windows, no radio latency. "Cross-arm" refers to the physical proximity between forearms, not an inter-device link |
| **I07** | Phone ↔ Cloud | HTTPS REST API, endpoint URL retrieved from hardcoded bootstrap address |
| **I08** | Beeper ↔ nRF52832 | GPIO PWM → surface transducer bonded to inner enclosure wall (IP67, no sound port) |
| **I09** | Qi Receiver ↔ Battery Charger | Qi coil → rectifier → 5V → Nicla BQ25100 charger. Coil placed opposite side of PCB from BMM150 |
| **I10** | ⚠️ **v2 ONLY** — UHF RFID Reader ↔ nRF52832 | SPI, reader IC controlled by nRF52832. *v1: footprint only, no reader populated.* |
| **I11** | DW3000 UWB ↔ nRF52832 | ⚠️ **FOOTPRINT ONLY — NOT POPULATED, NOT TESTED, NO FIRMWARE.** SPI (shared bus with Flash + RFID), dedicated CSn line. Power-gated (GPIO-controlled MOSFET on VDD_UWB rail, default OFF). Antenna: ceramic chip antenna footprint, tuned for UWB channel 5 (6.5 GHz) or channel 9 (8 GHz). Reserved purely to avoid PCB redesign if UWB is adopted in v2+ |

### Cross-Arm Arming: Why BLE Is Not Needed

The arming mechanism is purely inductive, based on cross-arm proximity — no mechanical button, no moving parts:

1. Athlete brings **forearms together** at the start gate → the passive copper/iron foil disc embedded in the **left strap** approaches the **right arm's** LDC1612 coil
2. Right LDC1612 detects the approaching foil disc → continuous 1000 ms proximity → **right arm arms** (F03)
3. Simultaneously, the passive copper/iron foil disc embedded in the **right strap** approaches the **left arm's** LDC1612 coil
4. Left LDC1612 detects the approaching foil disc → continuous 1000 ms proximity → **left arm arms**

Each arm arms independently on its own sensor. The "cross-arm handshake" (I06) is the mutual inductive detection: each LDC1612 sees the other arm's passive copper/iron foil disc approaching. It is not a BLE advertisement. No radio latency, no scan windows, no real-time BLE handshake.

**What if one arm fails to arm?** The athlete separates and brings forearms together again. Since each arm operates independently, a failed left-arm detection doesn't affect the right arm's ability to arm.

**Safety net:** If the left arm arms and the right arm doesn't (or vice versa), the phone accepts single-arm runs (R08). Gate detection from one arm is still functional — just no cross-correlation alignment.

---

## REQ-ROB — Robustness & Edge Cases

| ID | Requirement | Description |
|---|---|---|
| **R01** | False arm reject | Inductive trigger must require 1000 ms continuous hold — momentary contact (< 500 ms) must not arm |
| **R02** | Aborted start timeout | If barometric descent does not follow within 30 s of arming, return to IDLE |
| **R03** | Mid-run inductive trigger ignored | Once LOGGING, LDC1612 input is masked until run ends |
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
REQ-HW  (H01–H13)   ◄─────────────── ENVIRONMENTAL TESTS (cold chamber, shock)
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

---

*Next: system_design — state machine, BLE GATT service definition, module decomposition.*
