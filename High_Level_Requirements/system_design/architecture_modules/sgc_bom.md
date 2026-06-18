# SGC — Bill of Materials (v3.0)

*2026-06-16 — v3.0: Nicla Sense ME replica architecture. External W25Q16 removed — Nicla's onboard MX25R1635F (2 MB, U7) is sufficient. Reworked for BHI260AP/BMP390 via Arduino_BHY2 on internal SPI bus.*

*2026-06-09 — v2.0: RFID reader removed from active BOM (unpopulated footprint only).*

*Pricing as of mid-2026. Based on standard distributor pricing (Digi-Key, Mouser) for small quantities.*

---

## Nicla Sense ME Replica (Stock — single board)

The custom PCB is a strict replica of the Arduino Nicla Sense ME. The base BOM
is the Nicla board itself — all on-board components are replicated. This BOM
lists only the **SGC-specific additions** beyond the Nicla replica.

## Per-Device Electronics (SGC Additions Only)

| # | Component | Spec / Part | Prototype (1–5) | Small Batch (100) | Production (1000+) |
|---|---|---|---|---|---|
| 1 | **Inductive sensor** | LDC1612DNTR (12-WSON, 2-ch) | $3.19 | $2.50 | $2.00 |
| 2 | **UHF RFID module** | Impinj E310-based — **⚠️ v2 UNPOPULATED FOOTPRINT ONLY** | $0 | $0 | $0 |
| 3 | **UWB tag** | Qorvo DW3000 (5×5 QFN) — **⚠️ v2 UNPOPULATED FOOTPRINT ONLY** | $0 | $0 | $0 |
| 4 | **RGB LED strip** | 5× SK6812-mini (2×2mm each), daisy-chained | $2.50 | $1.50 | $1.00 |
| 5 | **Beeper** | Piezo surface transducer (10×10mm) | $1.50 | $1.00 | $0.70 |
| 6 | **Passives + power** | Caps, resistors, P-MOSFET ×2 (v2), pull-ups | $3.00 | $2.00 | $1.50 |
| 7 | **Custom PCB** | 4-layer FR4, ~22×55mm, ENIG, 0.8mm | $15.00¹ | $8.00 | $4.00 |
| 8 | **PCB assembly** | SMD pick-and-place + reflow | —² | $5.00 | $3.00 |
| | **SGC additions subtotal** | | **$25.19** | **$20.00** | **$12.20** |

¹ Prototype PCB: 5 boards from JLCPCB ~$30 → ~$6/board. Allocated $15 includes PCB + stencil + small-order surcharge.
² Prototype: hand-assembled. Labor = your time.

**Not included in SGC BOM (part of Nicla replica):**
- nRF52832 (ANNA-B112 module), BHI260AP, BMP390, BMM150, BME688 — already on Nicla
- MX25R1635F Flash U7 (2 MB) — Nicla's onboard flash, sufficient for SGC
- BQ25120A charger, IS31FL3194 RGB LED driver, LDOs — Nicla power management
- Battery (Renata ICP622540PMT), Qi coil — Nicla standard accessories
- 32 kHz + 32 MHz crystals — Nicla stock

## Enclosure + Mechanical (unchanged)

| # | Component | Spec | Prototype | Small Batch | Production |
|---|---|---|---|---|---|
| 9 | **Enclosure** | Custom IP67 polycarbonate, 2-shell, translucent | $20.00¹ | $12.00 | $6.00 |
| 10 | **Mounting strap** | Elastic forearm band + quick-release clip | $5.00 | $3.00 | $1.50 |
| 11 | **Cross-arm target disc** | Passive copper/iron foil disc embedded in opposite strap | $0.20 | $0.10 | $0.05 |
| | **Mechanical subtotal** | | **$25.20** | **$15.10** | **$7.55** |

---

## Total Per-Device Cost

| Scale | SGC Additions | Mechanical | **Total** |
|-------|:---:|:---:|---:|
| Prototype (1–5) | $25.19 | $25.20 | **$50.39** |
| Small Batch (100) | $20.00 | $15.10 | **$35.10** |
| Production (1000+) | $12.20 | $7.55 | **$19.75** |
