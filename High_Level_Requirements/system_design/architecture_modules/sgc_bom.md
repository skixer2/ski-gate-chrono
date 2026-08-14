# SGC — Bill of Materials (v4.4 — ANNA-B412 module)

*2026-08-14 — v4.4: Optional **DNP external RTC** footprint added (RV-3028-C7 @ 0x52 on `Wire` P0.22/P0.23) + coin cell holder + 0 Ω tie + decoupling caps — all DNP. Primary time stays phone `ABC0` BLE sync. See the "Optional DNP — External RTC" section below.*

*2026-08-14 — v4.3: Production MD1 module swapped ANNA-B112 (nRF52832) → ANNA-B412 (u-blox nRF52833, 128 KB RAM, BT 5.1, integrated antenna). Pin map retained as design intent; module land pattern to be verified vs B412 datasheet. See `anna_b412_migration.md`.*

*2026-08-13 — v4.2: Flash U7 committed to MX25R6435F (8 MB, pin-compatible, firmware
auto-detects size). USB-C charging added as active SGC line (GCT USB4085 + CC pull-downs
+ ESD + IP67 cap); Qi receiver remains DNP. Beeper BZ1 DNP/optional (footprint retained).*

*2026-08-13 — v4.1: Qi receiver dropped (DNP — planarity/centring; USB-C + IP67 cap
as mechanical alternative). Beeper BZ1 → DNP/optional (footprint retained). Battery
JST 3-pin connector flagged provisional (varies by cell model). Flash U7 flagged
pin-compatible upgrade path (MX25R3235F / MX25R6435F).*

*2026-08-12 — v4.0: Pole-mount v1 arming switched to a Langir 16 mm piezoelectric
pushbutton. LDC1612 + PCB coil dropped from the ACTIVE BOM (footprint retained for
v2 forearm guard). Cross-arm target disc removed. Magnetic reed switch (rejected —
magnetometer interference) never entered the BOM. Schurter PSE 16 evaluated and
rejected: $32.55 and EOL.*

*2026-06-16 — v3.0: Nicla Sense ME replica architecture.*
*2026-06-09 — v2.0: RFID reader removed from active BOM.*

*Pricing as of mid-2026. Standard distributor pricing (Digi-Key, Mouser) for small quantities.*

---

## Arming button — selected: Langir 16 mm piezo

| Property | Value |
|----------|-------|
| **Part** | **Langir 16 mm piezoelectric pushbutton, momentary NO** |
| Sealing | IP68 / IP69K (mounted), nut + O-ring |
| Face | Ø16 mm stainless steel (316L), flush or raised |
| Actuating force | ≤ 3 N (back-of-glove friendly) |
| Life | **50 M cycles** (no moving part) |
| Temp | −40…+85 °C |
| Output | NO pulse 20–1000 ms |
| Cost | ~$12 single / ~$9 @100 / ~$7 @1000+ |

**Why not the Schurter PSE NO 16 (p/n 1241.2356):** $32.55 at Digi-Key and flagged
"no longer manufactured, not restocked once inventory depletes" (EOL). Same no-moving-part
benefits at half the price and still in active production from Langir.

---

## Per-Device Electronics (SGC Additions Only)

| # | Component | Spec / Part | Prototype (1–5) | Small Batch (100) | Production (1000+) |
|---|---|---|---|---|---|
| 1 | **Arming button** | Langir 16 mm piezo (IP68/IP69K, momentary NO) | $12.00 | $9.00 | $7.00 |
| 2 | **UHF RFID module** | Impinj E310 — **⚠️ v2 UNPOPULATED** | $0 | $0 | $0 |
| 3 | **UWB tag** | Qorvo DW3000 — **⚠️ v2 UNPOPULATED** | $0 | $0 | $0 |
| 4 | **RGB LED strip** | 5× SK6812-mini (2×2mm), daisy-chained | $2.50 | $1.50 | $1.00 |
| 5 | **Beeper** | Piezo surface transducer (10×10mm) — ⚠️ DNP/optional (footprint retained) | $0 | $0 | $0 |
| 6 | **USB-C charging** | GCT USB4085-GF-A + CC pull-downs + ESD + IP67 cap | $1.50 | $1.00 | $0.70 |
| 7 | **Passives + power** | Caps, resistors, P-MOSFET ×2 (v2), pull-ups | $2.80 | $1.85 | $1.35 |
| 8 | **Custom PCB** | 4-layer FR4, ~22×55mm, ENIG, 0.8mm | $15.00¹ | $8.00 | $4.00 |
| 9 | **PCB assembly** | SMD pick-and-place + reflow | —² | $5.00 | $3.00 |
| | **SGC additions subtotal** | | **$33.80** | **$26.35** | **$17.05** |

¹ Prototype PCB: 5 boards from JLCPCB ~$30 → ~$6/board. Allocated $15 includes PCB + stencil + small-order surcharge.
² Prototype: hand-assembled. Labor = your time.

