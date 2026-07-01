# SGC — Production Strategy (v1.3)

*2026-06-29 — v1.3: Detailed cost breakdown (electronics + mechanical per phase), added Section 10 — Selling Price & Margin analysis.*

*2026-06-29 — v1.2: Added Section 5 — Mechanical Design (forearm guard, polycarbonate shell, potting, emergency port/power cut, production phasing for enclosure).*
*2026-06-29 — v1.1: Added phased ramp-up strategy (P0–P3). Steady-state 200/month is a target, not the starting point.*
*2026-06-29 — v1.0: Initial version. Captures strategy for producing ~200 units/month using off-the-shelf Nicla Sense ME + custom companion carrier PCB, side-by-side mounting.*

---

## 1. Phased Ramp-Up Strategy

> **Core principle:** Never build inventory without validated demand. Each phase must prove itself before the next gets funded.

### Phase 0 — Prototype (NOW)

| Parameter | Value |
|-----------|-------|
| Quantity | 5–10 units |
| Hardware | Existing custom PCB (ANNA-B112, Nicla replica) |
| Assembly | Manual / in-house |
| Cost per unit | ~€100–150 (manual labor, one-off parts) |
| Total investment | ~€500–1,500 |

**Goal:** Field validation. Put devices on real skiers, coaches, race officials.
- Does the proximity arming work on snow?
- Is BLE range sufficient at race speeds?
- Is battery life acceptable in -10°C?
- Does the beeper work through a helmet + wind noise?
- Firmware hardened through real-world abuse.

**Exit criteria:** 3+ successful on-snow test sessions, no hardware blockers.

### Phase 1 — Pilot Batch

| Parameter | Value |
|-----------|-------|
| Quantity | 20–50 units |
| Hardware | First companion carrier PCB + off-the-shelf Nicla |
| Assembly | Small EMS batch OR hand-assembled with carrier PCB from fab |
| Cost per unit | ~€120–180 (low volume, no economies of scale) |
| Total investment | ~€2,500–9,000 |

**Goal:** Early adopter validation.
- Sell or lend to friendly ski clubs, coaches, race teams
- Gather testimonials, feedback, bug reports
- **Pre-order campaign** to gauge real demand before committing to Phase 2
- Use pre-order numbers to calibrate Phase 2 batch size

**Exit criteria:** Pre-orders ≥ Phase 2 minimum batch (or strong qualitative demand signals).

### Phase 2 — First Market Season

| Parameter | Value |
|-----------|-------|
| Quantity | 100–200 units (over full season) |
| Hardware | Refined companion carrier PCB (v2 incorporating P1 feedback) |
| Assembly | EMS partner, small batch (50 units/batch) |
| Cost per unit | ~€100–130 |
| Total investment | ~€10,000–26,000 |

**Goal:** Real market validation.
- Sell at full price to paying customers
- **Build-to-order bias:** Produce in small batches triggered by actual orders, not forecasts
- Keep buffer stock minimal (~20-30 units for warranty replacements)
- Collect: return rate, support tickets, NPS, feature requests

**Exit criteria:** 100+ units sold, return rate < 5%, positive NPS. Demand clearly exceeding 200/season.

### Phase 3 — Steady State

| Parameter | Value |
|-----------|-------|
| Quantity | 200/month |
| Hardware | Production-optimized companion carrier PCB (v3) |
| Assembly | Full EMS panelized, bed-of-nails QA |
| Cost per unit | ~€95–116 (target) |

**Goal:** Scale with confidence.

**Only enter Phase 3 when:**
- Phase 2 sold out with waiting list
- Unit economics confirmed (gross margin ≥ target)
- Supply chain stable (lead times known, alternates qualified)
- EMS partner proven reliable through Phase 2 batches

---

## 2. Phase Transition Gates

```
P0: Prototype ──→ P1: Pilot ──→ P2: First Season ──→ P3: Steady State
  (10 units)      (20-50)       (100-200)              (200/month)
      │               │               │                      │
      ▼               ▼               ▼                      ▼
  Field test     Pre-orders      Sales data           Demand proven
  success        ≥ min batch     positive             waiting list
```

**Key financial rule:** Each phase funds itself (or at least proves the case for the next). P0 is R&D cost. P1 is market exploration. P2 should be cash-flow positive. P3 is scaling a proven business, not a bet.

---

## 3. Strategic Context

