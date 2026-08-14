# SGC — PCB Layout Guide (v1 Pole-Mount)

*2026-08-14 — MD1 module = ANNA-B412 (u-blox nRF52833, integrated antenna). Land pattern must be verified against the u-blox ANNA-B412 datasheet before layout. See `anna_b412_migration.md`.*

*2026-08-12 — Companion to `sgc_pcb.kicad_pro`, `symbols/sgc_pcb_symbols.kicad_sym`, and
`sgc_pcb_netlist.md`. This is the step-by-step recipe to turn the SGC netlist into a
fabrication-ready 4-layer board in **KiCad 8**.*

**Freeware tool: [KiCad 8](https://www.kicad.org/download/)** — open source, free for
commercial use, industry-standard for small-batch/EMS work. It has the full flow:
schematic capture → footprint assignment → PCB layout → DRC → Gerber/BOM/CPL export.
JLCPCB, PCBWay, Aisler and every SMD assembler accept KiCad output directly.

---

## 0. Arming Button (v1)

The v1 pole-mount device arms via a **piezoelectric pushbutton** — a button with no
moving parts and no magnet (the magnetic reed switch was rejected because its magnet
de-calibrates the BMM150 magnetometer inside the BHI260AP; the LDC1612 was rejected
because its 14 mm coil footprint + the aluminium pole shaft as a conductive target are
impractical for pole-mount).

### Selected part

| Property | Value |
|----------|-------|
| **Part** | **Langir 16 mm piezoelectric pushbutton, momentary NO** |
| Interface | **GPIO P0.02**, internal pull-up, **falling-edge interrupt** |
| Output | NO pulse 20–1000 ms (piezo — no sustained hold) |
| Sealing | IP68 / IP69K (nut + O-ring) |
| Face | Ø16 mm stainless steel (316L), flush or raised |
| Force / Life / Temp | ≤3 N / 50 M cycles / −40…+85 °C |
| Cost | ~$12 single / ~$9 @100 / ~$7 @1000+ |

**Firmware note:** a piezo emits a *pulse*, not a held level. P0.02 must be an **edge
interrupt** (a 10 Hz poll will miss the 20 ms pulse). Arming = single press; factory
reset = 5 presses within 3 s.

### Placement rule

Mount the button in a **Ø16 mm panel cutout** on the **outward or top face** of the
enclosure, raised 3–5 mm so the back of a glove finds it blind. The button is a
self-sealed unit — no moving part penetrates the wall, so IP67 is preserved without a
rubber boot. Keep the button's two wires short (< 15 mm) and away from the boost
inductor and the Qi coil (switching noise on the input could false-trigger the ISR).

> **Layout note:** the button is panel-mounted (not SMD on the PCB). Footprint it as two
> plated pads for the fly leads (or a 2-pin header) near the top edge, plus the Ø16 mm
> cutout in the enclosure CAD — not in the PCB. The PCB only carries the P0.02 pull-up
> (R9 = 100 kΩ) and the pad for the button's switch output.

---

## 1. Board Stackup & Setup

| Parameter | Value |
|-----------|-------|
| Layers | **4** (Signal / GND / PWR / Signal) |
| Thickness | **0.8 mm** (thin for wearable) |
| Finish | **ENIG** (mandatory — BHI260AP LGA pads won't solder on HASL) |
| Copper | 1 oz (35 µm) outer, 0.5 oz inner |
| Min trace/space | 0.1 mm / 0.1 mm |
| Min via | 0.3 mm drill / 0.6 mm pad |
| Dimensions | **22 × 55 mm** |

**KiCad setup:** *Board Setup → Board Stackup → Physical Stackup* — set 4 layers,
0.8 mm total, layer order `F.Cu / In1.Cu / In2.Cu / B.Cu`. Name them `Signal / GND / Power / Signal`.
Set design rules: clearance 0.1 mm, track width 0.15 mm default, via 0.6/0.3.

---

## 2. Component Placement (Zones)

Work top-to-bottom along the 55 mm axis. The pole-mount enclosure is **~25 × 25 × 70 mm**,
device mounted on the **back** of the pole (trailing side). Board long axis runs **parallel
to the pole**.

```
TOP EDGE (toward grip)
┌─────────────────────────────────────────────┐
│  BUTTON PADS  │  SK6812 ×5  │ (level shifter)│  ← top, nearest grip
├─────────────────────────────────────────────┤
│  BHI260AP (U5)            │  BMP390 (U6)     │  ← sensors, center
│  + 32k XTAL              │                  │
├─────────────────────────────────────────────┤
│  ANNA-B412 (MD1)         │  MX25R1635F (U7) │  ← MCU + flash, center-bottom
│  + BLE antenna edge      │                  │
├─────────────────────────────────────────────┤
│  BQ25120A (U9) │  MT3608 (U4) │ IP6833 (U11)│  ← power, bottom
│  battery JST   │  boost L     │  Qi coil     │
└─────────────────────────────────────────────┘
BOTTOM EDGE (Qi coil faces athlete, away from pole)
```

### Placement rules

1. **ANNA-B412 antenna** → place module so the BLE antenna edge faces **outward/upward**
   away from copper. Keep a **> 5 mm copper keepout** under and around the antenna.
   No components in the antenna near-field.
2. **BHI260AP** → center, away from Qi coil (> 10 mm) and boost inductor (> 5 mm).
   It contains the BMM150 magnetometer (magnetic-keepout component).
3. **32.768 kHz crystal** → directly adjacent to MD1 pins 17/18, load caps 12 pF each,
   guard ring to GND.
4. **SK6812 ×5** → single row along top edge, **~3 mm pitch**, DIN chain left→right.
   VDD = 5 V rail via level shifter.
5. **Button pads** → top edge, adjacent to the LED strip (button mounted on the outward
   face directly above), per §0.
6. **MT3608 boost** → near the LED strip (short 5 V trace). Inductor < 5 mm from SW pin.
7. **IP6833 (Qi)** → bottom edge, near Qi coil AC terminals (< 10 mm). Coil on **B.Cu**.
8. **BQ25120A** → near battery JST and Qi 5 V output.

---

## 3. Routing by Net Class

Assign these net classes (already defined in the `.kicad_pro`):

| Net class | Width | Clearance | Used for |
|-----------|-------|-----------|----------|
| Default | 0.15 mm | 0.1 mm | Signals |
| Power | 0.3 mm | 0.15 mm | VBAT, 3V3, 1V8, 5V, GND stubs |
| RF | 0.25 mm | 0.1 mm | SPI clocks, LED data, BLE (v2 RF) |

### Power tree (route first)

```
Qi coil → IP6833 → V_QI_5V → BQ25120A IN → BAT+ → J1 (battery)
                     │
BQ25120A → VBAT ──┬── 3.3V LDO → VCC_3V3  (nRF52, flash, beeper, I2C pull-ups)
                  ├── 1.8V LDO → VDD_1V8  (BHI260AP, BMP390, LDC1612 v2)
                  └── MT3608 → VDD_5V     (SK6812 ×5, level shifter VCC)
```

- **GND plane (In1.Cu):** solid, unsplit. Via-stitch around the boost and under the Qi coil.
- **PWR plane (In2.Cu):** split into 3V3 / 1V8 / 5V / VBAT regions.
- LDO output caps **< 3 mm** from the LDO pin. Boost input/output caps **< 3 mm** from MT3608.

### Critical nets

| Net | Rule |
|-----|------|
| BHI_SPI_SCK/MOSI/MISO | short (< 25 mm), length-matched ± 2 mm, ground between |
| FLASH_CS, BHI_CS | 10k pull-ups, no stubs |
| LED_DIN (P0.19 → 74AHCT1G125 → D1) | < 20 mm, keep away from boost SW node |
| BOOST_SW (MT3608 pin 1 → L_Boost) | **shortest net on the board**, fat trace, no vias |
| Qi AC1/AC2 | matched length, 0.5 mm width, differential pair, resonant caps < 2 mm from IP6833 |
| I2C0/I2C1 (SDA/SCL) | 2.2k pull-ups near MCU, < 30 mm |
| BEEPER | 100 Ω series, < 30 mm |
| Button → P0.02 | < 15 mm, 100k pull-up, keep away from boost/Qi |

---

## 4. Keepout Zones (magnetic + RF)

| Component | Keepout from | Distance | Reason |
|-----------|-------------|----------|--------|
| BMM150 (in BHI260AP) | Qi coil ferrite | **> 10 mm** | magnetic interference |
| BMM150 | boost inductor (4.7 µH) | **> 5 mm** | stray field |
| Button input (P0.02) | boost SW node, Qi coil | **> 10 mm** | noise false-trigger |
| ANNA-B412 antenna | any copper/component | **> 5 mm** | BLE range |
| Qi coil | ground plane under it | no copper | eddy-current loss |
| LDC1612 coil (v2) | copper planes | > 5 mm | eddy currents |

---

## 5. DRC & ERC

1. **ERC (schematic):** run, clear all errors. Power pins must be driven; no dangling labels.
2. **Assign footprints** to every symbol (the schematic generator already set the common ones).
3. **DRC (PCB):** set the rules from §1, run, clear all errors/warnings.
4. Check **courtyard overlap** = 0 (BHI260AP LGA is tight — verify paste mask).
5. Verify **unconnected items** = 0 before generating outputs.

---

## 6. Manufacturing Output (from KiCad)

| File | KiCad menu | Used for |
|------|-----------|----------|
| Gerber (all layers) | *Fabrication Outputs → Gerbers* | PCB fab (JLCPCB/PCBWay) |
| Drill file | *Fabrication Outputs → Drill Files* | hole drilling |
| BOM (.csv) | *Fabrication Outputs → BOM* | part ordering |
| CPL (.csv) | *Fabrication Outputs → Component Placement* | pick-and-place (EMS) |
| 3D STEP | *Export → STEP* | enclosure CAD fit check |

**JLCPCB notes:** choose ENIG finish, 4-layer 0.8 mm, 1 oz outer. Upload Gerber + drill +
BOM + CPL for SMT assembly. Flag the ANNA-B412, BHI260AP, BQ25120A, and IP6833 as
*extended/library parts* if you want JLCPCB to source them (else supply on reel).
The Langir button is panel-mounted — ordered separately, wired by hand at final assembly.

---

## 7. Button Physical Integration

```
        OUTWARD face (button + LED)
        ┌──────────────────────────┐
        │  [BUTTON]  ●●●●●  LEDs   │
        │                          │
        └──────────────────────────┘
              POLE SHAFT (back)
```

- Button in a **Ø16 mm cutout** on the outward face, raised 3–5 mm, sealed by its own
  nut + O-ring (IP69K) — no moving part through the wall.
- LED strip adjacent, facing outward/upward, so the athlete sees the arm confirmation
  (green chase) at the same spot they press.
- No cross-pole interaction — each device arms on its own button press.
- Enclosure wall material is unconstrained (no ferrous restriction now that the magnet
  is gone) — ABS or polycarbonate both fine.

---

## 8. Files in this directory

| File | Purpose |
|------|---------|
| `sgc_pcb.kicad_pro` | KiCad project (net classes, ERC/DRC rules, board defaults) |
| `sgc_pcb.kicad_sch` | Generated schematic (symbols + net labels) |
| `symbols/sgc_pcb_symbols.kicad_sym` | Custom symbol library (all SGC parts) |
| `generate_schematic.py` | Regenerates the schematic from the netlist data |
| `sgc_custom.kicad_sym` | Earlier symbols (SK6812, LDC coil, piezo) — merge if desired |
| `sgc_pcb_netlist.md` | Canonical pin-to-net wiring reference |

---

*Next: open in KiCad, finish pin-level wiring (connect-by-name via the global labels),
run ERC, then layout per §2–§4. Enclosure CAD from the final board outline.*
