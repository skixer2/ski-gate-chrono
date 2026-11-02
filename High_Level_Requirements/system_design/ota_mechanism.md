# SGC OTA Firmware Update Design (v2.1 — MCUboot + MCUmgr SMP; Stage 1 COMPLETE 2026-11-02)

*2026-10-29 — v2.0 (JP directive, doc-only): OTA architecture + path documented. Chosen path switched from the Arduino-era Nordic-DFU plan (§6, retained as historical reference) to the **Nordic-native MCUboot (dual-bank) + MCUmgr SMP over BLE** — the built-in Zephyr/NCS mechanism, consistent with the firmware's Zephyr port. Flash budget VERIFIED: dual-bank fits in the internal 512 KB without touching the external-NOR storage architecture. Staged rollout S1→S3 (= REQ-OTA in sgc_requirements.md v6.6). Activates ZEPHYR_PORT_PLAN.md ZPR-9 ("SMP/MCUmgr image management" — was deferred, now scheduled). No code changes.*
*2026-06 — v1.0: Nordic DFU via Nicla stock mbed bootloader (Arduino era). Superseded by the Zephyr port — see §6.*

---

## 1. Why OTA

Once a device is potted in waterproof glue, the USB port is permanently inaccessible. Field devices — ski-gate timers mounted on gates, units in coaches' pockets — **must update without USB/SWD**. SWD stays as **factory-recovery only** (§5).

**Trigger point:** before the fleet grows past arm's reach — the **2nd device (Nov 2026)** already makes cable-round updates annoying, and the production ramp (**P2/P3**, sgc_production_strategy.md) makes them impossible.

## 2. Flash Budget (VERIFIED — the enabling constraint)

| Region | Media | Content |
|---|---|---|
| Internal nRF52832 flash | **512 KB** | ONLY the app (~150 KB image) + now MCUboot + second slot |
| External NOR (MX25R6435F) | **8 MB** | Run storage + config (2 MB LOG region) — **untouched by OTA** |

~**360 KB internal free** ⇒ **dual-bank MCUboot fits WITHOUT touching the storage architecture.** Run data, config, and the RawRunStore layout are completely unaffected. This is the enabling constraint and it is unusually favorable: no storage migration, no data loss risk from the bootloader work.

Slot sizing: with 512 KB internal, image slots come out at ~**200–220 KB each** — generous headroom over the ~150 KB app, but image growth must be watched (§5).

## 3. Chosen Path — Nordic-Native

**MCUboot bootloader (dual-bank, swap, power-loss-safe, revert-on-confirm-fail) + MCUmgr SMP transport over BLE + signed images.**

| Element | Choice | Notes |
|---|---|---|
| Bootloader | **MCUboot** (NCS built-in: `CONFIG_BOOTLOADER_MCUBOOT`) | Dual-bank slots; swap-on-boot; power loss during swap is **SAFE by design** (that is the point of dual-bank); revert to the previous slot when a new image fails to confirm within its window |
| Transport | **MCUmgr SMP over BLE** (NCS 3.4 built-in: `mcumgr` BT) | Standard management protocol — image upload, state query, confirm/reset |
| Signing | **`imgtool`** | Dev key initially; **real production keys + secure storage at Stage 3** |
| Migration | One-time SWD flash of the **MCUboot+app combined hex** | The only cable update ever needed again |

Why not the alternatives: the v1.0 plan (Nordic DFU on the Arduino mbed bootloader, §6) died with the Arduino→Zephyr port — that bootloader no longer exists on the device. A custom updater was rejected: MCUboot/SMP is Nordic-native, NCS-integrated, battle-tested, and costs no protocol design.

## 4. Staging

### Stage 1 — Firmware (~1–2 bench days)
- MCUboot + SMP-over-BLE + signing pipeline (`imgtool`, dev key).
- Updates via Nordic's free **nRF Connect for Mobile** app (manual SMP upload).
- One-time SWD migration flash (MCUboot + app combined hex).
- **After this: no cable needed — including the 2nd Nicla.**
- *Acceptance (REQ-OTA S1):* OTA update 0.23.7 → test build via nRF Connect; device boots the new version; ABC6 reports it; bad image → rollback.

### Stage 1 — ✅ COMPLETE (2026-11-02, bench-proven end-to-end)

**Result:** 0.24.6 uploaded from phone → slot 1 → Test → MCUboot swap → confirmed permanent.
Boot proof: `magic=good, copy_done=0x1, image_ok=0x1`, RSA verified, banner v0.24.6; 35 runs
+ device config survived untouched (external flash). FW chain: 0.24.0 migration → 0.24.2
params → 0.24.4 netbuf-244 → 0.24.5 MTU-300 → **0.24.6 (the OTA payload, never wire-flashed)**.

**Transport design (measured, nRF52832 = 64 KB RAM part — arduino_nicla_sense_me/nrf52832):**