### Why Not a Custom Arduino Pro Factory Spin?

- **Nicla Sense ME layout files are NOT publicly available.** Arduino Pro keeps the PCB design proprietary.
- **Arduino Pro B2B custom runs** require minimum order quantities (MOQs) in the **thousands** per batch — far above where we are (and only reachable at P3+).
- **Arduino Pro does offer** specialized versions for large commercial volumes (ODM services, custom sensor integration, factory firmware flashing), but only at enterprise scale.

### Volume Mapping by Phase

| Phase | Volume | Approach |
|-------|--------|----------|
| P0 | 5–10 | Manual in-house (dev PCB) |
| P1 | 20–50 | Small EMS batch or manual + fab PCB |
| P2 | 100–200/season | EMS small batches (50/batch) |
| P3 | 200/month | Full EMS panelized assembly |
| Arduino Pro | 5,000+/batch | Direct ODM custom spin |

200/month is the **destination**, not the departure point.

---

## 4. Architecture: Off-the-Shelf Nicla + Companion Carrier PCB

### ⚠️ Stacked Mounting (REJECTED)

Soldering the Nicla Sense ME **on top** of the carrier PCB via its castellated solder pads is **not feasible**:

- The Nicla Sense ME is ~5 mm thick (components on both sides)
- Stacking adds carrier PCB thickness → total assembly exceeds enclosure constraints
- Castellated pads on the Nicla edge require precise alignment and reflow, complex for a 2-board stack

### ✅ Side-by-Side Mounting (ADOPTED)

The Nicla Sense ME sits **next to** the companion carrier PCB on the same plane. They are connected via the Nicla's header pins or a short flex cable.

```
┌─────────────────────────────────────────────────────────┐
│                    ENCLOSURE (IP67)                       │
│                                                          │
│  ┌──────────────────┐    ┌───────────────────────────┐  │
│  │ ArduinO Nicla    │    │  Custom Companion         │  │
│  │ Sense ME         │◄──►│  Carrier PCB              │  │
│  │ (off-the-shelf)  │    │                           │  │
│  │                  │    │  ┌─────────────────────┐  │  │
│  │  nRF52832        │    │  │ LDC1612 (I²C 0x2A) │  │  │
│  │  BHI260AP        │    │  │ 5V Boost SOT-23    │  │  │
│  │  BMP390          │    │  │ Level Shifter      │  │  │
│  │  BQ25120A        │    │  │ SK6812 ×5 header   │  │  │
│  │  MX25R1635F      │    │  │ Battery JST conn   │  │  │
│  │  BLE Antenna     │    │  └─────────────────────┘  │  │
│  └──────────────────┘    └───────────────────────────┘  │
│                                                          │
│  [ Battery (sub-zero LiPo, Grepow/Ultralife) ]           │
└─────────────────────────────────────────────────────────┘
```

### Connection

Nicla pins are brought out via its edge headers/castellated pads to the companion carrier PCB. Options:

- **Rigid flex interconnect** (preferred): Single flex PCB that folds — Nicla soldered on one end, carrier circuit on the other
- **Pin headers + ribbon cable**: Nicla's standard 0.1" headers → ribbon → carrier board

---

## 5. Mechanical Design — The Forearm Guard

The product is **first and foremost a forearm protector** for ski racers. Gate impacts at 80+ km/h are the primary physical threat. Performance timing is the differentiating feature — but protection is the non-negotiable baseline.

### 5.1 Product Identity

```
┌─────────────────────────────────────────────────────┐
│              SGC FOREARM GUARD                       │
│                                                      │
│   Protection first ── Timing as the plus             │
│                                                      │
│   Protects the athlete from slalom gate impacts      │
│   Measures split/run times via BLE to phone          │
│   Visible status via RGB LEDs through shell          │
└─────────────────────────────────────────────────────┘
```

### 5.2 Layer Stack (cross-section)

```
  OUTSIDE (gate side)
  ┌──────────────────────────────────┐
  │ ① Polycarbonate shell (2-3 mm)  │  ← Transparent, impact-resistant
  │    - Curved to forearm contour   │     Gate hits THIS surface
  │    - Translucent for LED glow    │
  ├──────────────────────────────────┤
  │ ② Electronics (potted)          │  ← Epoxy/silicone potting
  │    Nicla + Carrier PCB + Battery │     Waterproof, shockproof
  │    SK6812 LEDs facing shell      │     No moving parts
  ├──────────────────────────────────┤
  │ ③ Padding (EVA foam, 5-8 mm)   │  ← Closed-cell, impact absorption
  │    - Conforms to athlete's arm   │     Comfort + protection
  │    - Velcro straps               │
  └──────────────────────────────────┘
  INSIDE (athlete's forearm)
```

