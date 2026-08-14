# ANNA-B412 Module Migration — Change Note (2026-08-14)

## Decision (locked by JP)

Production MD1 BLE module changes from **ANNA-B112** (nRF52832) to **ANNA-B412**
(u-blox, nRF52833, integrated antenna).

| Property | ANNA-B112 (old) | ANNA-B412 (new) |
|----------|-----------------|-----------------|
| SoC | nRF52832 | nRF52833 |
| RAM | 64 KB | 128 KB |
| Flash | 512 KB | 512 KB |
| Bluetooth | 5.0 | 5.1 |
| Antenna | integrated | integrated (same class as Nicla Sense ME) |
| Body | 6.5 × 6.5 × 1.2 mm | 6.5 × 6.5 × 1.2 mm (same class) |
| Pinout | 52-pin LGA | may differ — design carrier to B412 datasheet |

## Rationale

- **128 KB RAM** (2× nRF52832) relaxes the Cordio BLE heap constraint that limited the nRF52832.
- **Peripheral superset** (nRF52833) over nRF52832.
- **Same 6.5 × 6.5 × 1.2 mm body class** — the carrier is being designed, so a pinout change is acceptable.
- **ANNA-B402 rejected** — antenna-pin / external-antenna path. B412 keeps the integrated antenna (same class as Nicla Sense ME).

## Design intent

- Keep the **Nicla-replica P0.xx GPIO map** for SGC peripherals unchanged (nRF52833 uses the same P0.xx numbering).
- Module **LGA pad numbers (1–52)** remain B112-derived placeholders until the u-blox ANNA-B412 datasheet pin table (Table 7 equivalent) is pasted in and verified.

## Non-goals (this task)

- ❌ **Firmware / PlatformIO:** env stays `nicla` (nRF52832) until a separate BSP-port task.
- ❌ **Arduino_BHY2 smoke-test on nRF52833:** still required later.
- ❌ **Requirements.md bulk nRF52832 wording:** may lag (optional follow-up).
- ❌ **Pin-identical claim:** no claim of pin-identical to B112 — design to the B412 datasheet.

## Open items for the FW port (separate task)

1. Add a PlatformIO env for ANNA-B412 / nRF52833 (Nordic SDK / ArduinoCore-mbed BSP).
2. Verify Arduino_BHY2 + Cordio BLE on nRF52833.
3. Re-map module LGA pad numbers against the u-blox ANNA-B412 datasheet pin table.
4. Re-run S03 / S04 / S05 / S06 device suite on B412 hardware.

## RTC DNP intent (2026-08-14)

An optional **DNP external RTC** footprint is reserved on the carrier PCB for a future
backup time source (primary time stays phone `ABC0` BLE sync).

- **Part:** Micro Crystal **RV-3028-C7** — I²C, integrated crystal, address **0x52**,
  DFN-8 (3.2 × 1.5 × 0.8 mm), 45 nA timekeeping. Chosen for small size + lowest Iq
  (vs PCF8523 needing an external XTAL, or larger DS3231M).
- **Bus:** `Wire` (I2C0) **P0.22/P0.23**, alongside the LDC1612 (0x2A) — no address
  clash (IS31FL3194 0x53 is on `Wire1` P0.15/P0.16, a different bus).
- **Power:** VDD → 3.3 V rail; VBACKUP → coin cell holder BT1 (DNP) **or** 0 Ω tie to
  VDD (R_RTC0, DNP — "no backup until populated").
- **/INT (`RTC_INT`):** left as an unconnected test point — a spare B412 GPIO (more
  available vs B112) can be routed later after the datasheet pin table is verified.
- **Not populated in v1** — DNP lines only. See `sgc_architecture_hardware.md` §12 and
  `sgc_bom.md` "Optional DNP — External RTC".
