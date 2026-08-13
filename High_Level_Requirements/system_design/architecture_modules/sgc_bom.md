# SGC — Bill of Materials (v4.2 — Langir piezo)

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
- nRF52832 (ANNA-B112 module), BHI260AP, BMP390, BMM150, BME688 — already on Nicla
- MX25R6435F Flash U7 (8 MB) — pin-compatible swap from MX25R1635F (2 MB). Same SOIC-8
  pinout, same JEDEC SPI command set + SFDP. Firmware auto-detects size (SPIFBlockDevice
  SFDP); layout re-mapped for 8 MB (pre-roll stays 0x0000–0x13FFF).
- BQ25120A charger, IS31FL3194 RGB LED driver, LDOs — Nicla power management
- Battery (Renata ICP622540PMT) — ⚠️ provisional; JST 3-pin connector may change per cell
- Qi coil / IP6833 receiver — ⚠️ DROPPED (DNP); USB-C + IP67 cap as mechanical alternative
- 32 kHz + 32 MHz crystals — Nicla stock

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