### 5.3 Materials

| Layer | Material | Rationale |
|-------|----------|-----------|
| Outer shell | **Polycarbonate (PC)** | High impact resistance (250× glass), transparent for LEDs, -40°C to +120°C range, UV-stabilized grades available |
| Potting | **Epoxy** or **silicone** | IP67 sealing, vibration dampening, thermal conduction, tamper-proof |
| Padding | **Closed-cell EVA foam** | Water-resistant (won't absorb sweat/snow), progressive impact absorption, comfortable |
| Straps | **Silicone-coated elastic** | Non-slip on ski suit, adjustable, cold-flexible |

### 5.4 Emergency Features

Two critical safety features accessible without destroying the sealed enclosure:

#### Emergency Programming Port

A small, waterproof-accessible SWD interface for field recovery:

- **Pogo-pin pad area** on the PCB, aligned with a sealed access port in the shell
- **Waterproof plug** (silicone gasket, screw-on cap) normally sealed
- Accessed ONLY when firmware is bricked or needs field update without disassembly
- SWD pins (SWDIO, SWCLK, GND, VCC) exposed on gold-plated pads
- Magnetic or threaded cap — tool-required to prevent accidental opening

```
  Polycarbonate shell
  ┌────────────────────┐
  │  ○ ○ ○ ○          │  ← Pogo-pad area (4 pads)
  │  ▼ plug            │  ← Sealed port (normally covered)
  └────────────────────┘
       │
       ▼
  ┌────────────────┐
  │  SWD  GND  VCC │  ← Gold pads on PCB
  │  SWCLK SWDIO    │
  └────────────────┘
```

#### Emergency Power Cut

A physical way to kill power if the device locks up (no software dependency):

- **Magnetic reed switch** (normally-closed) in series with battery positive rail
- Strong magnet held against marked spot on shell → opens circuit → hard power-off
- No mechanical penetration of the IP67 seal
- After removal of magnet: circuit closes → device reboots cleanly
- Alternative: **Physical latching switch** behind a secondary sealed port (simpler but less elegant)

```
  Battery (+) ──→ Reed Switch (NC) ──→ System Power
                       │
                  Magnet here
                  (marked on shell)
                  opens circuit
```

### 5.5 Assembly Sequence

1. **PCB assembly** (EMS): Carrier PCB + components + Nicla mounted
2. **Firmware flash + QA** (bed-of-nails, SWD)
3. **Battery connect** → power-on self-test
4. **Potting:** Electronics placed in shell cavity, epoxy/silicone poured, cured
5. **Padding attachment:** EVA foam cut to shape, adhesive-bonded to inner shell face
6. **Straps:** Velcro straps riveted or looped through shell slots
7. **Final QC:** Waterproof test (IP67), LED visibility check, BLE range test, impact test (sample batch)
8. **Packaging:** Retail box with quick-start card, charging cable, spare strap set

### 5.6 Enclosure Dimensions (target)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Length | ~180-220 mm | Covers mid-forearm to wrist |
| Width | ~60-80 mm | Fits under ski suit sleeve |
| Thickness | ~15-20 mm total | Shell + electronics + padding |
| Weight | ~150-250 g | Including battery, target < 300g |
| Curvature | Radius ~50-70 mm | Matches average adult forearm |

### 5.7 Production Phasing for Mechanical

| Phase | Shell | Padding | Straps | Potting |
|-------|-------|---------|--------|---------|
| P0 (prototype) | 3D printed PC-like resin or vacuum-formed PC sheet | Cut EVA sheet, double-sided tape | Off-shelf velcro straps | Manual epoxy pour |
| P1 (pilot) | Small-run vacuum-formed PC, CNC trimmed | Die-cut EVA foam | Custom elastic with silicone print | Manual or low-pressure dispensing |
| P2 (first season) | Injection-molded PC (simple 2-part mold) | Die-cut EVA, adhesive jig | Custom straps, riveted | Automated dispensing, curing oven |
| P3 (steady state) | Injection-molded PC (multi-cavity, textured finish) | Die-cut EVA, automated placement | Overmolded strap anchors | 2-part silicone, automated dispensing + vacuum degas |

---

## 6. Companion Carrier PCB — Bill of Materials

| Ref | Component | Package | Qty | Purpose |
|-----|-----------|---------|-----|---------|
| U_QI | IP6833 Qi Receiver | QFN-28 4×4mm | 1 | 5W Qi wireless power receiver (see §6.2) |
| L_QI | Qi receiver coil 24 µH | WPC A11 48×32mm | 1 | Wound wire + ferrite shield |
| C_AC1/2 | 22 nF, 50V, NP0/C0G | 0603 | 2 | Qi resonant capacitors |
| C_RECT | 22 µF, 16V, X5R | 0805 | 1 | Rectifier output filter |
| C_BOOT | 100 nF, 16V, X5R | 0603 | 1 | Bootstrap capacitor |
| U1 | LDC1612 | WSON-12 | 1 | Inductive proximity sensor |
| L1 | PCB trace coil | 14mm spiral | 1 | LDC1612 sensing coil |
| U2 | MT3608 Boost Converter | SOT-23-6 | 1 | 5V rail for SK6812 LEDs (see §6.1) |
| L_Boost | 4.7 µH power inductor, Isat ≥ 2A | 0805/1206 | 1 | Boost inductor |
| C_IN | 10 µF, 10V, X5R | 0805 | 1 | Boost input decoupling |
| C_OUT | 22 µF, 10V, X5R | 0805 | 1 | Boost output filter |
| R_FB1 | 75 kΩ, 1% | 0603 | 1 | Boost feedback divider high-side |
| R_FB2 | 10 kΩ, 1% | 0603 | 1 | Boost feedback divider low-side |
| U3 | Level Shifter (74AHCT1G125) | SOT-23-5 | 1 | 3.3V→5V data line shift |
| D1-D5 | SK6812-mini RGBW | 2×2mm | 5 | Status indicator LEDs |
| BZ1 | Piezo beeper | SMD | 1 | Audible feedback |
| J1 | Battery connector | JST-PH 2-pin | 1 | LiPo battery input |
| J2 | Nicla interconnect | 0.1" header/flex | 1 | Connection to Nicla |
| R1-R4 | 2.2kΩ pull-up × 2, 10kΩ × 2 | 0603 | 4 | I²C pull-ups, misc |
| C1-C4 | 100nF, 10µF decoupling | 0603 | 4 | Power decoupling |

### 6.1 5V Boost Rationale

**Why MCP1640/TPS61322x were rejected:**
- MCP1640: 350 mA peak switch current is insufficient. From 3.0V Li-Po → 5V @ 300 mA:
  I_sw_peak ≈ (5V × 0.3A) / (3.0V × 0.85 η) + 0.1A ripple ≈ 0.69 A >> 0.35 A limit.
- TPS61322x: Fixed-output variants exist but harder to source at JLCPCB;
  lower switch current variants less suited for 5× SK6812 at full brightness.

**MT3608 selected (Aerosemi, SOT-23-6):**
- Input: 2.0–24 V → works with depleted battery (3.0V cutoff)
- Switch current: 4 A peak → massive headroom for 300 mA LED load
- Fixed 1.2 MHz switching → 4.7 µH inductor (0805, ~$0.05)
- Vout = 0.6V × (1 + 75k/10k) = 5.1 V through external feedback divider
- EN pin → nRF52 GPIO for software power gate (shutdown ~2 µA)
- 65 µA quiescent in PFM light-load mode, ~88% efficiency at 300 mA load
- Temp range: −40 to +85 °C
- LCSC #C84817, JLCPCB basic part, ~$0.08–0.15 (volume). Widely second-sourced.

**5× SK6812-mini at full white:** ~60 mA per LED → 300 mA total peak at 5 V.
Sequential animation reduces average to ~15 mA. Boost sized for full worst case.

**Drop-in alternatives:**

| IC | Sw. I | Vin min | Cost | Notes |
|----|-------|---------|------|-------|
| FP6291 | 2.5 A | 2.6 V | ~$0.10 | Higher Vin min, less depleted-battery headroom |
| TPS61023 | 3.7 A | 0.5 V | ~$0.45 | TI quality, SOT-563 (tiny, harder to hand-solder) |

### 6.2 Qi Wireless Receiver Rationale

The companion carrier PCB hosts the Qi charging front-end because:
- The off-the-shelf Nicla Sense ME does NOT have built-in Qi charging
  (it charges via USB-C only, using its BQ25120A).
- The battery lives on the carrier PCB; charging must happen there.
- The Qi receiver's 5V output feeds the Nicla's VIN pin → BQ25120A IN → charges battery.

**IP6833 selected (Injoinic, QFN-28):**
- QFN-28 at 4×4mm, 0.4mm pitch — JLCPCB-assemblable (unlike BQ51013B's DSBGA)
- WPC Qi BPP (5W) compliant, works with any Qi charging pad
- Integrated full-bridge synchronous rectifier (no external diodes)
- Integrated 5V LDO (up to 1.6A) — feeds BQ25120A IN at max 300mA charge rate
- Integrated 32-bit MCU handles Qi protocol (ASK modulation, FSK demod, FOD)
- Minimal BOM: coil, resonant caps, filter caps — ~6 passives total
- LCSC available, ~$1-2 (volume)
- Temp range: −40 to +85°C

**Qi coil:** Würth 760308101 (24 µH, WPC A11) or equivalent wound coil with ferrite
backing. Wound coil gives 75-80% system efficiency vs ~60% for PCB spiral.

**For P0 prototype:** Use a pre-built BQ51013B module (Adafruit #1901 or generic,
$5-15). Solder V+ and GND pads to the Nicla replica PCB's Qi input.

### Level Shifter Rationale

- Nicla's nRF52832 outputs 3.3V logic
- SK6812 (WS2812-compatible) requires 5V logic, especially in cold temps where margin shrinks
- 74AHCT1G125 in SOT-23-5: single channel, fast enough for 800 kHz NZR
- Alternative: Dedicated level shifter IC like TXB0101 (auto-direction, but slightly slower)

### No 5V for Nicla

- Nicla is powered directly from the LiPo battery via its VIN pin
- Nicla's onboard BQ25120A handles charging via USB-C
- Qi receiver 5V output → Nicla VIN pin → BQ25120A IN → charges battery
- 5V boost only powers the LED strip data + power rails

---

## 7. Production Flow (Phase 3 — Steady State)

### 7.1 Sourcing

| Item | Source | Notes |
|------|--------|-------|
| Nicla Sense ME | Arrow / DigiKey / OKdo | Wholesale batches of 200 |
| Carrier PCB | JLCPCB / PCBWay | Panelized, ENIG finish |
| Components | LCSC / Mouser | BOM parts for carrier PCB |
| Battery | Grepow / Ultralife | Sub-zero LiPo, -20°C rated |
| Polycarbonate shell | Local injection molder | Multi-cavity tooling amortized |
| EVA foam | Foam converter / die-cutter | Closed-cell, custom shape |
| Straps | Textile supplier | Silicone-coated elastic + Velcro |
| Potting compound | Epoxy/silicone distributor | 2-part, production drums |

### 7.2 Assembly (EMS Partner)

1. **Carrier PCB fabrication:** Panelized in grids (e.g., 20-50 boards per panel) for efficient pick-and-place
2. **SMT assembly:** Automated pick-and-place for all carrier PCB components (LDC1612, boost, shifter, SK6812, passives)
3. **Reflow soldering:** Standard lead-free reflow profile
4. **Nicla attachment:** Nicla connected via flex/headers — NOT reflow-soldered on top
5. **Panel depanelization:** V-score or mouse-bite breakaway

### 7.3 Firmware Flashing & QA

- **Bed-of-nails test fixture** with pogo pins on SWD (SWDIO, SWCLK, GND, VCC)
- **Flashing:** Compiled `.bin` or `.hex` (Mbed OS + SGC application merged) flashed via SWD using OpenOCD / pyOCD
- **QA routine per unit:**
  1. I²C scan — verify LDC1612 responds at 0x2A
  2. LDC1612 register read — confirm sensor alive
  3. SK6812 test pattern — chase sequence, verify all 5 LEDs illuminate
  4. Beeper test — short beep
  5. BLE advertising check — verify device broadcasts

### 7.4 Mechanical Assembly

1. **Shell prep:** PC shell cleaned, LED window area masked if needed
2. **Potting:** Electronics placed in shell cavity, 2-part silicone/epoxy dispensed, vacuum degassed, cured
3. **Quality check:** Visual inspection for bubbles/voids in potting
4. **Foam:** EVA padding die-cut, adhesive-backed, applied to inner shell face
5. **Straps:** Velcro straps riveted or looped through shell slots
6. **Final QC:** Waterproof test (IP67 pressure decay), LED visibility, BLE range, impact test (sample batch)
7. **Packaging:** Retail box with quick-start card, USB-C charging cable, spare strap set

---

## 8. Seasonal Inventory Strategy (Phase 3 Only)

> ⚠️ **This section applies ONLY after Phase 3 is reached.** Phases 0–2 use build-to-order / small batches with minimal inventory. Do NOT stockpile before demand is validated.

Ski product = **highly seasonal demand** (October–March peak).

```
Month    J  F  M  A  M  J  J  A  S  O  N  D
Sales    ████████████░░░░░░░░░░░░████████████
Prod.    ████████████████████████████████████
Stock    →→→→→→→→→→→→→→→BUILD←←←←←←←←←←←←
```

- **Produce year-round** at steady 200/month
- **Stockpile** during spring/summer → ~1,200 units by autumn
- **Deplete stock** during winter sales peak
- Benefits: smooths cash flow, avoids factory rush fees, ensures component availability

### Phase 2 Inventory (Small Batches)

During Phase 2, inventory is intentionally minimal:

- Produce in 50-unit batches triggered by actual orders
- Buffer: ~20-30 units for warranty replacements + demo units
- Total inventory never exceeds ~80 units at any time
- Re-order when buffer drops below 15 units

---

## 9. Cost Estimate by Phase

### Phase 3 (Steady State — 200/month)

**Electronics:**

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Nicla Sense ME | ~€65 | Wholesale pricing |
| Carrier PCB (fab) | ~€2-3 | Panelized, 4-layer |
| Components (BOM) | ~€8-12 | LDC1612, boost, shifter, LEDs, passives |
| EMS assembly | ~€5-8 | SMT + test fixture amortized |
| Battery (sub-zero) | ~€8-15 | Grepow/Ultralife |
| Flashing + QA (electronics) | ~€2-3 | Bed-of-nails, per-unit test |
| **Electronics subtotal** | **~€90-106** | |

**Mechanical:**

| Item | Unit Cost | Notes |
|------|-----------|-------|
| PC shell (injection molded) | ~€3-6 | Multi-cavity tooling amortized over 5k+ units |
| EVA foam padding (die-cut) | ~€1-2 | Closed-cell, custom shape, adhesive-backed |
| Velcro straps (pair) | ~€2-4 | Silicone-coated elastic + Velcro, custom length |
| Potting compound (2-part silicone) | ~€3-5 | Production drums, ~15-25ml per unit |
| Mechanical assembly labor | ~€4-8 | Potting, foam, straps, QC |
| Packaging (retail box) | ~€2-3 | Cardboard + insert, quick-start card, cable |
| **Mechanical subtotal** | **~€15-28** | |

| **TOTAL COGS (Phase 3)** | **~€105-134** | |

### Phase 2 (First Season — 50/batch)

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Nicla Sense ME | ~€65-70 | Smaller order qty |
| Carrier PCB (fab) | ~€5-8 | Small batch |
| Components (BOM) | ~€10-15 | Lower qty pricing |
| EMS assembly | ~€8-12 | Smaller batches |
| Battery (sub-zero) | ~€10-18 | Lower qty |
| Flashing + QA (electronics) | ~€3-5 | Semi-automated |
| PC shell (injection molded, 2-part) | ~€8-15 | Simpler tooling, lower amortization |
| EVA foam + straps | ~€4-8 | Die-cut, custom |
| Potting + mechanical assembly | ~€8-15 | Automated dispensing, manual assembly |
| Packaging | ~€3-5 | Basic retail |
| **TOTAL COGS (Phase 2)** | **~€124-181** | |

### Phase 1 (Pilot — 20-50 units)

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Nicla Sense ME | ~€70-75 | Retail-ish pricing |
| Carrier PCB (fab) | ~€8-15 | Proto fab or very small batch |
| Components (BOM) | ~€12-20 | Low qty, possible MOQ waste |
| Assembly (manual/EMS small) | ~€15-25 | |
| Battery (sub-zero) | ~€12-20 | Low qty |
| Flashing + QA | ~€5-10 | Manual flashing |
| PC shell (vacuum-formed) | ~€10-20 | Small-run, CNC trimmed |
| EVA foam + straps | ~€5-12 | Hand-cut foam, off-shelf straps |
| Potting + mechanical assembly | ~€10-20 | Manual epoxy pour |
| Packaging | ~€3-5 | Basic |
| **TOTAL COGS (Phase 1)** | **~€150-222** | |

### Phase 0 (Prototype — 5-10 units)

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Already have PCBs | ~€0 | Existing custom PCB stock |
| Components (BOM) | ~€15-25 | Already sourced for dev |
| Assembly | ~€0 | Manual in-house (time cost only) |
| Battery | ~€15-25 | Few units |
| Shell (3D printed / vacuum-formed) | ~€15-40 | Proto enclosure |
| Foam + straps | ~€5-10 | Hand-cut, off-shelf |
| Potting | ~€5-10 | Manual |
| **Total per unit** | **~€55-110** | Mostly already sunk |
| **Total P0 outlay** | **~€550-1,100** | Incremental for 10 units |

---

## 10. Selling Price & Margin

### Market Positioning

SGC occupies a unique space:

| Category | Comparable | Price Range |
|----------|-----------|-------------|
| Forearm guard (basic) | POC, Komperdell, LEKI | €30-80 |
| Ski training tech | Carv (pressure inserts) | €200-300 |
| Sports watch | Garmin Fenix, Polar Vantage | €400-900 |
| Professional timing | Swiss Timing, Brower | €5,000-50,000+ |
| **SGC** | **Protection + personal timing** | **€249-349** |

SGC replaces the forearm guard AND provides timing that previously required 
thousands in professional equipment or imprecise manual stopwatches.

### Recommended Retail Prices

| Phase | Consumer (B2C) | Club/Bulk (B2B, 5+) | Rationale |
|-------|---------------|---------------------|-----------|
| P1 (Pilot) | €199 | €169 | Introductory pricing, early adopter feedback |
| P2 (First Season) | €249 | €199 | Proven value, still building reputation |
| P3 (Steady State) | €299 | €249 | Established product, full feature set |
| P3 Pro (v2, +UWB) | €349 | €299 | Professional features (pinpoint timing) |

### Gross Margin

| Phase | COGS | B2C Price | Gross Margin | B2B Price | Gross Margin |
|-------|------|-----------|-------------|-----------|-------------|
| P1 (worst case) | ~€222 | €199 | **-10%** ⚠️ | €169 | -24% ⚠️ |
| P1 (best case) | ~€150 | €199 | **25%** | €169 | 11% |
| P2 (mid case) | ~€150 | €249 | **40%** | €199 | 25% |
| P3 (mid case) | ~€120 | €299 | **60%** | €249 | 52% |
| P3 Pro (mid case) | ~€140 | €349 | **60%** | €299 | 53% |

> ⚠️ **P1 is not profitable at B2B pricing.** P1 units should be sold B2C only,
> or treated as marketing investment (free/discounted to key influencers).
> Profitability starts at P2 with real volumes.

### Revenue Scenarios (per season)

| Scenario | Phase | Units | Avg Price | Revenue | COGS | Gross Profit |
|----------|-------|-------|-----------|---------|------|-------------|
| Pilot only | P1 | 30 | €199 | €5,970 | €5,000 | €970 |
| First season | P2 | 150 | €230 (mix) | €34,500 | €22,500 | €12,000 |
| Steady state | P3 | 600 (3mo peak) | €270 (mix) | €162,000 | €72,000 | €90,000 |

### Price Ceiling Factors

- **Competition:** No direct competitor for "forearm guard + personal timing" — unique niche gives pricing power
- **Perceived value:** A ski racer already spends €50-80 on a forearm guard. Adding personal timing (normally inaccessible below €1,000+) justifies the premium
- **Club budgets:** Ski clubs are price-sensitive but invest in performance tools. €249 B2B for a device that lasts multiple seasons is a coach-level decision, not board-level
- **Warranty cost:** 2-year warranty at <5% return rate = ~€6-7/unit reserve
- **Swiss made / Alps engineering:** Story matters. "Engineered in the Swiss Alps, tested by Masters racers" supports premium pricing

---

## 11. Key References

- `sgc_architecture_hardware.md` — Hardware pin map, block diagram, sensor specs
- `sgc_pcb_layout.md` — PCB layout rules for v1 custom replica (reference for companion carrier layout)
- `sgc_bom.md` — Full SGC bill of materials
