# Ski Gate Chrono

BLE-connected sports-timing wearable for alpine ski racing — designed and built end to end as an independent R&D project: system architecture, embedded firmware, hardware co-design, and companion app.

## What it does

A device worn by the racer detects gate passes and run events in real time (IMU + barometric sensing), logs the full session at high rate to flash, and syncs runs to an Android app over BLE — including resumable transfers that survive connection drops mid-run.

## Firmware (C/C++, Nordic nRF52)

- Targets Nordic nRF52 series (nRF52832 prototypes, nRF52833 production module u-blox ANNA-B402), ARM Cortex-M4F
- Custom BLE GATT profile with CRC-verified, resumable download protocol (survives wedged links and reboots; auto-resume verified end to end)
- High-rate logging: 90+ frames/s sustained to an 8 MB SPI NOR flash via a custom multi-slot raw storage layer with linear pre-roll buffering
- Sensor pipeline: Bosch BHI260AP IMU @ 100 Hz, BMP390 barometric pressure; start-gate detection and timing logic
- Cooperative scheduler, watchdog supervision with per-subsystem checkpoints, JSON-lines telemetry (serial + BLE)

## Hardware

- Custom 4-layer PCB (22 × 55 mm): u-blox ANNA-B402 BLE module, USB-C (GCT USB4085), BQ25120 Li-ion charging, IS31FL3194 LED driver, MX25R6435F 8 MB flash
- Prototypes on Arduino Nicla Sense ME

## Companion app (Android)

- Flutter UI + Kotlin BLE layer (Nordic BLE stack)
- Auto-connect and background sync, one-run-per-connection batch downloads, connection-priority management, foreground service
- Local run storage and run visualization

## Tooling & quality

- PlatformIO build; versioned firmware releases (every code change bumps FW_VERSION)
- Automated Python serial test harness with tiered catalog (smoke / core / full) and a test ledger; release gates include stream-integrity, storage-rate, and ring-buffer soak tests
- Git history: 500+ commits of incremental, benchmark-driven development

## Repository layout

- `High_Level_Requirements/system_design/` — architecture and module design documentation
- `.../implementation/Firmware_implementation/` — device firmware (PlatformIO)
- `.../unit_tests/` — Python test harness, test catalog, and ledgers
- `.../Phone_app_prototype/` — companion Android app (Flutter/Kotlin)

## Status

Active development. Hardware v4.2 · firmware v5.7x · app v1.4x.

---

Contact: Jean Paul Voyat — jp.voyat@vytsolutions.com · [LinkedIn](https://www.linkedin.com/in/jean-paul-voyat-792704b/)
