---
type: architecture-decision
project: ski_gate_chrono
tags: [ski_gate_chrono, firmware, zephyr, binding-decision]
created: 2026-09-15
status: BINDING (JP, 2026-09-15 11:58 UTC)
---

# ZEPHYR PORT PLAN — firmware platform switch to nRF Connect SDK

## 1. Context and motivation

Three weeks of BLE transfer debugging (TC-2026-08-26-001, opened 2026-08-26; push-FT
saga earlier) culminating in the 2026-09-14/15 bench marathons (ArduinoFW 5.77→5.89,
App 1.44→1.46). Findings:

- The radio link is capable: 39 KB in 3-5 s proven repeatedly (40-60 fps at CI 15 ms,
  MTU 247, 2M PHY).
- Every remaining failure class traces to the mbed-Cordio/ArduinoBLE stack API surface:
  `BLE.begin()` cannot re-init after `end()` in-process (5.88/5.89 evidence); `writeValue()`
  blocks indefinitely with no backpressure or queue visibility; link recovery requires
  device reset; supervision/ghost slots unrecoverable in-process.
- The phone side adds its own failures (Android-generic 133, scan throttling, OEM stack
  churn) that a product must tolerate — impossible to tolerate gracefully when the device
  stack itself cannot react.

Product bar (JP): the athlete at -10°C must see run data within seconds of opening the
app. A transfer layer that needs minutes of retries is unusable. JP will not fund PCB
development until a passably-working prototype exists in hand.

## 2. Decision

Firmware platform: **Zephyr RTOS + nRF Connect SDK + Nordic SoftDevice controller.**

- Same silicon family in prototype and production: ANNA-B112 (nRF52833, Nicla Sense ME)
  now, ANNA-B402 (nRF52833) on the production PCB — the port lands on production
  hardware unchanged.
- Qualified controller; the ArduinoBLE failure classes do not exist on this stack.
- Carried over VERBATIM (validation loop unchanged):
  - BLE GATT service UUIDs and semantics
  - Pull protocol: `D` directory, `s <id> <off> <total>` stream, `[seq16][len8][<=MTU-7]`
    binary frames, `[FFFF][00]` end marker, CRC32 trailer verification, `r` ASCII debug path
  - Phone App 1.46 and the bench procedure

## 3. Requirements (ZPR)

- **ZPR-1 Board support.** Zephyr board definition for ANNA-B112 on the Nicla Sense ME
  carrier: pin map (documented in TOOLS.md + repo), native USB CDC (nRF52833 USB),
  SWD flashing via the existing on-board CMSIS-DAP probe. Flash/RAM partitions per
  NCS defaults + our external SPI flash (MX25R6435F, 8 MB).
- **ZPR-2 Service parity.** Identical GATT layout (service + state/battery/flash/run-info/
  console/cal characteristics, same UUIDs). App 1.46 connects with zero changes.
- **ZPR-3 Pull engine.** Non-blocking TX (`bt_gatt_notify` + conn callbacks); frame size
  = negotiated ATT MTU − 3 (target 244 B at MTU 247, upgradable per JP MTU note: request
  MTU 511 server-side, 508 B frames); pacing driven by TX buffer availability (stack
  backpressure), no fixed delays; **10 s job deadline** semantics preserved (abort +
  `bt_conn_disconnect` + re-advertise); supervision events drive disconnect state.
- **ZPR-4 Storage continuity.** RawRunStore slot layout preserved on the external SPI
  flash (pre-roll @ 0x0000, 8 × 244 KB slots @ 0x14000, config 0x1FC000, index 0x1FD000):
  runs written by ArduinoFW 5.8x remain readable/downloadable by the Zephyr FW.
- **ZPR-5 Sensors.** BHI260AP via Bosch bhy2 C API (port from Arduino_BHY2 internals);
  ODRs per existing config (rotation/lin_acc/pressure @ 100 Hz, temp @ 1 Hz).
- **ZPR-6 Logging.** JSON-lines over USB CDC, same event vocabulary (`boot`, `ble_conn`,
  `ble_disc`, `pull_prog`, `pull_deadline`, …) so existing bench tooling works.
- **ZPR-7 Validation gate (M1).** Existing App 1.46 downloads all 5 runs from a Zephyr
  image with byte-identical CRC results. This gate retires the platform risk before any
  further porting.
- **ZPR-8 Watchdog policy.** Hardware WDT remains; job deadline per ZPR-3. Stack-level
  link recovery replaces the radio-restart/reboot machinery (Zephyr supports
  enable/disable and supervised disconnects; no NVIC resets in normal operation).
- **ZPR-9 (later) OTA.** SMP/MCUmgr image management evaluated at M4 — not in scope
  for M0-M1.

## 4. Milestones

- **M0 (today): toolchain + board bring-up.** nRF Connect SDK (west) on JP-PC; board
  def for B112/Nicla; blink + USB CDC console + BLE beacon. Acceptance: `west flash`
  via existing DAP probe, console JSON `boot` line, device visible in nRF Connect scan.
- **M1 (next days): parity gate.** GATT service + pull engine + CRC serving the existing
  5 runs (test image, storage stub acceptable). Acceptance: App 1.46 downloads all 5,
  CRC-valid, ≥3 clean 5-run benches without device reset.
- **M2: storage.** RawRunStore on Zephyr flash API with layout continuity (ZPR-4).
- **M3: sensors.** BHI260 bring-up (long pole), state machine + detectors port.
- **M4: full system.** Chargers/LED/button/eslov-free operation, full bench suite,
  ArduinoFW retired to reference.

## 5. Risks

- BHI260 bring-up effort (Arduino_BHY2 hides sensor-hub internals) — mitigated: Bosch
  bhy2 API is C-portable; M1/M2 do not need sensors.
- Nicla board definition gaps (no official Zephyr board for Sense ME) — mitigated:
  nRF52833DK def + pin remap; USB CDC supported on nRF52833.
- Team unfamiliarity with Zephyr — mitigated: NCS samples cover GATT/flash/WDT paths;
  coordinator (ZioClaw) runs the port with JP as reviewer.

## 6. Non-goals

No phone app changes, no protocol changes, no hardware changes, no feature additions
during M0-M2.