**Removed from active BOM (v3.0 → v4.0):**
- LDC1612DNTR (was #1, $3.19/$2.50/$2.00) — **footprint retained, DNP, v2 forearm guard only**
- LDC1612 PCB coil (14 mm spiral) — copper-only, no cost, footprint retained
- LDC-specific passives: 100 nF + 33 pF tank cap + 2× 2.2 kΩ I²C pull-ups (reflected in #6)

**Not included in SGC BOM (part of Nicla replica):**
- MD1 BLE module — **ANNA-B412** (u-blox, nRF52833, integrated antenna) — replaces ANNA-B112
- BHI260AP, BMP390, BMM150, BME688 — already on Nicla
- MX25R6435F Flash U7 (8 MB) — pin-compatible swap from MX25R1635F (2 MB). Same SOIC-8
  pinout, same JEDEC SPI command set + SFDP. Firmware auto-detects size (SPIFBlockDevice
  SFDP); layout re-mapped for 8 MB (pre-roll stays 0x0000–0x13FFF).
- BQ25120A charger, IS31FL3194 RGB LED driver, LDOs — Nicla power management
- Battery (Renata ICP622540PMT) — ⚠️ provisional; JST 3-pin connector may change per cell
- Qi coil / IP6833 receiver — ⚠️ DROPPED (DNP); USB-C + IP67 cap as mechanical alternative
- 32 kHz + 32 MHz crystals — Nicla stock

### MD1 — Production BLE Module

| Ref | Component | Manufacturer | MPN | Qty | Status |
|-----|-----------|-------------|-----|-----|--------|
| MD1 | ANNA-B412 | u-blox | ANNA-B412 (confirm exact ordering code, e.g. ANNA-B412-00B) | 1 | Active |

- **SoC:** nRF52833 (128 KB RAM / 512 KB Flash), Bluetooth 5.1.
- **Antenna:** integrated — no external ANT matching network unless the B412 datasheet requires one.
- **Migration:** replaces ANNA-B112 (nRF52832). Pin map retained as design intent; land pattern must be verified against the u-blox ANNA-B412 datasheet.
- See `anna_b412_migration.md`.

## Optional DNP — External RTC (⚠️ NOT POPULATED in v1)

Reserved footprint for a future **backup time** source. Primary time = phone `ABC0`
BLE sync. **No parts are loaded in v1** — these are DNP lines only (footprint + net
present on the PCB). See `sgc_architecture_hardware.md` §12.

| Ref | Component | Spec / Part | Qty | Status |
|-----|-----------|-------------|-----|--------|
| U12 | **RTC** | Micro Crystal **RV-3028-C7** (I²C, integrated XTAL, 0x52, DFN-8 3.2×1.5×0.8 mm) | 1 | **DNP** |
| BT1 | **Coin cell holder** | CR1220 2-pad SMD (12.5 mm) — VBACKUP | 1 | **DNP** |
| R_RTC0 | 0 Ω jumper | 0603 — VBAT↔VDD tie (populate only if no coin cell) | 1 | **DNP** |
| C_RTC1 | 100 nF, 10 V, X5R | 0603 — VDD decoupling | 1 | **DNP** |
| C_RTC2 | 100 nF, 10 V, X5R | 0603 — VBACKUP decoupling | 1 | **DNP** |

- **Bus:** `Wire` (I2C0) P0.22/P0.23, address **0x52** — no clash: LDC1612 0x2A on the
  same bus; IS31FL3194 0x53 on `Wire1` (P0.15/P0.16), a different bus.
- **Backup options:** populate **BT1** (coin cell) for true battery backup, **or**
  populate **R_RTC0** (0 Ω) to tie VBAT→VDD ("no backup until populated").
- **/INT (`RTC_INT`):** unconnected test point (DNP) — no GPIO assigned; a spare B412
  GPIO may be routed later after the datasheet pin table is verified.
- **Cost:** RV-3028-C7 ~$1.50 single / ~$1.00 @100; coin cell holder ~$0.30; passives
  negligible. **$0 in v1 (all DNP).**

## Enclosure + Mechanical

| # | Component | Spec | Prototype | Small Batch | Production |
|---|---|---|---|---|---|
| 9 | **Enclosure** | Custom IP67 polycarbonate, 2-shell, translucent, **Ø16mm button boss** | $20.00¹ | $12.00 | $6.00 |
| 10 | **Mounting strap** | Elastic pole strap + quick-release buckle | $5.00 | $3.00 | $1.50 |
| | **Mechanical subtotal** | | **$25.00** | **$15.00** | **$7.50** |

¹ Enclosure prototype: 3D-printed PC-like resin, includes the button panel cutout + nut boss.

**Removed (v3.0 → v4.0):** cross-arm target disc (was #11, $0.20/$0.10/$0.05) — no longer needed with button arming.

---

## Total Per-Device Cost (Langir 16 mm piezo)

| Scale | SGC Additions | Mechanical | **Total** |
|-------|:---:|:---:|---:|
| Prototype (1–5) | $33.80 | $25.00 | **$58.80** |
| Small Batch (100) | $26.35 | $15.00 | **$41.35** |
| Production (1000+) | $17.05 | $7.50 | **$24.55** |

---

## Cost comparison — arming options (whole-device total)

| Arming | Button cost (1/100/1000+) | **Device total (1/100/1000+)** | Notes |
|--------|---------------------------|-------------------------------|-------|
| **Langir 16 mm piezo ✅** | $12 / $9 / $7 | **$58.80 / $41.35 / $24.55** | **selected** — in-production, IP68/69K, 50 M cyc |
| Schurter PSE 16 | $32.55 / $25 / $20 | $79.35 / $57.35 / $37.55 | rejected — EOL + price |
| Anti-vandal 16 mm (IP67) | $4 / $2.50 / $1.80 | $50.80 / $34.85 / $19.35 | budget fallback, true hold, moving part |
| *(v3.0 LDC1612 baseline)* | — | *$50.39 / $35.10 / $19.75* | *for reference* |
