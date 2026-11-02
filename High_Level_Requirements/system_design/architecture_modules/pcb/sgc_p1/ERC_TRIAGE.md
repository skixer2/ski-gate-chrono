# SGC P1 — ERC Triage (pre-revA gate)

**Data:** `erc_now.rpt` + `hier6.net`, kicad-cli (KiCad 10.0) vs JP's current file (local canon, 2026-11-02 22:53).
**Totals: 347 = 15 errors + 332 warnings. ZERO wiring errors found** — netlist membership proves every bus lands per spec (QSPI U2↔U8 is the BHI260AP hub's own FW flash, Nicla-parity; M1_CSB1 U8.38→U9.6 drives the BMP390).

## The 15 errors — all pin-typing / conventions, none electrical

| # | Item | Root cause | Proposed fix |
|---|------|-----------|--------------|
| 1 | `#PWR05` GND not driven @(24.13,151.13) | No `power_out` GND pin exists anywhere (normal) | Accept (or severity→warning) |
| 2 | `#PWR01` VBUS_USB not driven @(34.29,25.40) | J1 VBUS pin typed `passive` | Type J1 VBUS `power_out` (it is the source) |
| 3 | `#PWR03` VBAT not driven @(285.75,54.29) | J2 battery pin typed `passive` | Type J2 VBAT `power_out` |
| 4 | `#PWR93` +5V not driven @(96.15,53.98) (child) | MT3608 VOUT typed `passive` | Type MT3608 VOUT `power_out` (DNP cluster anyway) |
| 5 | U2.1/3/5/6/7 (CS#/WP/SI/SCLK/HOLD) not driven | U8's QSPI host pins typed `input/passive` | Type U8 QSPI host pins `output`/`bidirectional` |
| 6 | U8.38 MCSB1 not driven | M1-master CS typed `input` | Type `output` (it drives U9.6 CSB) |
| 7 | U8.18 MCSB2 not driven | Reserved 2nd M1 slave CS, net has 1 pin | Accept + text-box note "reserved" |
| 8 | J3.4 SWDCLK / J3.10 NRESET not driven | Driven by external debugger (SWD header) | Accept — universal for Cortex debug headers |
| 9 | U3.A3+B3 PMID power_out×2 | Two DSBGA bumps of ONE internal node | Type B3 `passive` (duplicate-bump convention) |
| 10 | U7.2 SO + U8.32 HSDO output×2 | Shared MISO: SPI2_MISO = U1.24+U7.2+U8.32 | Type slave SO/HSDO `bidirectional` (Hi-Z reality) |

## The 332 warnings — 4 buckets

1. **275 endpoint_off_grid** — generator mm-precision + manual edits. *Fix:* programmatic snap with **netlist-identity proof** (netlist before == after, byte-identical membership) right before revA snapshot — not mid-polish.
2. **41 lib_symbol_mismatch** — our deliberately-embedded copies vs global libs (embedding is the headless-generation doctrine). *Fix:* severity→ignore in project ERC settings; embedded copies ARE canon.
3. **13 isolated_pin_label** — expected on reserved/DNP nets (M1_CSB2, LED-reserve globals BOOST_EN/SK6812_DATA…). *Fix:* list-check + annotate; no rewire expected.
4. **3 footprint_link_issues** — REAL pre-layout fixes:
   - `Package_SO:SOIC-8_5.23x5.23mm_P1.27mm` not found (×2 — U2/U7 flash; MX25R1635FZUIH=USON8, MX25R6435FZAIL=SOP8-208mil → correct fp names needed)
   - `Connector_JST:JST_SH_BM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal` not found (J4) → KiCad-10 lib renamed/variants

## ERC severity proposals (project settings)
- lib_symbol_mismatch → ignore (embedded = canon)
- endpoint_off_grid → warning stays, bulk-snap before revA
- pin_to_pin warnings on shared buses → resolved by retyping, not suppression

## Sequencing rule
All file edits = **pure text surgery on JP's committed canon snapshot** (kiutils read-only doctrine).
JP's current local mods (sgc_p1.kicad_sch/.pro) must be committed FIRST; KiCad closed during surgery; verify = kicad-cli netlist diff (identity) + ERC re-run (expect: errors → ~0, off_grid untouched).

*Tools: erchist / ercraw / ercdriven / ercpwr / netq (pcb/sgc_p1/). Data: zio_claw\sgc_bench\erc_now.rpt, hier6.net.*
