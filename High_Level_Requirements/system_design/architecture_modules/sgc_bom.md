# SGC — Bill of Materials (v4.3 — MAX17048 fuel gauge)

*2026-10-28 — v4.3: **MAX17048G+T10 fuel gauge added** as active line #10 (+ gauge
passives #11) — v2 custom PCB only (AD-029); the BQ25120A band-gauge is demoted to
fallback on gauge-equipped boards. SGC-additions subtotal $37.60 / $29.19 / $19.57
(proto / 100 / 1000+); device totals +$3.80 / +$2.84 / +$2.52. Pricing verified
2026-10-28 (Mouser/Farnell/Findchips): $4.45 @1 / $2.80 @100 / $2.66 @250 / $2.56
@1000 (Mouser USD); BOM lines carry $3.60 proto (distributor single-piece cut tape),
$2.80 @100, $2.50 @1000+ (authorized-reel blend; broker/reel channels dip to
~$1.80–2.25 — deliberately not assumed).*

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
| 10 | **Fuel gauge** | MAX17048G+T10 (TDFN-8), I²C 0x36 — ⚠️ **v2 custom PCB only** (P0 Nicla: BQ band-gauge) | $3.60 | $2.80 | $2.50 |
| 11 | **Fuel-gauge passives** | 0.1 µF X7R (VDD decoupling) + 10 kΩ (ALRT pull-up), 0603 | $0.20 | $0.04 | $0.02 |
| | **SGC additions subtotal** | | **$37.60** | **$29.19** | **$19.57** |

¹ Prototype PCB: 5 boards from JLCPCB ~$30 → ~$6/board. Allocated $15 includes PCB + stencil + small-order surcharge.
² Prototype: hand-assembled. Labor = your time.
³ MAX17048 pricing assumptions (verified 2026-10-28): proto = distributor single-piece
cut tape (Digi-Key ~$3.62, Mouser $4.45 → $3.60 mid taken); @100 = Mouser $2.80;
@1000+ = authorized-reel blend (Mouser $2.56 @1k, Verical $2.24 @2.5k reel → $2.50).
A 200/month steady-state buy lands at the @250 break ($2.66 Mouser / £2.11 Farnell
≈ $2.70) — the cost model carries $2.80 conservatively. Passives = LCSC basic-part
pricing. Broker/reel channels dip to ~$1.80–2.25 and are NOT assumed.

**Removed from active BOM (v3.0 → v4.0):**
- LDC1612DNTR (was #1, $3.19/$2.50/$2.00) — **footprint retained, DNP, v2 forearm guard only**
- LDC1612 PCB coil (14 mm spiral) — copper-only, no cost, footprint retained
- LDC-specific passives: 100 nF + 33 pF tank cap + 2× 2.2 kΩ I²C pull-ups (reflected in #6)

**Not included in SGC BOM (part of Nicla replica):**
- nRF52832 (ANNA-B112 module), BHI260AP, BMP390, BMM150, BME688 — already on Nicla. **Custom-board radio module: ANNA-B402** (nRF52833, decided 2026-09-10 — supersedes B112/B412 evaluations); add to the module line-item at the next BOM revision
- MX25R6435F Flash U7 (8 MB) — pin-compatible swap from MX25R1635F (2 MB). Same SOIC-8
  pinout, same JEDEC SPI command set + SFDP. Firmware auto-detects size (SPIFBlockDevice
  SFDP); layout re-mapped for 8 MB (pre-roll stays 0x0000–0x13FFF).
- BQ25120A charger, IS31FL3194 RGB LED driver, LDOs — Nicla power management. On the v2 custom PCB the BQ's band-gauge is fallback only — the MAX17048 (#10) is the preferred SoC source (AD-029)
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

> Numbering note (v4.3): electronics rows renumbered 1–11; the mechanical table keeps
> its own sequence — the duplicate #9/#10 across the two tables is cosmetic and is to
> be resolved at the next full BOM renumber.

**Removed (v3.0 → v4.0):** cross-arm target disc (was #11, $0.20/$0.10/$0.05) — no longer needed with button arming.

---

## Total Per-Device Cost (Langir 16 mm piezo)

| Scale | SGC Additions | Mechanical | **Total** | Δ vs v4.2 (gauge) |
|-------|:---:|:---:|---:|---:|
| Prototype (1–5) | $37.60 | $25.00 | **$62.60** | +$3.80 |
| Small Batch (100) | $29.19 | $15.00 | **$44.19** | +$2.84 |
| Production (1000+) | $19.57 | $7.50 | **$27.07** | +$2.52 |

---

## Cost comparison — arming options (whole-device total)

| Arming | Button cost (1/100/1000+) | **Device total (1/100/1000+)** | Notes |
|--------|---------------------------|-------------------------------|-------|
| **Langir 16 mm piezo ✅** | $12 / $9 / $7 | **$62.60 / $44.19 / $27.07** | **selected** — in-production, IP68/69K, 50 M cyc |
| Schurter PSE 16 | $32.55 / $25 / $20 | $83.15 / $60.19 / $40.07 | rejected — EOL + price |
| Anti-vandal 16 mm (IP67) | $4 / $2.50 / $1.80 | $54.60 / $37.69 / $21.87 | budget fallback, true hold, moving part |
| *(v3.0 LDC1612 baseline)* | — | *$54.19 / $37.94 / $22.27* | *for reference (incl. gauge adder for like-for-like)* |

Device totals include the v4.3 fuel-gauge adder (+$3.80 / +$2.84 / +$2.52, lines #10–#11).
