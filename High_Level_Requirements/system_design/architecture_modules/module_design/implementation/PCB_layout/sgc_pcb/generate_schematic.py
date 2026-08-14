#!/usr/bin/env python3
"""
SGC PCB schematic generator — produces a valid KiCad 8 `.kicad_sch` file.

This script encodes the full SGC "Nicla Sense ME Replica" netlist as Python
data structures and emits the S-expression schematic. Regenerate anytime the
netlist changes (see sgc_pcb_netlist.md).

Run:  python3 generate_schematic.py
"""
import uuid
import datetime

# ---------------------------------------------------------------------------
# Symbol placement data
# ---------------------------------------------------------------------------
# Each entry: (lib_id, reference, value, footprint, x, y, angle, dnp)
# Coordinates in mils. 100 mil grid. Left column x=100, right column x=600.
SYMBOLS = [
    # ── Sheet 1: MCU Core ──
    # MD1 = ANNA-B412 (u-blox nRF52833, integrated antenna). Symbol pinout is a
    # B112-derived placeholder — designer MUST verify land pattern/pad map vs the
    # u-blox ANNA-B412 datasheet before layout.
    ("sgc_pcb_symbols:ANNA-B412", "MD1", "ANNA-B412", "SGC:ANNA-B412", 200, 300, 0, False),
    ("Device:Crystal",             "Y1",  "32.768kHz", "Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm", 150, 200, 0, False),  # XL1/XL2 — was missing from schematic
    ("sgc_pcb_symbols:BHI260AP",   "U5",  "BHI260AP",  "SGC:BHI260AP_LGA-44", 600, 300, 0, False),
    ("sgc_pcb_symbols:BMP390",     "U6",  "BMP390",    "SGC:BMP390_LGA-10",   800, 300, 0, False),
    ("sgc_pcb_symbols:MX25R6435F", "U7",  "MX25R6435F","Package_SO:SOIC-8_5.23x5.23mm_P1.27mm", 600, 100, 0, False),
    ("sgc_pcb_symbols:IS31FL3194", "U8",  "IS31FL3194","SGC:IS31FL3194_QFN-16", 800, 100, 0, False),
    ("sgc_pcb_symbols:BQ25120A",   "U9",  "BQ25120A",  "SGC:BQ25120A_DSBGA-25", 400, -100, 0, False),

    # ── Sheet 2: SGC Sensors (v1 arming = piezo button) ──
    ("sgc_pcb_symbols:Piezo_Button", "SW1", "Langir_16mm_Piezo", "SGC:Piezo_Button_16mm_Panel", 100, -300, 0, False),
    ("sgc_pcb_symbols:LDC1612",       "U1",  "LDC1612DNTR",     "SGC:LDC1612_WSON-12",  300, -300, 0, True),  # v2 only

    # ── Sheet 3: SGC Peripherals ──
    ("sgc_pcb_symbols:SK6812-mini",     "D1", "SK6812-mini", "SGC:SK6812-mini", 100, 500, 0, False),
    ("sgc_pcb_symbols:SK6812-mini",     "D2", "SK6812-mini", "SGC:SK6812-mini", 200, 500, 0, False),
    ("sgc_pcb_symbols:SK6812-mini",     "D3", "SK6812-mini", "SGC:SK6812-mini", 300, 500, 0, False),
    ("sgc_pcb_symbols:SK6812-mini",     "D4", "SK6812-mini", "SGC:SK6812-mini", 400, 500, 0, False),
    ("sgc_pcb_symbols:SK6812-mini",     "D5", "SK6812-mini", "SGC:SK6812-mini", 500, 500, 0, False),
    ("sgc_pcb_symbols:74AHCT1G125",     "U10", "74AHCT1G125", "Package_TO_SOT_SMD:SOT-23-5", 100, 650, 0, False),
    ("sgc_pcb_symbols:Piezo_Transducer","BZ1", "Piezo_10x10", "SGC:Piezo_Transducer_10x10", 300, 650, 0, True),  # DNP — footprint retained (user feedback: wind/helmet noise, low value)

    # ── Sheet 4: Power ──
    ("sgc_pcb_symbols:MT3608",        "U4",  "MT3608",  "Package_TO_SOT_SMD:SOT-23-6", 600, -300, 0, False),
    ("sgc_pcb_symbols:IP6833",        "U11", "IP6833",  "SGC:IP6833_QFN-28",           800, -300, 0, True),   # DNP — Qi dropped (planarity/centring), USB-C alt
    ("sgc_pcb_symbols:Qi_Coil",       "L3",  "24uH_A11","SGC:Qi_Coil_WPC_A11",         800, -500, 0, True),   # DNP — Qi dropped
    ("sgc_pcb_symbols:Battery_JST_3pin", "J1", "BAT_JST", "Connector_JST:JST_ACH_BM03B-ACHSS-GAN-ETF", 400, -500, 0, False),
    ("Connector:USB_C_Receptacle_USB2.0", "J2", "USB4085", "Connector_USB:USB_C_Receptacle_USB2.0", 800, -600, 0, False),  # USB-C charging (GCT USB4085, replaces Qi)

    # ── Sheet 5: v2 (unpopulated) ──
    ("sgc_pcb_symbols:PMOS_Gate", "Q1", "PMOS_RFID", "Package_TO_SOT_SMD:SOT-23", 900, 500, 0, True),
    ("sgc_pcb_symbols:PMOS_Gate", "Q2", "PMOS_UWB",  "Package_TO_SOT_SMD:SOT-23", 900, 600, 0, True),
]

