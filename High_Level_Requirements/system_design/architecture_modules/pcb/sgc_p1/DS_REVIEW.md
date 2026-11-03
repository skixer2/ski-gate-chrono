# DS Review - SGC P1 (netlist hier9, 2026-11-03)

Reviewer: hardware design review of netlist `review/hier9.net` (KiCad 10.0.7 export, 2-sheet hierarchy) vs `erc_d2b.rpt`, `sgc_architecture_hardware.md` v2.5, `ERC_TRIAGE.md`. Netlist is ground truth. DNP-absent v1 operation is the acceptance criterion.

## Verdict

Power architecture and buses are fundamentally sound (single 1.8 V digital rail from BQ25120A SYS default, correct USB-C sink, correct SPI/I2C/CS map, ERC residue all triaged). **As drawn, revA would not fully work**: one GPIO is driven above absolute maximum via the /CD pull-up, the status RGB LED has no current path (common pin floating), the BHI260AP core-regulator capacitor mandated by Bosch's BOM is missing, and the v1 arming piezo button has no electrical connection point. Two majors (BHI boot strap strategy, I2C0 pull-up values) follow.

## BLOCKERS (would prevent intended function)

### B1. [BLOCKER] BQ_CD pull-up ties nRF GPIO_50 to the PMID rail — over absolute max
- **Evidence (netlist):** `BQ_CD` ∋ {R4.2, U1.50 (GPIO_50), U3.E2 (/CD)}; `V_PMID` ∋ {R4.1, U3.A3 (PMID, power_out), U3.B3, C2.1, C15.1, U5.A1 SDB, U5.A2 VCC}. R4 = 100 k.
- **Why:** PMID ≈ VBUS (~5 V) whenever USB is plugged, ≈ VBAT (3.0–4.2 V) on battery. The net idles at that potential and touches a 1.8 V-domain nRF52833 GPIO (abs max VDD+0.3 V = 2.1 V). Persistent over-voltage on GPIO_50 → device damage/latch-up risk. This holds regardless of whether /CD is an input or an open-drain output (see V1).
- **Fix (schematic):** re-top R4 from V_PMID to `+1V8` (rail exists, already hosts R5/R6/R9/R10 tops), or delete the /CD-to-MCU connection entirely (charge status is available via I2C + INT). No other change needed.

### B2. [BLOCKER] Status RGB LED D1 has no current path — common terminal unconnected
- **Evidence (netlist):** `unconnected-(D1-PadCOM)` contains only D1.COM; `LED_R/G/B` ∋ {D1.R/G/B, U5.B2/C2/D2 (OUT1–OUT3)}. No net connects D1.COM to V_PMID, GND, or anything.
- **Why:** IS31FL3194 OUT1–3 are current-sinking outputs (datasheet param "VHR — current **sink** headroom, 150 mV @ 20 mA"; Lumissil eval board wires LED anodes to the supply rail). The LED anode(s) must reach the driver's supply rail (`V_PMID` = U5 VCC). With COM floating there is no loop → LED never lights. The LED is the primary v1 status/UX channel (state, charging, factory-reset feedback).
- **Fix (schematic):** tie D1.COM to `V_PMID` (the U5 VCC rail). Also resolve polarity explicitly (see V5) — if SMLP34RGB2W3 is common-cathode the driver topology itself must be re-checked. No series resistors needed (current set by IS31 register) — absence of resistors is correct.

### B3. [BLOCKER] BHI260AP VREG (pin 6) has no capacitor — Bosch-mandated 220 nF missing
- **Evidence (netlist):** `unconnected-(U8-VREG-Pad6)` — pin 6 fully unconnected, no cap anywhere on it.
- **Why:** BST-BHI260AP-DS000 external-component BOM lists **"C4 220 nF — Regulator capacitor VREG"**. VREG is the output of the internal core (Fuser2) regulator; running it undamped risks core-supply instability → unreliable BHI boot/fusion = broken sensor chain.
- **Fix (schematic):** add 220 nF from U8.6 to GND (new cap symbol; keep the pin off all other nets).

