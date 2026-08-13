# SGC — USB-C Charging Port Spec (v1.1 — finalized)

*2026-08-13 — replaces dropped Qi wireless receiver (U11 / IP6833). Mechanical charging
alternative: USB-C receptacle wired as a sink + IP67 tethered cap.*

> **Status (v4.2):** Finalized + integrated into the schematic (`J2` USB-C, `R_CC1`/`R_CC2`
> 5.1 kΩ CC pull-downs, `C_USB` 10 µF bulk) and BOM. Part: GCT USB4085-GF-A (16-pin).

## Context

Qi (IP6833 + coil) was dropped due to planarity + centring issues. The **BQ25120A**
charger already accepts 5 V at `IN` (4.35–5.5 V), so USB-C charging needs **no additional
charging IC** — only a USB-C receptacle wired as a sink, ESD protection, and a sealed cap.

---

## 1. Connector

| Property | Value |
|----------|-------|
| **Part** | **GCT USB4085-GF-A** (16-pin USB-C 2.0 receptacle) |
| Mounting | Mid-mount SMD, through-hole shell stakes (mechanical retention) |
| Power rating | VBUS 5 A, GND 6.25 A (massively over-spec'd for ≤300 mA charge) |
| Temp | −40…+85 °C (ski range) |
| Mating cycles | 10k–20k |
| Cost | ~$0.60–0.80 @ qty |

**Why 16-pin over a 6-pin charging-only part:** keeps D+/D− and SBU available for future
(USB2.0 debug/serial), and the USB4085 has strong shell stakes to survive cable insertion
force (5–20 N) on a 0.8 mm board. A 6-pin (VBUS/GND/CC1/CC2/D+/D−) charging-only part is a
viable space/cost fallback.

### Electrical hookup (charging-only)

| USB-C pin | Net | Connect to |
|-----------|-----|------------|
| A4/A9/B4/B9 | VBUS | → BQ25120 `IN` (via 10 µF bulk + ESD) |
| A1/A12/B1/B12 + shell | GND | → GND plane |
| A5 (CC1) | CC1 | 5.1 kΩ (1%) → GND |
| B5 (CC2) | CC2 | 5.1 kΩ (1%) → GND |
| A6/A7, B6/B7 (D+/D−) | — | NC (or test points for future) |
| A8/B8 (SBU1/2) | — | NC |

- **CC pull-downs** (5.1 kΩ = "sink" Rd) advertise the device as a sink so any USB-C
  source provides 5 V. Legacy USB-A→C adapters also work (5 V, 500 mA–2.4 A).
- **ESD:** TVS on VBUS + CC (exposed port, gloves/static). Nexperia **PESD5V0U2UT** or
  TI **TPD4E05U06**.
- **Bulk cap** 10 µF at the connector for hot-plug inrush; BQ25120 ref design adds its own
  1 µF on `IN`.

---

## 2. Cap (IP67 seal)

- **Recommended (production):** custom-molded **silicone (shore A ~50) tethered dust cap**,
  IP67, keyed to a boss around the enclosure opening. Tether = molded living hinge or
  lanyard. Plug ≈ 8.6 × 2.9 mm with a sealing lip/groove for positive retention.
- **Off-the-shelf (prototype):** generic IP67 silicone USB-C plug (PortPlugs, ModCover,
  etc.) — fine for validation; replace with custom-molded for production.

---

## 3. Enclosure / PCB integration

- Connector is mid-mount → board edge sits flush with the enclosure port opening
  (USB-C opening nominal 8.3 × 2.6 mm + clearance).
- Shell stakes must be soldered (TH) for pull-out strength.
- Cap needs a recess/boss so it sits flush and seals (no snag on ski poles/gloves).

---

## Open items

- [ ] Confirm 16-pin vs 6-pin (space/cost tradeoff on 22 × 55 mm).
- [ ] Choose ESD part (PESD5V0U2UT vs TPD4E05U06).
- [ ] Custom-mold cap vs off-the-shelf for P0.
