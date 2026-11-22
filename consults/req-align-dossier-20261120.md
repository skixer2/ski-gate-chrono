# Dossier — Requirements ↔ Code ↔ Schematic Alignment Audit (2026-11-20)

## Your job
Two independent alignment audits, then a report. Be adversarial and evidence-based:
cite file:line for every claim. Do NOT trust the documents' own status claims — verify
against source. Report only what you can prove; mark anything unverifiable as UNVERIFIED.

## System context
SGC = Ski Gate Chrono. A device (Arduino Nicla Sense ME, nRF52832, running Zephyr/NCS
v3.4.0 firmware with MCUboot + MCUmgr OTA) detects a ski-race start-gate crossing via a
2.00 m barometric pressure drop, records 100 Hz IMU + baro data to flash, and a Flutter
phone app downloads runs over BLE and syncs to a cloud API (PostgreSQL).

Repo root: /home/node/.openclaw/workspace/ski_gate_chrono
You have READ access to everything under that path. Read freely.

## Why this audit exists
Two regressions shipped in two days, both invisible to the current test process:
 1. ARM window silently cut 120 s -> 20 s in commit ddd8ef3 (v0.16.0) while porting a
    ring buffer. Users armed, skied to the gate, and the unit had already given up.
 2. Baro raw->Pa used *39/100 instead of the datasheet-exact 1/128 hPa, a 1.41 hPa bias.
 3. Charger registers (ICHG 110 mA, VBATREG 4.35 V) were written once at boot with a
    read-guard, and were silently left at factory defaults after any power loss.
Root cause class: code drifted from documented intent and NOTHING checked the drift.
JP wants requirement-traceable daily tests (smoke / core / full tiers) so drift fails CI.

## Sources of truth (read all of these)
Requirements:
 - High_Level_Requirements/sgc_requirements.md            (352 lines; REQ-FUNC F01-F70,
   REQ-PHONE, REQ-PERF P01-P09, REQ-HW H01-H14, REQ-IF, REQ-ROB, REQ-DB, REQ-OTA,
   plus a "V-Model Traceability" section near line 290)

Firmware code (the real implementation):
 - .../module_design/implementation/Firmware_Zephyr/apps/03_pull_proto/src/*.c  (14 files)
   Key files: recorder.c (state machine + drop detector), sensor_prod.c (BHI260AP decode),
   bitpacker.c, ring.c, store_flash.c, ble_config.c (GATT + BQ25120A charger),
   pull_proto.c (serial commands), power_state.c, led_state.c, button.c, main.c
 - VERSION + src/app_version.h  (both must agree)

Phone code:
 - .../implementation/Phone_app_prototype/lib/**/*.dart  (43 files)

