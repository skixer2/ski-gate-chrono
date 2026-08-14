# SGC — Production Strategy (v1.6 — ANNA-B412 module)

*2026-08-14 — v1.6: Production MD1 BLE module swapped ANNA-B112 (nRF52832) → ANNA-B412 (u-blox nRF52833, integrated antenna) on the custom replica PCB. Off-the-shelf Nicla (P0–P2) unchanged. See `anna_b412_migration.md`.*

*2026-08-12 — v1.5: Arming changed from magnetic reed switch (rejected — magnet de-calibrates BMM150) to a Langir 16 mm piezo pushbutton on P0.02. BOM + cost tables updated (button ~€7–12 vs reed ~€0.80).*
*2026-08-11 — v1.4: AD-017 pole-mount pivot. Simplified mechanical — no injection molding, potting, foam padding, or strap assembly. Forearm guard deferred to v2. Cost estimates updated for pole-mount form factor.*
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
| Hardware | Existing custom PCB (ANNA-B412, Nicla replica) |
| Assembly | Manual / in-house |
| Cost per unit | ~€100–150 (manual labor, one-off parts) |
| Total investment | ~€500–1,500 |

**Goal:** Field validation. Put devices on real skiers, coaches, race officials.
- Does the proximity arming work on snow?
- Is BLE range sufficient at race speeds?
- Is battery life acceptable in -10°C?
- ~~Does the beeper work through a helmet + wind noise?~~ → beeper DNP (v4.2, footprint retained)
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
│  │  nRF52832        │    │  │ Button pad (P0.02) │  │  │
│  │  BHI260AP        │    │  │ 5V Boost SOT-23    │  │  │
│  │  BMP390          │    │  │ Level Shifter      │  │  │
│  │  BQ25120A        │    │  │ SK6812 ×5 header   │  │  │
│  │  MX25R6435F      │    │  │ Battery JST conn   │  │  │
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

## 5. Mechanical Design — Pole Mount (v1 Primary)

The v1 device clips to the upper ski pole shaft, just below the handgrip.
Electronics face the back (trailing side), shielded from gate impacts by the pole shaft itself.
No potting, no foam padding, no injection-molded shell — just a small sealed enclosure + strap.

### 5.1 Product Identity

```
┌─────────────────────────────────────────────────────┐
│              SGC POLE TIMER                          │
│                                                      │
│   Personal timing for every ski racer                │
│                                                      │
│   Clips to your ski pole, below the grip             │
│   Measures split/run times via BLE to phone          │
│   Dual devices (one per pole) for full timing        │
│   Zero course infrastructure needed                  │
└─────────────────────────────────────────────────────┘
```

### 5.2 Enclosure Construction

Two identical injection-molded or 3D-printed ABS/PC halves with an O-ring groove around the perimeter.
Screws sit outside the O-ring seal → IP67 maintained without potting.

```
  TOP HALF (faces athlete / upward)
  ┌──────────────────────────┐
  │  ┌────────────────────┐  │
  │  │  SK6812 LEDs ×5    │  │  ← Visible through translucent wall
  │  │  (chase animation)  │  │
  │  └────────────────────┘  │
  │  ┌────────────────────┐  │
  │  │  Piezo Button       │  │  ← Top end, outward face
  │  └────────────────────┘  │
  ├──────────────────────────┤  ← O-ring groove (seal)
  │  ┌────────────────────┐  │
  │  │  PCB + Battery      │  │  ← Internal cavity
  │  │  Beeper (DNP)       │  │
  │  └────────────────────┘  │
  └──────────────────────────┘
  BOTTOM HALF (faces pole shaft)
```

### 5.3 Pole Mounting

- **Method:** Silicone-coated elastic strap with quick-release buckle, wrapping around the pole shaft AND the device body
- **One size fits all:** Strap tension accommodates standard pole diameters (16–18 mm)
- **Non-slip:** Silicone coating grips the pole; no clamp mechanism needed for a ~50g device
- **Quick removal:** Unbuckle for charging, re-attach in seconds

### 5.4 Emergency Reset & Power Cut (zero magnets)

The piezo button doubles as the reset input:
- **Factory reset:** 5 presses within 3 s → firmware performs factory reset (F42)
- **Hard power cut (emergency, firmware hung):** the battery is on a JST connector —
  open the enclosure (4 screws, outside the O-ring seal) and unplug/re-plug the battery
  to hard-reset a frozen device. No magnet, no second switch.

The battery is **replaceable** (JST connector, not potted or soldered) — deliberate for
the extreme conditions the device will see: cold-weather LiPo degradation makes a
user/service-swappable battery the right call.

### 5.5 Assembly Sequence (Pole Mount)