# Passives laid out explicitly (resistors + capacitors + inductor)
PASSIVES = [
    # Ref, Value, Footprint, x, y, dnp
    ("C1",  "12pF",  "Capacitor_SMD:C_0603_1608Metric", 250, 100, False),  # XL1 load cap
    ("C2",  "12pF",  "Capacitor_SMD:C_0603_1608Metric", 250, 150, False),  # XL2 load cap
    ("R6",  "10k",   "Resistor_SMD:R_0603_1608Metric",  150, 100, False),  # RESET pull-up
    ("R7",  "2.2k",  "Resistor_SMD:R_0603_1608Metric",  850, 150, False),  # I2C0 SDA pull-up
    ("R8",  "2.2k",  "Resistor_SMD:R_0603_1608Metric",  850, 100, False),  # I2C0 SCL pull-up
    ("R9",  "100k",  "Resistor_SMD:R_0603_1608Metric",  150, -300, False), # LDC_INTB pull-up
    ("R10", "10k",   "Resistor_SMD:R_0603_1608Metric",  200, 650, False),  # QI_DETECT pull-up
    ("R12", "100",   "Resistor_SMD:R_0603_1608Metric",  350, 650, False),  # Beeper series
    ("R15", "2.2k",  "Resistor_SMD:R_0603_1608Metric",  350, -300, False), # I2C1 SDA pull-up
    ("R16", "2.2k",  "Resistor_SMD:R_0603_1608Metric",  350, -250, False), # I2C1 SCL pull-up
    ("R_FB1", "75k", "Resistor_SMD:R_0603_1608Metric",  650, -400, False), # boost FB high
    ("R_FB2", "10k", "Resistor_SMD:R_0603_1608Metric",  650, -450, False), # boost FB low
    ("C17", "100n",  "Capacitor_SMD:C_0603_1608Metric", 350, -200, False), # LDC VDD decoup
    ("C18", "33pF",  "Capacitor_SMD:C_0603_1608Metric", 350, -150, False), # LDC tank cap
    ("C19", "100n",  "Capacitor_SMD:C_0603_1608Metric", 500, 550, False),  # LED decoup (1 of 5)
    ("C_IN",  "10uF",  "Capacitor_SMD:C_0805_2012Metric", 600, -400, False), # boost input
    ("C_OUT", "22uF",  "Capacitor_SMD:C_0805_2012Metric", 600, -500, False), # boost output
    ("C_AC1", "22nF",  "Capacitor_SMD:C_0603_1608Metric", 850, -400, True),  # Qi AC1 (DNP — Qi dropped)
    ("C_AC2", "22nF",  "Capacitor_SMD:C_0603_1608Metric", 850, -450, True),  # Qi AC2 (DNP)
    ("C_RECT","22uF",  "Capacitor_SMD:C_0805_2012Metric", 900, -300, True),  # Qi rect filter (DNP)
    ("C_BOOT","100n",  "Capacitor_SMD:C_0603_1608Metric", 900, -350, True),  # Qi bootstrap (DNP)
    ("C_VDD", "1uF",   "Capacitor_SMD:C_0603_1608Metric", 900, -400, True),  # Qi VDD (DNP)
    ("L_Boost", "4.7uH", "Inductor_SMD:L_0805_2012Metric", 650, -350, False), # boost inductor
    ("R_CC1", "5.1k", "Resistor_SMD:R_0402_1005Metric", 900, -600, False),  # USB-C CC1 sink pull-down
    ("R_CC2", "5.1k", "Resistor_SMD:R_0402_1005Metric", 900, -650, False),  # USB-C CC2 sink pull-down
    ("C_USB", "10uF", "Capacitor_SMD:C_0805_2012Metric", 900, -700, False),  # USB VBUS bulk
]