### B4. [BLOCKER] v1 arming piezo button has no electrical connection point
- **Evidence (netlist):** `BUTTON` ∋ {U1.20 (GPIO_20), U6.5 (LDC INTB, DNP)} only. No connector on the board (J1 USB-C, J2 battery, J3 SWD, J4 LED-strip DNP) carries it; no switch symbol exists in the BOM.
- **Why:** Architecture doc (H14/F03/§3): v1 arming = external Langir 16 mm piezo pushbutton on a GPIO, edge interrupt, single press to arm, 5 presses in 3 s = factory reset; LDC1612 proximity is explicitly v2-only. As drawn the v1 device cannot be armed by its specified control.
- **Fix (schematic):** add a 2-pin connector (BUTTON + GND) or solder pad pair on the `BUTTON` net; U1.20 internal pull-up handles the idle state. If v1 arming is meant to be BLE-app-only, this downgrades to minor — but that contradicts H14/F03 and should be an explicit JP decision.

## MAJOR (functions but with real risk)

### M1. [MAJOR] BHI260AP HOSTBOOT (U8.3) unconnected → forced host-boot mode
- **Evidence (netlist):** `unconnected-(U8-HOSTBOOT-Pad3)`. Datasheet BST-BHI260AP-DS000 pin table: "3 HOSTBOOT | System Control | **Input, Pull-Up** | Boot mode select (host / standalone)"; §5.2: standalone (flash) boot requires HOSTBOOT **low** at start-up.
- **Why:** Floating = internally pulled high = host boot at every POR. The QSPI flash U2 is wired correctly but can never serve standalone boot as drawn. If the intended model is Nicla-style standalone boot (BHI runs fusion from flash, host boots fast / sleeps), it is broken; if host-boot-per-POR is intended (host uploads FW each time over SPI), it works but is slower and undocumented.
- **Fix (schematic):** 10 kΩ pull-down on U8.3 to GND if flash standalone boot is the intent; otherwise record the host-boot decision. Either way, add an explicit strap so boot source is deterministic.

### M2. [MAJOR] I2C0 pull-ups 100 kΩ — too weak for the mandated 400 kHz bus
- **Evidence (netlist):** `I2C0_SDA` ∋ {R9.2 (R9=100k, R9.1 on +1V8), U1.40, U3.E4 SDA, U4.8 SDA, U5.B1 SDA}; `I2C0_SCL` ∋ {R10.2, U1.36, U3.E5 SCL, U4.7 SCL, U5.C1 SCL}. Architecture doc §10 mandates Fast-mode 400 kHz on this bus.
- **Why:** 100 kΩ × ~30 pF ≈ 3 µs rise ≫ 300 ns Fast-mode spec; even at 100 kHz it is marginal (1 µs limit). u-blox ANNA-B402 datasheet Table 15 suggests kΩ-class pull-ups (≈5 kΩ @ 50 pF fast mode). Bus members BQ25120A + MAX17048 + IS31FL3194 all support 400 kHz; MAX17048 VIH = 1.4 V fixed and IS31 VIH = 1.4 V fixed → 1.8 V logic levels are fine (verified), the resistor value is the problem.
- **Fix (schematic):** R9/R10 → 2.2 k–4.7 kΩ to +1V8. (I2C1 has no pull-ups at all, but both its slaves are DNP v2 — see m12.)

## MINOR / nits