1. **PCB assembly** (EMS): Carrier PCB + components + Nicla mounted
2. **Firmware flash + QA** (bed-of-nails, SWD)
3. **Battery connect** → power-on self-test
4. **Enclosure assembly:** PCB+battery placed in bottom half → O-ring seated → top half screwed on (4 screws outside O-ring)
5. **Strap:** Elastic strap threaded through slots or wrapped externally
6. **Final QC:** Waterproof test (IP67 pressure decay), LED visibility, button activation, BLE range, BMM150 figure-8 cal
7. **Packaging:** Retail box with quick-start card, USB-C cable, spare strap

### 5.6 Enclosure Dimensions (target)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Length (along pole) | ~70 mm | PCB + battery in series |
| Width | ~25 mm | PCB width + enclosure walls |
| Height (off pole) | ~25 mm | PCB + battery + enclosure walls |
| Weight | ≤ 50 g | Electronics + enclosure + strap |

### 5.7 Production Phasing for Mechanical (Pole Mount)

| Phase | Shell | Strap | Assembly |
|-------|-------|-------|----------|
| P0 (prototype) | 3D-printed ABS/PC, 2 parts + O-ring | Off-shelf silicone strap with buckle | Manual, 4 screws |
| P1 (pilot) | 3D-printed or small-run molded | Custom elastic with silicone print | Manual, jig-assisted |
| P2 (first season) | Injection-molded ABS, single-cavity tooling (~€3–5k) | Custom strap, bulk order | Jig + pneumatic screwdriver |
| P3 (steady state) | Injection-molded, multi-cavity | Overmolded strap anchors | Semi-automated assembly |

---

## 5bis. Mechanical Design — Forearm Guard (⚠️ v2 / Future Development)

*Preserved from v1.3. The forearm guard form factor is deferred to v2 as a premium product variant
reusing the same electronics module. Adds LDC1612 cross-arm proximity arming, 200g shock rating,
injection-molded PC shell with potting, and EVA foam padding.
See §5 for the v1 pole-mount mechanical design.*

---

## 6. Companion Carrier PCB — Bill of Materials (v1 Pole Mount)

| Ref | Component | Package | Qty | Purpose |
|-----|-----------|---------|-----|---------|
| U_QI | IP6833 Qi Receiver (⚠️ DROPPED) | QFN-28 4×4mm | 0 | Removed — replaced by USB-C charging |
| L_QI | Qi receiver coil 24 µH (⚠️ DROPPED) | WPC A11 48×32mm | 0 | Removed — replaced by USB-C charging |
| C_AC1/2 | 22 nF, 50V, NP0/C0G (⚠️ DROPPED) | 0603 | 0 | Qi resonant caps — removed |
| C_RECT | 22 µF, 16V, X5R (⚠️ DROPPED) | 0805 | 0 | Rectifier filter — removed |
| C_BOOT | 100 nF, 16V, X5R (⚠️ DROPPED) | 0603 | 0 | Bootstrap cap — removed |
| SW1 | Langir 16mm piezo button | Ø16mm panel | 1 | Arming (v1). On P0.02, edge interrupt, NO pulse 20–1000 ms |
| U1 | LDC1612 (⚠️ v2 only, unpopulated) | WSON-12 | (0) | Forearm guard arming. Footprint retained, no IC loaded in v1 |
| L1 | PCB trace coil (⚠️ v2 only) | 14mm spiral | (0) | LDC1612 sensing coil. Not routed in v1 pole-mount PCB |
| U2 | MT3608 Boost Converter | SOT-23-6 | 1 | 5V rail for SK6812 LEDs |
| L_Boost | 4.7 µH power inductor, Isat ≥ 2A | 0805/1206 | 1 | Boost inductor |
| C_IN | 10 µF, 10V, X5R | 0805 | 1 | Boost input decoupling |
| C_OUT | 22 µF, 10V, X5R | 0805 | 1 | Boost output filter |
| R_FB1 | 75 kΩ, 1% | 0603 | 1 | Boost feedback divider high-side |
| R_FB2 | 10 kΩ, 1% | 0603 | 1 | Boost feedback divider low-side |
| U3 | Level Shifter (74AHCT1G125) | SOT-23-5 | 1 | 3.3V→5V data line shift |
| D1-D5 | SK6812-mini RGBW | 2×2mm | 5 | Status indicator LEDs |
| BZ1 | Piezo beeper (⚠️ DNP) | SMD | 0 | Footprint retained; not populated (v4.2) |
| J1 | Battery connector | JST-PH 2-pin | 1 | LiPo battery input |
| J_USB | USB-C receptacle (GCT USB4085-GF-A) | mid-mount SMD | 1 | USB-C charging (5V → BQ25120 IN) |
| R_CC1/2 | 5.1 kΩ (1%) | 0402 | 2 | USB-C CC sink pull-downs |
| C_USB | 10 µF, 10V, X5R | 0805 | 1 | USB VBUS bulk cap |
| J2 | Nicla interconnect | 0.1" header/flex | 1 | Connection to Nicla |
| R1-R4 | 2.2kΩ pull-up × 2, 10kΩ × 2 | 0603 | 4 | I²C pull-ups, misc |
| C1-C4 | 100nF, 10µF decoupling | 0603 | 4 | Power decoupling |