Hardware:
 - .../architecture_modules/sgc_schematic_spec.md         (368 lines: the HW specification)
 - .../architecture_modules/pcb/sgc_p1/*.kicad_sch        (7 sheets: root, mcu, power,
   sensors, storage, connectors, boost_led)  -- real KiCad 8 schematic netlists
 - .../architecture_modules/sgc_architecture_hardware.md  (pin map, power tree)
 - reference/ABX00050-schematics.pdf, InputInformation/Nicla_sense_schematic.pdf
   (vendor reference schematics for the Arduino Nicla Sense ME carrier)

Existing test documentation (verify whether it is CURRENT or stale):
 - .../implementation/MASTER_TEST_PLAN.md                 (280 lines, v1.2)
 - .../implementation/TEST_COVERAGE_MATRIX.md             (264 lines; claims 86 v1 reqs,
   24 done / 27 planned / 26 manual / 6 not implemented)
 - .../implementation/unit_tests/device/requirements_traceability.md
 - .../implementation/unit_tests/phone/TEST_SPEC.md
 - .../system_tests/device.md, system_tests/phone.md
 - .../architecture_modules/integration_tests/{device,phone,hardware}.md
 - High_Level_Requirements/acceptance_tests/{device,phone,on_snow_field_test_checklist}.md

## AUDIT 1 — Requirements ↔ Code
Goal: for EVERY requirement ID in sgc_requirements.md, determine whether the code
implements it, and whether the implementation matches what the requirement SAYS.
Deliver a table: requirement ID | requirement summary (<=15 words) | implementing
file:line | verdict (MATCH / CONTRADICTS / MISSING / PARTIAL / UNVERIFIABLE) | evidence.

Prioritise finding CONTRADICTIONS and MISSING items. Known suspects to confirm or refute
with evidence (do not just repeat these — find more):
 - V-Model diagram in sgc_requirements.md says device language is "C++ / Arduino + mbedOS"
   but the implementation is Zephyr/NCS in C. Docs vs reality.
 - F61/F62/F63/F64 and F57.1-F57.4 are referenced in the traceability diagram; confirm
   they actually exist as defined requirements and are implemented.
 - Drop detector: does the code's 2.00 m threshold, P0 window, and sea-level/altitude
   compensation match REQ-FUNC wording? (recorder.c ~lines 116-210)
 - ARM window: requirement vs the constant now restored to 100*120 in recorder.c.
 - Charger: does any requirement state the charge current or CV? If a requirement
   specifies the cell (500 mAh Renata ICP632136HPST), does code agree (0x98 = 100 mA =
   0.2C; 0x96 = 4.35 V)?
 - Battery percentage encoding, the "<60%" floor semantics, and BLE characteristics
   (ABC0-ABCB, ABD0) vs REQ-IF.
 - Data retention / ring buffer depth vs spec.
 - Any requirement mentioning timing/jitter (REQ-PERF) vs actual tick handling.

## AUDIT 2 — Requirements ↔ Schematics
Goal: every hardware requirement (REQ-HW H01-H14) and every hardware claim in
sgc_schematic_spec.md must be checked against BOTH the KiCad netlists and, where the
custom PCB differs from the Nicla carrier, against the vendor schematic.
Deliver a table: requirement/pin/interface | spec claim | schematic evidence (sheet +
net/ref-des) | verdict | note.

Specifically verify (prove with netlist evidence, not assumption):
 - BQ25120A connections: I2C bus instance/address, CD pin (P0.25) and its polarity
   consequence, VBAT/VIN, NTC, and the VIN-sense path used for the "on USB" flag.
 - I2C bus split: BQ25120A + LED on one bus, BHI260AP/flash on another. Which pins.
 - BHI260AP SPI: CS handling (there was a fix moving CS under SPI-driver control because
   a manual CS + preemption silently corrupted flash writes), SPI2 sharing with flash.
 - Console UART: TX/RX pin numbers (spec says TX P0.20 / RX P0.09 — verify), and the
   DAPLink/level-shifter path.
 - External flash part and its CS/size vs store_flash.c expectations.
 - Piezo/arming button pin and any LDC1612 remnants (an abandoned cross-arm approach).
 - LED driver part and bus.
 - Battery connector vs the fitted cell.

## Deliverable format
Write BOTH tables plus a short synthesis, to:
  /home/node/.openclaw/workspace/ski_gate_chrono/consults/req-align-verdict-20261120.md
AND return the full text of the report in your final message.

End with:
 (a) Top 10 highest-risk misalignments, ranked, each with the file:line evidence.
 (b) Which of the 86 claimed test-matrix rows are actually stale/unsupported.
 (c) A concrete proposal for three daily test tiers (smoke < 2 min, core < 15 min,
     full nightly) mapped to requirement IDs — prioritising the exact regression classes
     that bit us (a constant drifting from its documented value; a state-machine timeout;
     a register left at default). Tests must be runnable without hardware where
     possible (host-compiled unit tests over the pure logic: bitpacker, ring, drop
     detector, store layout) and clearly marked where hardware is mandatory.

## Rules
 - Evidence over assertion. Every verdict needs file:line or sheet+net.
 - If a doc claims something and you cannot verify it, say UNVERIFIED — never guess.
 - Do not modify any file except your report under consults/.