1. **m1.** `BQ_INT` (U3.D2) pulled up via R5 (100k→+1V8) but not routed to any U1 pin — charger interrupts/faults unmonitored; I2C status polling must cover it.
2. **m2.** GAUGE_ALRT pull-up R6 = 100 k vs doc's stated 10 k — electrically OK for a slow open-drain alert; align doc or BOM.
3. **m3.** Net naming inverted vs doc/Zephyr convention: power devices (MAX17048 0x36, IS31 0x53, BQ25120 0x6A) sit on nets named `I2C0_*` (U1.36/U1.40), sensors on `I2C1_*` (U1.37/U1.39); the doc calls the power bus "I2C1/Wire1" and the sensor bus "I2C0/Wire". Netlist membership is correct — rename nets or update doc to prevent firmware mapping errors.
4. **m4.** VINLS (U3.B4/C4) tied to +1V8/SYS instead of the conventional PMID. Harmless (LS/LDO is disabled via LSCTRL=E3→GND; `LSLDO_C` carries only C6), but datasheet-typical connection is PMID — normalize for reviewability.
5. **m5.** U1.18 (XL2) tied to GND, XL1 (U1.17) a lone test point: crystal-less LFRC operation is sanctioned (USB-ANNA-B402 guide ships without LFXO), but firmware must configure LFRC per u-blox app note and must never enable LFXO.
6. **m6.** v2 drive conflict seeded: `BUTTON` shares U1.20 with U6.5 (LDC1612 INTB). If INTB is push-pull (see V6) and a v2 button lands on the same net, they fight. Split nets or verify before v2 stuffing.
7. **m7.** MT3608 EN (U10.4) has no pull-down on `BOOST_EN`; during nRF boot/high-Z the DNP-populated v2 boost could free-run at power-on. Add 100 k EN→GND for the v2 stuffing option. (v1: all-DNP, no effect.)
8. **m8.** Doc §7 power tree is stale vs netlist (doc: Qi/IP6833 → 3.3 V LDO for nRF + separate 1.8 V sensor LDO + populated MT3608; netlist: USB-C → BQ25120A SYS = single 1.8 V digital rail, boost all-DNP, no RFID/UWB power switches). Netlist wins — sync the doc.
9. **m9.** Whole system runs at VDD=1.8 V: valid for nRF52833 (1.7–3.6 V) and BQ25120A SYS defaults to 1.8 V at power-up (verified, SLUSD08A), but radio TX headroom (+8 dBm class) and noise margins are reduced vs 3 V designs; validate BLE range in field trials.
10. **m10.** Charge-only USB-C is consistent throughout: J1 D+/D−/SBU NC, U1.34/35 (USBDP/USBDM) NC, U1.21 (VBUS) NC. No VBUS↔SYS/+5V back-feed path exists (`VBUS_USB` feeds only U3.A2 IN; reverse blocking internal) — battery-only operation safe. Confirm the required state of module pin 21 (V10).
11. **m11.** UART_TX/RX exist only as test-point labels (U1.10, U1.22); field debug is SWD-only. Fine if intended.

## VERIFICATION-NEEDED (datasheet questions, exact and answerable)

- **V1.** SLUSD08A §7: is BQ25120A /CD (ball E2) an input or an open-drain output, and what is its absolute-max rating vs PMID? (B1's fix must respect the pin's true role; the over-voltage finding stands either way.)
- **V2.** SLUSD08A: BQ25120A SYS buck maximum output current vs worst-case +1V8 load (BLE TX + U7 program/erase ~20–25 mA peaks + BHI + BMP + pulls ≈ 60–100 mA), and L1 (0402, 2.2 µH) saturation current ≥ buck peak current.
- **V3.** SLUSD08A: does /MR (ball E1, unconnected here) have an internal pull-up making NC safe? (Nicla-era design drove /MR from the module reset; that link is gone in P1.)
- **V4.** ANNA-B402 datasheet/SIM: required connection of module pin 21 (VBUS) and pins 34/35 when USB device mode is unused — NC as drawn, or tie to GND?
- **V5.** ROHM SMLP34RGB2W3: common-anode or common-cathode, and its pin/die map vs the symbol (symbol description itself says "common polarity verify at capture") — needed to land B2's fix correctly.
- **V6.** TI LDC1612 datasheet: INTB output type (push-pull or open-drain) — decides m6 for v2.
- **V7.** SLUSD08A electrical table: BQ25120A SCL/SDA VIH/VIL at 1.8 V bus levels (expected compatible — part targets 1.8 V hosts — confirm for the record).