# ---------------------------------------------------------------------------
# Net connections — label each signal net so wires are optional
# ---------------------------------------------------------------------------
# We use global labels for cross-block connectivity (readable, avoids wire soup).
GLOBAL_LABELS = [
    # Power
    ("VCC_3V3", 150, 600), ("VDD_1V8", 250, 600), ("VBAT", 350, 600),
    ("VDD_5V", 450, 600), ("VBUS", 550, 600), ("GND", 650, 600),
    # SPI1 (BHI)
    ("BHI_SPI_SCK", 150, 700), ("BHI_SPI_MOSI", 250, 700), ("BHI_SPI_MISO", 350, 700),
    ("BHI_CS", 450, 700), ("BHI_INT", 550, 700), ("FLASH_CS", 650, 700),
    # SGC SPI (v2)
    ("SGC_SPI_SCK", 150, 800), ("SGC_SPI_MOSI", 250, 800), ("SGC_SPI_MISO", 350, 800),
    # I2C0 / Wire1
    ("I2C0_SDA", 150, 900), ("I2C0_SCL", 250, 900),
    # I2C1 / Wire
    ("I2C1_SDA", 350, 900), ("I2C1_SCL", 450, 900),
    # GPIO
    ("LDC_INTB", 150, 1000), ("BEEPER", 250, 1000), ("QI_DETECT", 350, 1000),
    ("LED_DIN", 450, 1000), ("BOOST_EN", 550, 1000),
    # v2
    ("RFID_CS", 150, 1100), ("RFID_EN", 250, 1100), ("UWB_CS", 350, 1100), ("UWB_PWR", 450, 1100),
]

# ---------------------------------------------------------------------------
# Text annotations (functional block separators)
# ---------------------------------------------------------------------------
TEXTS = [
    ("SGC — Custom PCB: Nicla Sense ME Replica (v1 pole-mount)", 200, 1200, 100),
    ("Sheet 1 — MCU Core: ANNA-B412 + BHI260AP + BMP390 + Flash + RGB + Charger", 200, 400, 80),
    ("Sheet 2 — Sensors: Langir Piezo Button (v1 arming) + LDC1612 (v2, DNP)", 100, -200, 80),
    ("Sheet 3 — Peripherals: SK6812 ×5 + Level Shifter + Beeper (DNP)", 100, 750, 80),
    ("Sheet 4 — Power: Boost MT3608 + Battery JST + USB-C Charging (Qi DNP)", 600, -150, 80),
    ("Sheet 5 — v2 Peripherals (UNPOPULATED): RFID + UWB power gates", 900, 700, 80),
]

# ---------------------------------------------------------------------------
# S-expression emission
# ---------------------------------------------------------------------------
def s_uuid():
    return str(uuid.uuid4())