| Kconfig | Value | Why |
|---|---|---|
| `BT_L2CAP_TX_MTU` | **300** (was 247) | one SMP frame per BLE write; MTU−3 ≥ worst-case frame |
| `MCUMGR_TRANSPORT_NETBUF_SIZE` | **297** (= MTU−3) | image-list response (2 slots × 32 B hash) = 252–262 B; 297 leaves 35 B headroom |
| `MCUMGR_GRP_OS` + `…_MCUMGR_PARAMS` | y | the app queries mcumgr params at connect — without it the app computes MTU 0 and refuses to send |
| `MCUMGR_TRANSPORT_BT_REASSEMBLY` | **n** | Nordic's 2475×4 set needs ~7 KB of our 8 KB free RAM — rejected (RAM-margin doctrine) |

Coupling invariant (violating it = Corrupt/Too-large, proven empirically):
`NETBUF_SIZE ≤ MTU−3` when reassembly is off, and the advertised `buf_size` drives the app's frame size.
RAM: 57,484 / 64 KB used → **8,052 B free (12.3%)**. BT buffer COUNTS untouched (att_pool starvation
history — resize, never recount).

**Phone tool = "nRF Connect Device Manager"** (separate app; Advanced mode). NOT nRF Connect for Mobile
(no MCUmgr anymore) and NOT "nRF Device Firmware Update" (legacy Secure-DFU protocol).

**Operating procedure & gotchas (all learned on hardware):**
1. Upload `.bin` → Image 0 → mode **Test and Confirm** (or Confirm on the pending image for a permanent swap without trial).
2. **Test = trial boot**: one reboot runs the new image; without Confirm the NEXT reboot reverts. Extra resets during a Test = revert (observed).
3. Slot images must DIFFER (identical 0.24.5 in both slots → the app targets the active slot → "Testing active slot denied (33)").
4. MCUmgr reset has a 250 ms built-in delay (`GRP_OS_RESET_MS`) — the app may look unresponsive; retry once.
5. SWD stays factory-recovery; nothing on the external flash (runs/config @0x1FC000) is touched by any of this.

### Stage 2 — App (~1–2 days)
- In-app updater using the Nordic **`mcumgr` Android library** (Apache-2.0, `io.runtime.mcumgr`).
- UI: **Settings → Firmware update** — file picker, progress, verify. (Phone requirement F64.)
- ~150 KB image = **10–15 s** at current BLE throughput.
- *Acceptance (REQ-OTA S2):* full in-app update flow end-to-end; version verified post-reboot.

### Stage 3 — Production hardening
- Real signing keys + secure storage; dev key retired.
- MCUboot **revert/rollback** enforced: a new image must **confirm alive within its window** or the device reverts on next boot.
- **Downgrade protection:** the app checks the device's firmware version (ABC6 characteristic, present from FW 0.23.6) as the update precondition and blocks downgrades.
- **Update gating:** "device needs update" logic in the app.
- *Acceptance (REQ-OTA S3):* signed-only enforcement (unsigned/mis-signed rejected); downgrade blocked; revert fires on a non-confirming image.

## 5. Constraints & Risks

| Constraint | Handling |
|---|---|
| Image size growth | Must stay under the slot budget (~200–220 KB per slot at 512 KB internal) — watch per-release |
| SMP transfer vs. operations | **Forbidden while ARMED/LOGGING — SLEEP state only** (an update mid-run would kill acquisition and risk the store) |
| Power loss during swap | **SAFE by design** — dual-bank means the old slot is never destroyed until the new image confirms |
| BLE throughput | ~10–20 KB/s ⇒ ~15 s per ~150 KB transfer — negligible |

## 6. Historical — v1.0 Nordic DFU Plan (Arduino era, SUPERSEDED 2026-10-29)

*Retained for archaeology; the Zephyr port removed the mbed bootloader this relied on. The SWD-recovery guidance below (TC2050/pogo or sealed connector, don't touch SWDIO/SWCLK GPIO config) remains VALID for §1's "SWD = factory recovery" policy.*

<details>
<summary>v1.0 content (collapsed)</summary>

- Nicla stock bootloader Nordic DFU over BLE; `nordic_dfu` Flutter package; `DfuTarg` entry via GPREGRET magic (`0xB1`) + `NVIC_SystemReset`; `OTAControl` characteristic `5347ABCF` write `0x01`; `nrfutil pkg generate` with `--sd-req 0xB6` (S140 v6.1.1); MAC+1 offset handled by `forceScanningForNewAddressInDfu`.
- Open questions then: exact GPREGRET magic, SoftDevice req value, DFU MAC offset — all moot post-Zephyr.

**JTAG/SWD recovery (still valid):** Tag-Connect TC2050 pogo footprint (recommended, no soldered connector) or sealed IP67 micro connector (SWDIO, SWCLK, GND, VCC); recovery = probe + `pio run -t upload` (now: west flash); nRF52832 SWD pins shared with GPIO — never reconfigure them in the application.

</details>

---

*Traceability: F40 (device), F64 (phone), REQ-OTA (staged acceptance) — sgc_requirements.md v6.6. System context: sgc_system_design.md §10. Zephyr port anchor: ZEPHYR_PORT_PLAN.md ZPR-9.*