**v1 BOM delta vs forearm guard:** LDC1612 and its PCB coil removed → saves ~€3-5 in components.
Piezo button (Langir 16mm) added → ~€7-12 per unit (single qty), ~€6-7 @ 1000+.

---

## 7. Production Flow (Pole Mount — Phase 3)

### 7.1 Sourcing

| Item | Source | Notes |
|------|--------|-------|
| Nicla Sense ME | Arrow / DigiKey / OKdo | Wholesale batches of 200 |
| Carrier PCB | JLCPCB / PCBWay | Panelized, ENIG finish |
| Components | LCSC / Mouser | BOM parts for carrier PCB (no LDC1612 in v1) |
| Battery | Grepow / Ultralife | Sub-zero LiPo, -20°C rated |
| Enclosure (ABS/PC, 2 halves) | Local injection molder or 3D print service | Single-cavity tooling |
| O-ring | Standard supplier | Silicone, -40°C rated |
| Strap + buckle | Textile supplier | Silicone-coated elastic |
| Piezo button (Langir 16mm) | Langir / distributor | Ø16mm panel, ×1 per device |

### 7.2 Assembly (EMS Partner)

1. **Carrier PCB fabrication:** Panelized for pick-and-place
2. **SMT assembly:** Automated for all components (boost, shifter, SK6812, passives); button is panel-mounted, hand-wired at final assembly
3. **Reflow soldering:** Standard lead-free profile
4. **Nicla attachment:** Nicla connected via flex/headers
5. **Panel depanelization**

### 7.3 Firmware Flashing & QA

- **Bed-of-nails test fixture** with pogo pins on SWD + power
- **Flashing:** via SWD using OpenOCD / pyOCD
- **QA routine per unit:**
  1. Button test — press → GPIO P0.02 falling-edge pulse captured
  2. SK6812 test pattern — chase sequence, verify all 5 LEDs
  3. ~~Beeper test — short beep~~ (beeper DNP v4.2)
  4. BLE advertising check — verify device broadcasts
  5. BMM150 calibration check

### 7.4 Mechanical Assembly (Pole Mount)

1. **Enclosure prep:** Two ABS/PC halves cleaned
2. **PCB + battery placed** in bottom half
3. **O-ring seated** in groove
4. **Top half screwed on** (4 screws, outside O-ring seal)
5. **Strap threaded** through enclosure slots
6. **Final QC:** IP67 pressure decay test, LED visibility, BLE range
7. **Packaging:** Retail box with quick-start card, USB-C cable, spare strap

---

## 8. Seasonal Inventory Strategy (Phase 3 Only)

> ⚠️ **This section applies ONLY after Phase 3 is reached.** Phases 0–2 use build-to-order / small batches with minimal inventory.

Ski product = **highly seasonal demand** (October–March peak).

- **Produce year-round** at steady 200/month
- **Stockpile** during spring/summer → ~1,200 units by autumn
- **Deplete stock** during winter sales peak

---

## 9. Cost Estimate by Phase (Pole Mount v1)

### Phase 3 (Steady State — 200/month)

**Electronics:**

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Nicla Sense ME | ~€65 | Wholesale pricing |
| Carrier PCB (fab) | ~€2-3 | Panelized, 4-layer |
| Components (BOM, no LDC1612) | ~€5-9 | Boost, shifter, SK6812, passives |
| Piezo button (Langir 16mm) | ~€6-10 | Panel-mount, ×1 per device |
| EMS assembly | ~€5-8 | SMT + test fixture amortized |
| Battery (sub-zero) | ~€8-15 | Grepow/Ultralife |
| Flashing + QA (electronics) | ~€2-3 | Bed-of-nails, per-unit test |
| **Electronics subtotal** | **~€87-103** | |

**Mechanical (Pole Mount):**

| Item | Unit Cost | Notes |
|------|-----------|-------|
| ABS/PC enclosure (2 halves, inj. molded) | ~€1-3 | Single-cavity tooling ~€3-5k amortized over 5k+ |
| O-ring (silicone) | ~€0.20-0.50 | Standard size |
| Silicone strap + buckle | ~€1-2 | Custom length |
| Screws (4× stainless) | ~€0.10 | |
| Assembly labor (screw + strap + QC) | ~€2-4 | Simple, no potting |
| Packaging (retail box + USB-C cable) | ~€3-5 | Includes USB-C cable, spare strap |
| **Mechanical subtotal** | **~€7-15** | |

