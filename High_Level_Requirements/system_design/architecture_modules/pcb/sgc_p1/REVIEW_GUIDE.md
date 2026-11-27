# SGC P1 — Review Guide (JP, 2026-11-27)

## Files to review
- **Schematic**: open `sgc_p1.kicad_pro` in KiCad 8/10 → 7 sheets (root + power/mcu/sensors/storage/boost_led/connectors)
- **Layout**: open `sgc_p1.kicad_pcb` (same project)
- **Reference docs**: PLACEMENT_PLAN.md (placement v2.3), PLACEMENT_REVIEW.md (independent review), README.md (regeneration workflow)

## State
- Everything pushed to GitHub master at commit 64a1688+ (schematic validated, PCB semantic-gate 0 fails)
- Regeneration (if you edit the netlist): see README — preserve_layout.py keeps YOUR symbol moves

## Schematic — what to check
1. Power tree: VBAT → BQ25120A (U3) → +1V8 rail → ANNA/BHI/flash; MAX17048 (U4) gauge, VBAT sense
2. U4 pin 2 = VBAT (was the GND bug — fixed in the sheets)
3. USB-C (J1): charge-only (A4/A9 = VBUS, CC1/CC2, D+/D− deliberately NC)
4. SWD (J3): +1V8, SWDIO, SWDCLK, SWO, GND, RESET_N (pogo-jig programming)
5. SK6812 data: DIN level-shift option U11 TXS0102 (DNP, v2 LED reserve)
6. Sensor I2C (I2C0) + BHI QSPI/SPI domain at 1V8

## Layout — what to check
1. All 79 parts inside the 22×55 outline; battery band (bottom face y 4–47) stays clear
2. Known-intentional (do NOT flag):
   - **J6** (right edge, half-out pinholes): castellated strap anchor — by design
   - **J1** (bottom edge): USB-C shell flush with edge, tongue overhangs — top-mount convention; "PCB Edge" Dwgs.User marker sits ON the edge
   - **U1 F.Fab circle** (pokes past top edge): documentation marking, never manufactured
   - **J1 F.Cu outline lines**: shell overhang marking — fab clips them
   - **J4/J5**: DNP hookup footprints (J4 strip feed, J5 button/piezo — both TBD-JP)
3. Labels: on their parts; silk refs at 0.8 mm — tell me if you want them hidden/shrunk

## Open decisions (JP)
1. **USB data (A/B/C)**: (A) charge-only rev A, flash via SWD jig — current state; (B) route D+/D−/VBUS now (3 nets), rail flip to 3.3 V in rev B enables USB DFU flashing — zero respin; (C) full 3.3 V + USB now (power-tree rework)
2. **Silk refs**: keep (recommended) / hide / shrink
3. **Enclosure**: sensor-end vent (BME688), USB cutout + J6 strap-anchor access, board glued (2× NPTH jig holes retained)

## Known caveats
- J1 GCT USB4085: top-mount (Dip Type), shell flush with edge per GCT convention — verified against the official KiCad footprint
- U11 TXS0102 package = VSSOP-8 2.3×2.0 (DCT-style) — confirm when ordering
- J5 function = piezo/button hookup, marked TBD-JP in the netlist