## Verified OK (datasheet-backed, no action)

- **MAX17048 CELL→GND, VDD=VBAT**: correct per ADI MAX17048/49 datasheet — CELL is "not internally connected" on the MAX17048 (it is the MAX17049 sense); "MAX17048 measures VCELL between the VDD and GND pins"; VIH = 1.4 V fixed → 1.8 V bus fine. AD-029 stands.
- **ANNA-B402 RESET_N without external pull-up**: datasheet Table 13 documents internal 13 kΩ pull-up — safe as drawn (J3.10 + U1.12 only).
- **U7 MX25R6435F WP#/HOLD# (pins 3/7) unconnected**: Macronix v1.6 Note 1 — internal pull-ups remain active when the pins have no physical connection. Safe in single-SPI use. (Corollary: never wire these pins without external pull-ups later — connecting disables the internal pull.)
- **U2 QSPI hook-up**: all 6 lines U8↔U2 incl. WP#/SIO2→QSPI_D2, HOLD#/SIO3→QSPI_D3 — Bosch-prescribed topology.
- **BHI RESETN**: driven by U1.52 (`BHI_RESET`) + internal 71–121 kΩ pull-up → deterministic.
- **BQ25120A straps**: R1 1.82 k → ~110 mA fast charge (KISET=200 A·Ω); R2 1 k → ~200 mA input limit (KILIM=200 A·Ω); R3 4.99 k → 10 % termination (Table 3 verbatim); SYS default = 1.8 V at power-up → +1V8 rail valid before any I2C config; LSCTRL=GND correctly disables the unused LS/LDO; TS fed by the 3-wire pack NTC.
- **USB-C sink**: CC1/CC2 5.1 k Rd (R7/R8→GND); all four VBUS pins (A4/A9/B4/B9) on `VBUS_USB`; VBUS decoupling 1 µ + 10 µ.
- **J3 Cortex Debug mapping**: 1 VTref(+1V8) · 2 SWDIO · 3/5/9 GND · 4 SWDCLK · 6 SWO(U1.16) · 7 KEY NC · 8 TDI NC · 10 nRESET — correct.
- **SPI host bus**: shared MISO/MOSI/SCK (U1.24/25/19 ↔ U7.2/5/6 + U8.32/11/33) with dedicated CS per slave (CS_FLASH U1.33→U7.1, CS_BHI U1.27→U8.34); BHI HIRQ→U1.11; no address/CS contention.
- **I2C addresses**: 0x36 / 0x53 / 0x6A on the power bus, 0x2A / 0x52 (both DNP) on the sensor bus — no collisions.
- **MT3608 reserve (all-DNP)**: boost topology now complete — VBAT→L2→SW (`BOOST_SW` ∋ L2.1, D2.A, U10.1), D2 SS34 cathode→+5V, FB divider 75 k/10 k → 5.1 V, C19 22 pF feedforward, C18 22 µ output, BOOST_EN from U1.49.
- **ANT**: U1.5↔U1.6 bridge per module convention; U1.1 GND pattern pin tied to GND.
- **Decoupling per rail**: VBAT 1 µ+0.1 µ (+10 µ DNP), PMID 4.7 µ+0.1 µ, +1V8 bulk 10 µ + 8 distributed caps, LS/LDO out 1 µ, IS31 SDB tied to its own VCC.
- **ERC residue**: the 4 remaining errors (SWDCLK/NRESET debugger-driven, MCSB2 reserved, GND convention) match the accepted triage list; no new ERC errors introduced by the D2/L2 fix (`+5V` now driven by D2.K power_out).