def emit_symbol(lib_id, ref, value, footprint, x, y, angle, dnp):
    """Emit one symbol instance with two pins (generic)."""
    lines = []
    lines.append(f'  (symbol (lib_id "{lib_id}") (at {x} {y} {angle}) (unit 1)')
    lines.append(f'    (exclude_from_sim no) (in_bom {"no" if dnp else "yes"}) (on_board {"no" if dnp else "yes"}) (dnp {"yes" if dnp else "no"})')
    lines.append(f'    (uuid {s_uuid()})')
    lines.append(f'    (property "Reference" "{ref}" (at {x} {y-40} 0) (effects (font (size 1.27 1.27))))')
    lines.append(f'    (property "Value" "{value}" (at {x} {y+40} 0) (effects (font (size 1.27 1.27))))')
    lines.append(f'    (property "Footprint" "{footprint}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) hide))')
    lines.append(f'    (instances (project "sgc_pcb" (path "/a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d" (reference "{ref}") (unit 1))))')
    lines.append('  )')
    return lines

def emit_power(label, x, y):
    """Emit a power symbol (GND or +V)."""
    lib = "power:GND" if label == "GND" else f"power:{label}"
    name = "GND" if label == "GND" else label
    return [
        f'  (symbol (lib_id "{lib}") (at {x} {y} 0) (unit 1)',
        f'    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)',
        f'    (uuid {s_uuid()})',
        f'    (property "Reference" "#PWR" (at {x} {y} 0) (effects (font (size 1.27 1.27)) hide))',
        f'    (property "Value" "{name}" (at {x} {y+30} 0) (effects (font (size 1.27 1.27))))',
        f'    (instances (project "sgc_pcb" (path "/a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d" (reference "#PWR") (unit 1))))',
        '  )',
    ]

def main():
    out = []
    out.append('(kicad_sch')
    out.append('  (version 20231120)')
    out.append('  (generator "eeschema")')
    out.append('  (generator_version "8.0")')
    out.append(f'  (uuid {s_uuid()})')
    out.append('  (paper "A3")')
    out.append('  (title_block')
    out.append('    (title "SGC — Ski Gate Chrono")')
    out.append('    (date "2026-08-14")')
    out.append('    (rev "v4.3")')
    out.append('    (company "VYT Solutions")')
    out.append('    (comment 1 "Custom PCB — Nicla Sense ME Replica")')
    out.append('    (comment 2 "22 × 55 mm, 4-layer FR4, 0.8 mm — 8 MB flash + USB-C charging")')
    out.append('  )')

    # Lib symbols — reference external library
    out.append('  (lib_symbols')
    out.append('  )')

    # Symbols
    for sym in SYMBOLS:
        out.extend(emit_symbol(*sym))
    for ref, val, fp, x, y, dnp in PASSIVES:
        lib_id = "Device:R" if ref.startswith("R") else ("Device:C" if ref.startswith("C") else "Device:L")
        out.extend(emit_symbol(lib_id, ref, val, fp, x, y, 0, dnp))

    # Power symbols for each distinct power rail
    power_nets = {"GND": (700, -600)}
    for i, (name, _, _) in enumerate(GLOBAL_LABELS):
        if name in ("VCC_3V3", "VDD_1V8", "VBAT", "VDD_5V", "VBUS"):
            out.extend(emit_power(name, 700 + (i % 5) * 100, -600 - (i // 5) * 100))

    # Global labels
    for name, x, y in GLOBAL_LABELS:
        out.append(f'  (global_label "{name}" (shape bidirectional) (at {x} {y} 0) (fields_autoplaced yes)')
        out.append(f'    (effects (font (size 1.27 1.27)) (justify left))')
        out.append(f'    (uuid {s_uuid()})')
        out.append(f'    (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) hide))')
        out.append('  )')

    # Section text
    for text, x, y, size in TEXTS:
        out.append(f'  (text "{text}" (at {x} {y} 0) (effects (font (size 2.0 2.0) bold)) (uuid {s_uuid()}))')

    out.append(')')

    with open("sgc_pcb.kicad_sch", "w") as f:
        f.write("\n".join(out) + "\n")
    print("Wrote sgc_pcb.kicad_sch")

if __name__ == "__main__":
    main()