| **TOTAL COGS (Phase 3, pole mount)** | **~€94-118** | |

**Kit price (2 devices = 2 poles):** ~€188-236 COGS

### Phase 2 (First Season — 50/batch)

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Nicla Sense ME | ~€65-70 | Smaller order qty |
| Carrier PCB + components + assembly | ~€20-30 | Small batch |
| Battery | ~€10-18 | Lower qty |
| Flashing + QA | ~€3-5 | Semi-automated |
| Enclosure (3D-printed or small-run molded) | ~€3-8 | |
| Strap + assembly + packaging | ~€5-10 | |
| **TOTAL COGS (Phase 2)** | **~€106-141** | |

### Phase 1 (Pilot — 20-50 units)

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Nicla Sense ME | ~€70-75 | Retail-ish |
| Carrier PCB + components + manual assembly | ~€30-50 | Proto/low volume |
| Battery | ~€12-20 | Low qty |
| 3D-printed enclosure + O-ring | ~€5-15 | |
| Strap + assembly + packaging | ~€5-10 | |
| **TOTAL COGS (Phase 1)** | **~€122-170** | |

### Phase 0 (Prototype — 5-10 units)

| Item | Unit Cost | Notes |
|------|-----------|-------|
| Already have PCBs | ~€0 | Existing stock |
| Components + battery | ~€20-35 | Already sourced for dev |
| 3D-printed enclosure + O-ring | ~€5-10 | |
| Strap | ~€2-5 | |
| **Total per unit** | **~€27-50** | Mostly already sunk |
| **Total P0 outlay** | **~€270-500** | Incremental for 10 units |

---

## 10. Selling Price & Margin (Pole Mount v1)

### Market Positioning

SGC is a **personal ski timing device** — no course infrastructure, no professional timing crew.
Clips to the pole and times every run.

| Category | Comparable | Price Range |
|----------|-----------|-------------|
| Ski training tech | Carv (pressure inserts) | €200-300 |
| Sports watch | Garmin Fenix, Polar Vantage | €400-900 |
| Professional timing | Swiss Timing, Brower | €5,000-50,000+ |
| **SGC Pole Timer** | **Personal timing, zero infrastructure** | **€149-199** |

### Recommended Retail Prices

| Phase | Single Device (B2C) | 2-Device Kit (B2C) | Club/Bulk Kit (B2B, 5+) | Rationale |
|-------|--------------------|-------------------|------------------------|-----------|
| P1 (Pilot) | €99 | €169 | €139 | Introductory, early adopter |
| P2 (First Season) | €119 | €199 | €169 | Proven value |
| P3 (Steady State) | €149 | €249 | €199 | Established product |
| P3 Forearm Guard (v2) | — | €299 | €249 | Premium protection variant |

### Gross Margin (Kit)

| Phase | COGS/Kit | B2C Price | Gross Margin | B2B Price | Gross Margin |
|-------|----------|-----------|-------------|-----------|-------------|
| P1 (worst case) | ~€340 | €169 | **-50%** ⚠️ | €139 | -59% ⚠️ |
| P1 (best case) | ~€244 | €169 | **-31%** ⚠️ | €139 | -43% ⚠️ |
| P2 (mid case) | ~€247 | €199 | **-19%** ⚠️ | €169 | -32% ⚠️ |
| P3 (mid case) | ~€212 | €249 | **15%** | €199 | -6% ⚠️ |

> ⚠️ **P1–P2 are NOT profitable.** Treated as market validation investment.
> Profitability starts at P3, and only if Nicla wholesale pricing and volume discounts materialize.
> Key profit driver: replace Nicla with custom ANNA-B412 (nRF52833) PCB at P3 → saves ~€50/device → COGS drops to ~€60-80/kit → margin 60%+.

### Revenue Scenarios (per season)

| Scenario | Phase | Kits | Avg Price | Revenue | COGS | Gross Profit |
|----------|-------|------|-----------|---------|------|-------------|
| Pilot only | P1 | 15 | €169 | €2,535 | €4,000 | -€1,465 |
| First season | P2 | 75 | €190 | €14,250 | €18,500 | -€4,250 |
| Steady state | P3 | 300 | €235 | €70,500 | €63,600 | €6,900 |

> **Note:** These numbers assume off-the-shelf Nicla at ~€65 wholesale. True profitability requires
a custom ANNA-B412 (nRF52833) PCB (no Nicla) — achievable at P3+ but not earlier due to MOQ constraints.
See §4 Architecture for the Nicla → custom PCB migration path.

---

## 11. Key References

- `sgc_architecture_hardware.md` — Hardware pin map, block diagram, sensor specs
- `sgc_pcb_layout.md` — PCB layout rules for v1 custom replica (reference for companion carrier layout)
- `sgc_bom.md` — Full SGC bill of materials
