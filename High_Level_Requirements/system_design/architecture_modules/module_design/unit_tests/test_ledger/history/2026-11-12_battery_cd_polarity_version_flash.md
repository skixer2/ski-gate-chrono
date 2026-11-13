# TC-2026-11-12-001 — Battery-%-on-battery + version-source sync + 0.25.8 flash (COMPLETE)

| Field | Value |
|---|---|
| **Status** | PASS (all three) |
| **FW** | 0.25.5 → 0.25.6 → 0.25.7 → 0.25.8 (94b617d) |
| **Port / bench** | COM3, SWD via zflash, hexsafe archive |

## Arc
- Symptom: battery % unknown on battery (I2C dead to BQ25120A), OK on USB.
- Diagnosis ladder (ABCB diag char): 0.25.5 CD re-assert + retry (rc -4 persisted) → 0.25.6 raw errno + staged recover (EIO=5 NACK vs ETIMEDOUT discriminator) → **0.25.7 root cause: CD polarity is VIN-dependent (BQ25120A Table 1)** — CD tracks bq_on_vin per poll; recovery drives HIGH.
- **PASS (hardware, JP 11-10):** app shows "Full (100%)" on battery.
- Version debt closed in 0.25.8: VERSION file (MCUboot-signed image version) + app banner synced — was 0.24.7 vs 0.25.7 drift (phone-OTA would report stale version).
- 0.25.8 flashed 11-12 from hexsafe copy: OpenOCD **Verified OK**; boot banner: MCUboot "Image version: v0.25.8" + app "v0.25.8-94b617d"; manifest version_MCUBOOT 0.25.8+0; device advertising "SGC JP", telemetry sane, battery 100% stable.

## Verdict / root cause
- CD polarity (VIN-dependent) — one bit, whole subsystem.
- Version drift class: two manual version sources; now value-synced (single semantic source still TODO if versions diverge again).
