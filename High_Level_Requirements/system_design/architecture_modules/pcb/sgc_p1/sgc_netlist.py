#!/usr/bin/env python3
"""
sgc_netlist.py — SGC P1 board: symbol pin definitions + per-component net assignment.

SINGLE SOURCE OF TRUTH for both generators (gen_symbols.py, gen_schematic.py).

Every pin number/name and every net comes from:
  High_Level_Requirements/system_design/architecture_modules/sgc_schematic_spec.md  (v0.1, 92b352e)
Cross-checked against datasheet texts in /tmp: anna_b402.txt (UBX-20032372-R06 Table 4),
bq25120a.txt (SLUSD08A §7), bhi260ap.txt (BST-BHI260AP-DS000-02 pad table),
bmp390.txt (BST-BMP390-DS002-07), is31fl3194.txt, ldc1612.txt, usb4085.txt.

Anything the spec marks TBD-JP is NEVER invented — it appears as a schematic text note.

Net naming:
  power nets:  GND, +1V8 (VSYS rail), VBAT, V_PMID, VBUS_USB  -> drawn with power symbols
  signal nets: named global labels
  'NC'         -> no-connect flag on the pin
"""

P = 2.54  # pin pitch (mm)

# ---------------------------------------------------------------------------
# Symbol pin definitions.
# side: 'L' = left column (top->bottom), 'R' = right column (top->bottom),
#       'B' = bottom center pin (e.g. exposed pad).
# Order within each side = top-to-bottom placement order.
# ---------------------------------------------------------------------------
SYMBOL_PINS = {
    "D_SCHOTTKY": dict(
        prefix="D", value="SS34", fp="Diode_SMD:D_SMA",
        ds="", desc="Schottky 40V 3A SMA - MT3608 boost rectifier, V2 LED reserve, DNP v1", hw=5.08,
        pins=[
            ("1",  "K","power_out", "T"),
            ("2",  "A","passive", "B"),
        ],
    ),
    "MT3608": dict(
        prefix="U", value="MT3608", fp="Package_TO_SOT_SMD:SOT-23-6",
        ds="https://www.jlcpcb.com/partdetail/Aerosemi-MT3608_C84817.html", desc="Aerosemi MT3608 boost 2.0-24V in, 1.2MHz, EN shutdown ~2uA (pin6 NC not drawn). V2 LED reserve, DNP v1", hw=7.62,
        pins=[
            ("5",  "IN","power_in", "L"),
            ("4",  "EN","input", "L"),
            ("3",  "FB","input", "L"),
            ("1",  "SW","output", "R"),
            ("2",  "GND","power_in", "B"),
        ]),
    "TXS0102": dict(
        prefix="U", value="TXS0102 (OD-9 TBD)", fp="Package_SO:VSSOP-8_2.3x2mm_P0.5mm",
        ds="https://www.ti.com/lit/ds/symlink/txs0102.pdf", desc="TI TXS0102-class dual-supply translator VCCA +1V8 / VCCB +5V for SK6812 DIN level. V2 LED reserve, DNP v1", hw=7.62,
        pins=[
            ("1",  "VCCA","power_in", "L"),
            ("2",  "A1","bidirectional", "L"),
            ("3",  "A2","bidirectional", "L"),
            ("7",  "OE","input", "L"),
            ("8",  "VCCB","power_in", "R"),
            ("6",  "B1","bidirectional", "R"),
            ("5",  "B2","bidirectional", "R"),
            ("4",  "GND","power_in", "B"),
        ]),
    "CON-2": dict(
        prefix="J", value="JST-SH-2 piezo (TBD-JP)", hw=10.16,
        fp="Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal",
        ds="", desc="Off-board piezo arming button (Langir 16mm, arch doc 3) - water-resistant press sensing",
        pins=[
            ("1",  "PIN1","passive", "L"),
            ("2",  "PIN2","passive", "L"),
        ],
    ),
    "CON-3": dict(
        prefix="J", value="JST-SH-3 (TBD-JP)", hw=7.62,
        fp="Connector_JST:JST_SH_BM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal",
        ds="https://www.jst-mfg.com/product/detail_en.php?series=SR",
        desc="Off-board SK6812 strip feed: +5V / DATA / GND. Strip glued to pole or in transparent case (v2), NOT on PCB (JP 2026-11-01: space). DNP v1",
        pins=[
            ("1",  "PIN1","passive", "L"),
            ("2",  "PIN2","passive", "L"),
            ("3",  "PIN3","passive", "L"),
        ]),
    "SK6812": dict(
        prefix="D", value="SK6812MINI", fp="LED_SMD:LED_SK6812MINI_PLCC4_3.5x3.5mm_P1.75mm",
        ds="https://www.lcsc.com/product-detail/light-emitting-diodes-LED_OPSCO-SK6812MINI_C114586.html", desc="SK6812-mini RGB LED, 5V, 4-wire chain (DIN<-prev DOUT). x5 strip on VDD_LED. V2 LED reserve, DNP v1", hw=7.62,
        pins=[
            ("1",  "VDD","power_in", "T"),
            ("2",  "DIN","input", "L"),
            ("3",  "GND","power_in", "B"),
            ("4",  "DOUT","output", "R"),
        ]),
    # B402 Table 4, complete incl. pins 4/7/8 (GND) omitted from the spec table.
    # Left column = pins 1..28, right column = pins 29..56 (numeric order).
    "ANNA-B402": dict(
        prefix="U", value="ANNA-B402",
        fp="", ds="https://content.u-blox.com/sites/default/files/ANNA-B402_DataSheet_UBX-20032372.pdf",
        desc="u-blox ANNA-B402 open-CPU BLE 5.1 module (nRF52833), internal antenna",
        hw=17.78,
        pins=[
            ("1",  "ANT_PCB {GND pat.}",   "passive", "L"),
            ("2",  "ANT_GND1 {NC corner}", "passive", "L"),
            ("3",  "ANT_GND2 {NC corner}", "passive", "L"),
            ("4",  "GND",                  "power_in", "L"),
            ("5",  "ANT_INT {bridge>6}",   "passive", "L"),
            ("6",  "ANT {50R}",            "passive", "L"),
            ("7",  "GND",                  "power_in", "L"),
            ("8",  "GND",                  "power_in", "L"),
            ("9",  "VCC",                  "power_in", "L"),
            ("10", "GPIO_10",              "bidirectional", "L"),   # P0.20 UART TX TP
            ("11", "GPIO_11",              "bidirectional", "L"),   # P0.14 BHI HIRQ
            ("12", "RESET_N",              "input",    "L"),        # P0.18 module reset
            ("13", "GPIO_13",              "bidirectional", "L"),  # P1.09 spare
            ("14", "GPIO_14",              "bidirectional", "L"),  # P0.11 v2 user SPI SCK
            ("15", "GPIO_15",              "bidirectional", "L"),  # P0.12 spare
            ("16", "SWO/GPIO_16",          "bidirectional", "L"),  # P1.00 SWO
            ("17", "XL1/GPIO_17",          "bidirectional", "L"),  # P0.00 32k in
            ("18", "XL2/GPIO_18",          "bidirectional", "L"),  # P0.01 (GND w/ ext clk)
            ("19", "GPIO_19",              "bidirectional", "L"),  # P0.03 spi2 SCK
            ("20", "GPIO_20",              "bidirectional", "L"),  # P0.02 BUTTON
            ("21", "VBUS",                 "power_in", "L"),       # module USB, unused
            ("22", "NFC1/GPIO_22",         "bidirectional", "L"),  # P0.09 UART RX TP
            ("23", "NFC2/GPIO_23",         "bidirectional", "L"),  # P0.10 spare
            ("24", "GPIO_24",              "bidirectional", "L"),  # P0.05 spi2 MISO
            ("25", "GPIO_25",              "bidirectional", "L"),  # P0.04 spi2 MOSI
            ("26", "GPIO_26",              "bidirectional", "L"),  # P0.21 gauge /ALRT
            ("27", "GPIO_27",              "bidirectional", "L"),  # P0.31 BHI CS
            ("28", "GPIO_28",              "bidirectional", "L"),  # P0.30 v2 UWB_PWR
            ("29", "GPIO_29",              "bidirectional", "R"),  # P0.29 v2 UWB_CS
            ("30", "GPIO_30",              "bidirectional", "R"),  # P0.28 v2 user SPI MISO
            ("31", "GPIO_31",              "bidirectional", "R"),  # P0.27 v2 user SPI MOSI
            ("32", "GPIO_32",              "bidirectional", "R"),  # P0.06 spare
            ("33", "GPIO_33",              "bidirectional", "R"),  # P0.26 U7 CS
            ("34", "USBDP",                "bidirectional", "R"),  # module USB, unused
            ("35", "USBDM",                "bidirectional", "R"),  # module USB, unused
            ("36", "GPIO_36",              "bidirectional", "R"),  # P0.16 i2c0 SCL
            ("37", "GPIO_37",              "bidirectional", "R"),  # P0.22 i2c1 SDA
            ("38", "GPIO_38",              "bidirectional", "R"),  # P0.19 SK6812 data
            ("39", "GPIO_39",              "bidirectional", "R"),  # P0.23 i2c1 SCL
            ("40", "GPIO_40",              "bidirectional", "R"),  # P0.15 i2c0 SDA
            ("41", "SWDCLK",               "input",    "R"),
            ("42", "SWDIO",                "bidirectional", "R"),
            ("43", "GND",                  "power_in", "R"),
            ("44", "GND",                  "power_in", "R"),
            ("45", "GND",                  "power_in", "R"),
            ("46", "GND",                  "power_in", "R"),
            ("47", "GPIO_47",              "bidirectional", "R"),  # P1.07 spare
            ("48", "GPIO_48",              "bidirectional", "R"),  # P0.07 spare (trace clk)
            ("49", "GPIO_49",              "bidirectional", "R"),  # P0.17 boost EN
            ("50", "GPIO_50",              "bidirectional", "R"),  # P0.08 BQ CD
            ("51", "GPIO_51",              "bidirectional", "R"),  # P1.01 spare
            ("52", "GPIO_52",              "bidirectional", "R"),  # P0.13 BHI RESET
            ("53", "GND",                  "power_in", "R"),
            ("54", "GND",                  "power_in", "R"),
            ("55", "GND",                  "power_in", "R"),
            ("56", "GND",                  "power_in", "R"),
        ],
    ),
    "BQ25120A": dict(
        prefix="U", value="BQ25120A",
        fp="", ds="https://www.ti.com/lit/ds/slusd08a/slusd08a.pdf",
        desc="TI BQ25120A linear+buck charger, YFP-25 DSBGA; ball map verbatim SLUSD08A §7",
        hw=10.16,
        pins=[
            ("A1", "GND",     "power_in", "L"),
            ("A2", "IN",      "power_in", "L"),
            ("A3", "PMID",    "power_out", "L"),
            ("A4", "SW",      "power_out", "L"),
            ("A5", "PGND",    "power_in", "L"),
            ("B1", "BAT",     "power_in", "L"),
            ("B2", "BAT",     "power_in", "L"),
            ("B3", "PMID",    "power_out", "L"),
            ("B4", "VINLS",   "power_in", "L"),
            ("B5", "SYS",     "power_out", "L"),
            ("C1", "ISET",    "input", "L"),
            ("C2", "ILIM",    "input", "L"),
            ("C3", "TS",      "input", "L"),
            ("C4", "VINLS",   "power_in", "R"),
            ("C5", "LS/LDO",  "power_out", "R"),
            ("D1", "IPRETERM","input", "R"),
            ("D2", "INT",     "open_collector", "R"),
            ("D3", "RESET",   "output", "R"),
            ("D4", "/PG",     "open_collector", "R"),
            ("D5", "GND",     "power_in", "R"),
            ("E1", "/MR",     "input", "R"),
            ("E2", "/CD",     "input", "R"),
            ("E3", "LSCTRL",  "input", "R"),
            ("E4", "SDA",     "bidirectional", "R"),
            ("E5", "SCL",     "input", "R"),
        ],
    ),
    "MAX17048": dict(
        prefix="U", value="MAX17048G+T10",
        fp="", ds="https://www.analog.com/media/en/technical-documentation/data-sheets/max17048-max17049.pdf",
        desc="ADI MAX17048 fuel gauge, TDFN-8, addr 0x36; AD-029: VDD(3) senses, CELL(2) NC->GND",
        hw=10.16,
        pins=[
            ("1",  "CTG",   "passive", "L"),          # -> GND
            ("2",  "CELL",  "passive", "L"),          # NC internally -> GND (AD-029)
            ("3",  "VDD",   "power_in", "L"),         # VBAT sense
            ("4",  "GND",   "power_in", "L"),
            ("5",  "ALRT",  "open_collector", "R"),
            ("6",  "QSTRT", "input", "R"),
            ("7",  "SCL",   "input", "R"),
            ("8",  "SDA",   "bidirectional", "R"),
            ("EP", "EP",    "power_in", "B"),         # -> GND
        ],
    ),
    "IS31FL3194": dict(
        prefix="U", value="IS31FL3194",
        fp="", ds="https://www.lumissil.com/assets/pdf/core/IS31FL3194_DS.pdf",
        desc="Lumissil IS31FL3194 RGB LED driver, WLCSP-8, addr 0x53 (fixed 101 0011)",
        hw=10.16,
        pins=[
            ("A1", "SDB",  "input", "L"),
            ("B1", "SDA",  "bidirectional", "L"),
            ("C1", "SCL",  "input", "L"),
            ("D1", "GND",  "power_in", "L"),
            ("A2", "VCC",  "power_in", "R"),
            ("B2", "OUT1", "output", "R"),
            ("C2", "OUT2", "output", "R"),
            ("D2", "OUT3", "output", "R"),
        ],
    ),
    "LDC1612": dict(
        prefix="U", value="LDC1612",
        fp="", ds="https://www.ti.com/lit/ds/symlink/ldc1612.pdf",
        desc="TI LDC1612 inductance-to-digital, WSON-12; DNP v2, ADDR=GND -> 0x2A",
        hw=10.16,
        pins=[
            ("1",  "SCL",   "input", "L"),
            ("2",  "SDA",   "bidirectional", "L"),
            ("3",  "CLKIN", "input", "L"),            # tie GND if int. osc (v2 TBD-JP)
            ("4",  "ADDR",  "input", "L"),            # -> GND = 0x2A
            ("5",  "INTB",  "output", "L"),           # push-pull -> BUTTON net (OD-11)
            ("6",  "SD",    "input", "L"),            # -> GND
            ("7",  "VDD",   "power_in", "R"),
            ("8",  "GND",   "power_in", "R"),
            ("9",  "IN0A",  "passive", "R"),
            ("10", "IN0B",  "passive", "R"),
            ("11", "IN1A",  "passive", "R"),
            ("12", "IN1B",  "passive", "R"),
        ],
    ),
    "BHI260AP": dict(
        prefix="U", value="BHI260AP",
        fp="", ds="https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bhi260ap-ds000.pdf",
        desc="Bosch BHI260AP smart sensor hub, 44-pad LGA; H-IF SPI to nRF, QSPI flash, M1 master",
        hw=15.24,
        pins=[
            ("1",  "M3SDA",   "input", "L"),          # unused v1
            ("2",  "M3SCL",   "input", "L"),          # unused v1
            ("3",  "HOSTBOOT","input", "L"),          # NC v1 (TBD-JP vs Nicla)
            ("4",  "QSPI_D0", "input", "L"),
            ("5",  "QSPI_CLK","input", "L"),
            ("6",  "VREG",    "power_out", "L"),      # NC v1; decap TBD-JP (not in spec §6)
            ("7",  "VDDIO",   "power_in", "L"),
            ("8",  "QSPI_D3", "input", "L"),
            ("9",  "RESETN",  "input", "L"),          # -> P0.13 (moved, spec C3)
            ("10", "HIRQ",    "output", "L"),         # -> P0.14
            ("11", "HSDX",    "input", "L"),          # MOSI <- P0.04
            ("12", "VDDIO",   "power_in", "L"),
            ("13", "M2SCX",   "input", "L"),          # unused v1
            ("14", "QSPI_CSN","input", "L"),
            ("15", "QSPI_D1", "input", "L"),
            ("16", "MCSB3",   "input", "L"),          # unused v1
            ("17", "GNDIO",   "power_in", "L"),
            ("18", "MCSB2",   "input", "L"),          # BME688 CS (DNP, OD-6)
            ("19", "MCSB4",   "input", "L"),          # unused v1
            ("20", "QSPI_D2", "input", "L"),
            ("21", "OCSB",    "input", "L"),          # unused v1
            ("22", "ASCX",    "input", "L"),          # unused v1
            ("23", "JTAG_CLK","input", "L"),          # unused v1
            ("24", "JTAG_DIO","input", "L"),          # unused v1
            ("25", "GND",     "power_in", "R"),
            ("26", "GND",     "power_in", "R"),
            ("27", "GND",     "power_in", "R"),
            ("28", "VDD",     "power_in", "R"),
            ("29", "M1SCX",   "output", "R"),         # M1 SCK -> sub-sensors
            ("30", "ASDX",    "input", "R"),          # unused v1
            ("31", "RESV3",   "input", "R"),          # do not connect
            ("32", "HSDO",    "output", "R"),         # MISO -> P0.05
            ("33", "HSCX",    "input", "R"),          # SCK <- P0.03
            ("34", "HCSB",    "input", "R"),          # CS <- P0.31 (SPI mode)
            ("35", "M2SDX",   "output", "R"),         # unused v1
            ("36", "GNDIO",   "power_in", "R"),
            ("37", "M2SDI",   "input", "R"),          # unused v1
            ("38", "MCSB1",   "input", "R"),          # BMP390L CS
            ("39", "OSDO",    "input", "R"),          # unused v1
            ("40", "RESV2",   "input", "R"),          # do not connect
            ("41", "RESV1",   "input", "R"),          # do not connect
            ("42", "VDDIO",   "power_in", "R"),
            ("43", "M1SDI",   "input", "R"),          # M1 MISO <- sub-sensors
            ("44", "M1SDX",   "output", "R"),         # M1 MOSI -> sub-sensors
        ],
    ),
    "BMP390": dict(
        prefix="U", value="BMP390L",
        fp="", ds="https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp390-ds002.pdf",
        desc="Bosch BMP390L barometric pressure, 10-pin LGA; rides BHI260AP M1 port (Nicla parity)",
        hw=7.62,
        pins=[
            ("1",  "VDDIO", "power_in", "L"),
            ("2",  "SCK",   "input", "L"),
            ("3",  "VSS",   "power_in", "L"),
            ("4",  "SDI",   "bidirectional", "L"),
            ("5",  "SDO",   "output", "L"),
            ("6",  "CSB",   "input", "R"),
            ("7",  "INT",   "output", "R"),           # DNC in this use (ds)
            ("8",  "VSS",   "power_in", "R"),
            ("9",  "VSS",   "power_in", "R"),
            ("10", "VDD",   "power_in", "R"),
        ],
    ),
    "MX25R6435F": dict(
        prefix="U", value="MX25R6435FZAIL-08G",
        fp="Package_SO:SOIC-8_5.23x5.23mm_P1.27mm",
        ds="https://www.macronix.com/en-us/products/NOR-Flash/Serial-NOR-Flash/Pages/spec.aspx?p=MX25R6435F",
        desc="Macronix 64 Mb NOR flash (SGC RawRunStore), SOIC-8 200mil; pin config verbatim v1.6",
        hw=10.16,
        pins=[
            ("1", "CS#",                  "input", "L"),
            ("2", "SO/SIO1",              "output", "L"),
            ("3", "WP#/SIO2",             "input", "L"),   # NC: internal PU (ds Note 1)
            ("4", "GND",                  "power_in", "L"),
            ("5", "SI/SIO0",              "input", "R"),
            ("6", "SCLK",                 "input", "R"),
            ("7", "RESET#/HOLD#/SIO3",    "input", "R"),   # NC: internal PU (ds Note 1)
            ("8", "VCC",                  "power_in", "R"),
        ],
    ),
    "MX25R1635F": dict(
        prefix="U", value="MX25R1635FZUIH0",
        fp="Package_SO:SOIC-8_5.23x5.23mm_P1.27mm",
        ds="https://www.macronix.com/en-us/products/NOR-Flash/Serial-NOR-Flash/Pages/spec.aspx?p=MX25R1635F",
        desc="Macronix 16 Mb NOR flash = BHI260AP FW flash (Nicla U2); OD-5: add to BOM",
        hw=10.16,
        pins=[
            ("1", "CS#",         "input", "L"),
            ("2", "SO/SIO1",     "output", "L"),
            ("3", "WP#/SIO2",    "input", "L"),
            ("4", "GND",         "power_in", "L"),
            ("5", "SI/SIO0",     "input", "R"),
            ("6", "SCLK",        "input", "R"),
            ("7", "HOLD#/SIO3",  "input", "R"),
            ("8", "VCC",         "power_in", "R"),
        ],
    ),
    "USB4085": dict(
        prefix="J", value="USB4085-GF-A",
        fp="", ds="https://gct.co/connector/usb4085",
        desc="GCT USB4085-GF-A USB-C 2.0 mid-mount, 16 SMD pins + 4 shell stakes",
        hw=12.7,
        pins=[
            ("A1",  "GND",   "passive", "L"),
            ("A4",  "VBUS",  "passive", "L"),
            ("A5",  "CC1",   "passive", "L"),
            ("A6",  "D+",    "passive", "L"),
            ("A7",  "D-",    "passive", "L"),
            ("A8",  "SBU1",  "passive", "L"),
            ("A9",  "VBUS",  "passive", "L"),
            ("A12", "GND",   "passive", "L"),
            ("S1",  "SHELL", "passive", "L"),
            ("S2",  "SHELL", "passive", "L"),
            ("B12", "GND",   "passive", "R"),
            ("B9",  "VBUS",  "passive", "R"),
            ("B8",  "SBU2",  "passive", "R"),
            ("B7",  "D-",    "passive", "R"),
            ("B6",  "D+",    "passive", "R"),
            ("B5",  "CC2",   "passive", "R"),
            ("B4",  "VBUS",  "passive", "R"),
            ("B1",  "GND",   "passive", "R"),
            ("S3",  "SHELL", "passive", "R"),
            ("S4",  "SHELL", "passive", "R"),
        ],
    ),
    "JST-BAT-3": dict(
        prefix="J", value="JST-3 battery (PN TBD-JP, OD-4)",
        fp="", ds="",
        desc="Battery connector VBAT/GND/NTC (3-wire pack, per ABX00050 pinout); PN per OD-4",
        hw=5.08,
        pins=[
            ("1", "VBAT", "passive", "L"),
            ("2", "NTC",  "passive", "L"),
            ("3", "GND",  "passive", "L"),
        ],
    ),
    "SWD-2X5": dict(
        prefix="J", value="SWD 2x5 1.27mm Cortex Debug",
        fp="", ds="",
        desc="ARM Cortex Debug 10-pin; OD-2: Tag-Connect TC2050 footprint alternative",
        hw=7.62,
        pins=[
            ("1",  "VTREF", "passive", "L"),   # +1V8 sense
            ("2",  "SWDIO", "bidirectional", "L"),
            ("3",  "GND",   "passive", "L"),
            ("4",  "SWDCLK","input", "L"),
            ("5",  "GND",   "passive", "L"),
            ("6",  "SWO",   "output", "L"),
            ("7",  "KEY",   "passive", "R"),   # no pin (key)
            ("8",  "TDI",   "input", "R"),     # NC
            ("9",  "GND",   "passive", "R"),
            ("10", "NRESET","input", "R"),     # RESET_N
        ],
    ),
    # --- generic passives (Device-library style, in our lib for self-containment) ---
    "R": dict(
        prefix="R", value="R", fp="", ds="", desc="Resistor", hw=1.016,
        pins=[("1", "~", "passive", "T"), ("2", "~", "passive", "B")],
    ),
    "C": dict(
        prefix="C", value="C", fp="", ds="", desc="Capacitor", hw=0.762,
        pins=[("1", "~", "passive", "T"), ("2", "~", "passive", "B")],
    ),
    "L": dict(
        prefix="L", value="L", fp="", ds="", desc="Inductor", hw=1.016,
        pins=[("1", "~", "passive", "T"), ("2", "~", "passive", "B")],
    ),
    "LED_RGB": dict(
        prefix="D", value="SMLP34RGB2W3", fp="", ds="",
        desc="ROHM SMLP34RGB2W3 RGB chip LED (Nicla DL1); common polarity verify at capture",
        hw=3.81,
        pins=[("R", "R", "passive", "L"), ("G", "G", "passive", "L"), ("B", "B", "passive", "L"),
              ("COM", "COM", "passive", "R")],
    ),
}

# Vertical body length for T/B-pin passives (pin1 at +len, pin2 at -len)
PASSIVE_LEN = {"R": 3.81, "C": 2.91, "L": 3.81}

# ---------------------------------------------------------------------------
# Power symbol definitions (KiCad standard-library style)
# ---------------------------------------------------------------------------
POWER_SYMBOLS = {
    # name -> (netname == value, arrow style)  graphics: bar above (rail) / GND bars below
    "GND":      "gnd",
    "+1V8":     "rail",
    "VBAT":     "rail",
    "V_PMID":   "rail",
    "VBUS_USB": "rail",
    "+5V":      "rail",
}

# ---------------------------------------------------------------------------
# Component placement: refdes -> (symbol, x, y, {pin: net-or-'NC'}, value, footprint, dnp)
# Coordinates = symbol origins on the A3 sheet (mm). Grouping per task brief:
# USB-C+charger top-left, gauge+battery bottom-left, B402 center, sensors right,
# flash+LED right-bottom, SWD bottom-left.
# ---------------------------------------------------------------------------
E = ""  # empty footprint (custom, see README)

PARTS = [

    # --- V2 LED STRIP RESERVE (JP 2026-10-31 b): all DNP v1 ---
    dict(ref="U11", sym="TXS0102", x=312, y=248, dnp=True, nets={"1": "+1V8", "2": "SK6812_DATA", "3": "NC", "4": "GND", "5": "NC", "6": "LED_DIN_5V", "7": "+1V8", "8": "+5V"}),
    dict(ref="U10", sym="MT3608", x=352, y=248, dnp=True, nets={"1": "BOOST_SW", "2": "GND", "3": "BOOST_FB", "4": "BOOST_EN", "5": "VBAT"}),
    dict(ref="L2", sym="L", x=384, y=248, fp="Inductor_SMD:L_0805_2012Metric", dnp=True, value="4.7uH 2A", nets={"1": "BOOST_SW", "2": "VBAT"}),  # 2026-11-03: L2 belongs VBAT->SW (was on the diode slot - spec bug)
    dict(ref="D2", sym="D_SCHOTTKY", x=386, y=230, dnp=True, fp="Diode_SMD:D_SMA", value="SS34 (TBD-JP)", nets={"1": "+5V", "2": "BOOST_SW"}),
    dict(ref="R13", sym="R", x=287.02, y=40.64, dnp=False, fp="Resistor_SMD:R_0402_1005Metric", value="10k 1%", nets={"1": "HOSTBOOT", "2": "GND"}),
    dict(ref="C20", sym="C", x=287.02, y=50.8, dnp=False, fp="Capacitor_SMD:C_0402_1005Metric", value="220n", nets={"1": "VREG", "2": "GND"}),
    dict(ref="J5", sym="CON-2", x=109.22, y=246.38, dnp=False, fp="Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal", value="JST-SH-2 piezo (TBD-JP)", nets={"1": "BUTTON", "2": "GND"}),  # 2026-11-03: boost rectifier added
    dict(ref="R11", sym="R", x=304, y=266, fp="Resistor_SMD:R_0402_1005Metric", dnp=True, value="75k 1%", nets={"1": "+5V", "2": "BOOST_FB"}),
    dict(ref="R12", sym="R", x=320, y=266, fp="Resistor_SMD:R_0402_1005Metric", dnp=True, value="10k 1%", nets={"1": "BOOST_FB", "2": "GND"}),
    dict(ref="C17", sym="C", x=336, y=266, fp="Capacitor_SMD:C_0603_1608Metric", dnp=True, value="10uF", nets={"1": "VBAT", "2": "GND"}),
    dict(ref="C18", sym="C", x=352, y=266, fp="Capacitor_SMD:C_0805_2012Metric", dnp=True, value="22uF", nets={"1": "+5V", "2": "GND"}),
    dict(ref="C19", sym="C", x=368, y=266, fp="Capacitor_SMD:C_0402_1005Metric", dnp=True, value="22pF", nets={"1": "+5V", "2": "BOOST_FB"}),
    dict(ref="J4", sym="CON-3", x=392, y=266, dnp=True, nets={"1": "+5V", "2": "LED_DIN_5V", "3": "GND"}),
    # --- USB-C + CC resistors + bulk cap (left edge, top) ---
    dict(ref="J1",  sym="USB4085",    x=52,  y=55,  fp=E, dnp=False, nets={
        "A1": "GND", "A4": "VBUS_USB", "A5": "CC1", "A6": "NC", "A7": "NC", "A8": "NC",
        "A9": "VBUS_USB", "A12": "GND", "S1": "GND", "S2": "GND",
        "B12": "GND", "B9": "VBUS_USB", "B8": "NC", "B7": "NC", "B6": "NC",
        "B5": "CC2", "B4": "VBUS_USB", "B1": "GND", "S3": "GND", "S4": "GND"}),
    dict(ref="R7",  sym="R", x=75, y=25, fp="Resistor_SMD:R_0402_1005Metric", dnp=False, value="5.1k 1%", nets={"1": "CC1", "2": "GND"}),   # Rd sink CC1
    dict(ref="R8",  sym="R", x=85, y=25, fp="Resistor_SMD:R_0402_1005Metric", dnp=False, value="5.1k 1%", nets={"1": "CC2", "2": "GND"}),   # Rd sink CC2
    dict(ref="C16", sym="C", x=60, y=80, fp="Capacitor_SMD:C_0603_1608Metric", dnp=False, value="10uF", nets={"1": "VBUS_USB", "2": "GND"}),  # VBUS bulk @ conn

    # --- Charger subsystem (top-left) ---
    dict(ref="U3", sym="BQ25120A", x=120, y=58, fp=E, dnp=False, nets={
        "A1": "GND", "A2": "VBUS_USB", "A3": "V_PMID", "A4": "SW_NODE", "A5": "GND",
        "B1": "VBAT", "B2": "VBAT", "B3": "V_PMID", "B4": "+1V8", "B5": "+1V8",
        "C1": "ISET_R", "C2": "ILIM_R", "C3": "TS_NTC",
        "C4": "+1V8", "C5": "LSLDO_C",
        "D1": "IPRETERM_R", "D2": "BQ_INT", "D3": "NC", "D4": "NC", "D5": "GND",
        "E1": "NC", "E2": "BQ_CD", "E3": "GND", "E4": "I2C0_SDA", "E5": "I2C0_SCL"}),
    dict(ref="C1", sym="C", x=152, y=42, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="1uF",   nets={"1": "VBUS_USB", "2": "GND"}),   # BQ IN bypass (>=1uF)
    dict(ref="C2", sym="C", x=152, y=52, fp="Capacitor_SMD:C_0603_1608Metric", dnp=False, value="4.7uF", nets={"1": "V_PMID", "2": "GND"}),    # PMID >=3uF, Nicla C10
    dict(ref="C3", sym="C", x=152, y=62, fp="Capacitor_SMD:C_0603_1608Metric", dnp=False, value="10uF",  nets={"1": "+1V8", "2": "GND"}),      # SYS >=4.7uF, Nicla C15
    dict(ref="C4", sym="C", x=152, y=72, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="1uF",   nets={"1": "VBAT", "2": "GND"}),      # BAT bypass
    dict(ref="C5", sym="C", x=152, y=82, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="1uF",   nets={"1": "+1V8", "2": "GND"}),      # VINLS (-> SYS per C4 rule)
    dict(ref="C6", sym="C", x=152, y=92, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="1uF",   nets={"1": "LSLDO_C", "2": "GND"}),   # LS/LDO stability
    dict(ref="L1", sym="L", x=150, y=30, fp="Inductor_SMD:L_0603_1608Metric",  dnp=False, value="2.2uH", nets={"1": "SW_NODE", "2": "+1V8"}),   # Nicla MLZ1608A2R2WT000
    dict(ref="R1", sym="R", x=168, y=48, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="1.82k 1%", nets={"1": "ISET_R", "2": "GND"}),      # ICHG ~110 mA (OD-15)
    dict(ref="R2", sym="R", x=168, y=58, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="1k 1%",    nets={"1": "ILIM_R", "2": "GND"}),       # ILIM 200 mA
    dict(ref="R3", sym="R", x=168, y=68, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="4.99k 1%", nets={"1": "IPRETERM_R", "2": "GND"}),  # term/prechg 10%
    dict(ref="R4", sym="R", x=168, y=80, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="100k",     nets={"1": "+1V8", "2": "BQ_CD"}),      # CD PU V_PMID (OD-8)
    dict(ref="R5", sym="R", x=168, y=92, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="100k",     nets={"1": "+1V8", "2": "BQ_INT"}),       # BQ INT PU
    dict(ref="R9", sym="R", x=184, y=48, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="4.7k 1%",     nets={"1": "+1V8", "2": "I2C0_SDA"}),     # i2c0 PU (Nicla parity)
    dict(ref="R10",sym="R", x=184, y=60, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="4.7k 1%",     nets={"1": "+1V8", "2": "I2C0_SCL"}),

    # --- Fuel gauge + battery (bottom-left) ---
    dict(ref="U4", sym="MAX17048", x=60, y=150, fp=E, dnp=False, nets={
        "1": "GND", "2": "GND", "3": "VBAT", "4": "GND", "5": "GAUGE_ALRT",
        "6": "GND", "7": "I2C0_SCL", "8": "I2C0_SDA", "EP": "GND"}),
    dict(ref="C7", sym="C", x=95, y=140, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="0.1uF", nets={"1": "VBAT", "2": "GND"}),   # gauge VDD bypass
    dict(ref="R6", sym="R", x=84, y=164, fp="Resistor_SMD:R_0402_1005Metric",  dnp=False, value="10k",    nets={"1": "+1V8", "2": "GAUGE_ALRT"}), # /ALRT PU
    dict(ref="J2", sym="JST-BAT-3", x=35, y=200, fp=E, dnp=False, nets={
        "1": "VBAT", "2": "TS_NTC", "3": "GND"}),

    # --- ANNA-B402 (center) ---
    dict(ref="U1", sym="ANNA-B402", x=225, y=132, fp=E, dnp=False, nets={
        "1": "GND",        # ANT_PCB -> GND pattern (corner placement, spec 4.9)
        "2": "NC", "3": "NC",
        "4": "GND", "5": "ANT", "6": "ANT", "7": "GND", "8": "GND",
        "9": "+1V8",
        "10": "UART_TX_TP", "11": "BHI_HIRQ", "12": "RESET_N", "13": "NC",
        "14": "USER_SPI_SCK", "15": "NC", "16": "SWO",
        "17": "32K_XL1", "18": "GND",        # XL2 -> GND w/ external clock (spec 3.2)
        "19": "SPI2_SCK", "20": "BUTTON", "21": "NC",   # module USB unused
        "22": "UART_RX_TP", "23": "NC", "24": "SPI2_MISO", "25": "SPI2_MOSI",
        "26": "GAUGE_ALRT", "27": "CS_BHI",
        "28": "UWB_PWR", "29": "UWB_CS", "30": "USER_SPI_MISO", "31": "USER_SPI_MOSI",
        "32": "NC", "33": "CS_FLASH", "34": "NC", "35": "NC",
        "36": "I2C0_SCL", "37": "I2C1_SDA", "38": "SK6812_DATA", "39": "I2C1_SCL",
        "40": "I2C0_SDA", "41": "SWDCLK", "42": "SWDIO",
        "43": "GND", "44": "GND", "45": "GND", "46": "GND",
        "47": "NC", "48": "NC", "49": "BOOST_EN", "50": "BQ_CD", "51": "NC",
        "52": "BHI_RESET",
        "53": "GND", "54": "GND", "55": "GND", "56": "GND"}),

    # --- Sensor cluster (right) ---
    dict(ref="U8", sym="BHI260AP", x=330, y=65, fp=E, dnp=False, nets={"3": "HOSTBOOT", "6": "VREG", 
        "1": "NC", "2": "NC", "4": "QSPI_IO0", "5": "QSPI_SCK",
        "7": "+1V8", "8": "QSPI_IO3", "9": "BHI_RESET", "10": "BHI_HIRQ",
        "11": "SPI2_MOSI", "12": "+1V8", "13": "NC", "14": "QSPI_CS", "15": "QSPI_IO1",
        "16": "NC", "17": "GND", "18": "M1_CSB2", "19": "NC", "20": "QSPI_IO2",
        "21": "NC", "22": "NC", "23": "NC", "24": "NC",
        "25": "GND", "26": "GND", "27": "GND", "28": "+1V8",
        "29": "M1_SCK", "30": "NC", "31": "NC", "32": "SPI2_MISO", "33": "SPI2_SCK",
        "34": "CS_BHI", "35": "NC", "36": "GND", "37": "NC", "38": "M1_CSB1",
        "39": "NC", "40": "NC", "41": "NC", "42": "+1V8",
        "43": "M1_MISO", "44": "M1_MOSI"}),
    dict(ref="C10", sym="C", x=385, y=40, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="1uF",  nets={"1": "+1V8", "2": "GND"}),  # BHI bank C5
    dict(ref="C11", sym="C", x=385, y=52, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="100n", nets={"1": "+1V8", "2": "GND"}),  # BHI bank C6
    dict(ref="C12", sym="C", x=385, y=64, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="100n", nets={"1": "+1V8", "2": "GND"}),  # BHI bank C7
    dict(ref="C13", sym="C", x=385, y=76, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="100n", nets={"1": "+1V8", "2": "GND"}),  # BHI bank C12/C13
    dict(ref="U9", sym="BMP390", x=330, y=130, fp=E, dnp=False, nets={
        "1": "+1V8", "2": "M1_SCK", "3": "GND", "4": "M1_MOSI", "5": "M1_MISO",
        "6": "M1_CSB1", "7": "NC", "8": "GND", "9": "GND", "10": "+1V8"}),
    dict(ref="C14", sym="C", x=365, y=132, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="100n", nets={"1": "+1V8", "2": "GND"}), # BMP390 decap

    # --- RGB LED + driver (right-bottom) ---
    dict(ref="U5", sym="IS31FL3194", x=315, y=175, fp=E, dnp=False, nets={
        "A1": "V_PMID", "B1": "I2C0_SDA", "C1": "I2C0_SCL", "D1": "GND",
        "A2": "V_PMID", "B2": "LED_R", "C2": "LED_G", "D2": "LED_B"}),
    dict(ref="D1", sym="LED_RGB", x=372, y=175, fp=E, dnp=False, value="SMLP34RGB2W3", nets={
        "R": "LED_R", "G": "LED_G", "B": "LED_B", "COM": "V_PMID"}),
    dict(ref="C15", sym="C", x=395, y=175, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="0.1uF", nets={"1": "V_PMID", "2": "GND"}), # IS31 decap (value TBD-JP, OD-17)

    # --- Flash (bottom-right) ---
    dict(ref="U7", sym="MX25R6435F", x=300, y=225, fp="Package_SO:SOIC-8_5.23x5.23mm_P1.27mm", dnp=False, nets={
        "1": "CS_FLASH", "2": "SPI2_MISO", "3": "NC", "4": "GND",
        "5": "SPI2_MOSI", "6": "SPI2_SCK", "7": "NC", "8": "+1V8"}),
    dict(ref="C8", sym="C", x=348, y=217, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="100n", nets={"1": "+1V8", "2": "GND"}),
    dict(ref="U2", sym="MX25R1635F", x=300, y=258, fp="Package_SO:SOIC-8_5.23x5.23mm_P1.27mm", dnp=False, nets={
        "1": "QSPI_CS", "2": "QSPI_IO1", "3": "QSPI_IO2", "4": "GND",
        "5": "QSPI_IO0", "6": "QSPI_SCK", "7": "QSPI_IO3", "8": "+1V8"}),
    dict(ref="C9", sym="C", x=348, y=250, fp="Capacitor_SMD:C_0402_1005Metric", dnp=False, value="100n", nets={"1": "+1V8", "2": "GND"}),

    # --- LDC1612 v2 DNP footprint (sensor bus) ---
    dict(ref="U6", sym="LDC1612", x=160, y=245, fp=E, dnp=True, nets={
        "1": "I2C1_SCL", "2": "I2C1_SDA", "3": "NC", "4": "GND", "5": "BUTTON",
        "6": "GND", "7": "+1V8", "8": "GND",
        "9": "LDC_CH0A", "10": "LDC_CH0B", "11": "LDC_CH1A", "12": "LDC_CH1B"}),

    # --- SWD connector (bottom-left) ---
    dict(ref="J3", sym="SWD-2X5", x=60, y=258, fp=E, dnp=False, nets={
        "1": "+1V8", "2": "SWDIO", "3": "GND", "4": "SWDCLK", "5": "GND",
        "6": "SWO", "7": "NC", "8": "NC", "9": "GND", "10": "RESET_N"}),
]

# Value overrides/defaults: parts without explicit 'value' take symbol default (part number).
for p in PARTS:
    p.setdefault("value", SYMBOL_PINS[p["sym"]]["value"])

# ---------------------------------------------------------------------------
# Text notes (text boxes): (x, y, w, h, text)
# ---------------------------------------------------------------------------
NOTES = [
    (140, 14, 165, 12,
     "SGC P1 - ANNA-B402 carrier, 22x55 mm 4-layer (0.8 mm, ENIG) - rev A-WIP - 2026-10-29\n"
     "Generated from sgc_schematic_spec.md v0.1 (92b352e). Every pin/net traceable to the spec;\n"
     "TBD-JP items appear as notes only - nothing invented. Regenerate: python3 gen_symbols.py && python3 gen_schematic.py"),
    (160, 72, 132, 17,
     "ANTENNA KEEPOUT (SIM UBX-21000517 App.B p.70): no traces/vias in ANY layer in the antenna strip clearance area.\n"
     "Internal antenna: ANT_INT(5)-ANT(6) bridge. Corner placement -> ANT_PCB(1) = GND pattern, ANT_GND1/2 (2,3) NC.\n"
     "Stackup must be checked vs u-blox reference (certification). Keep U8/IS31 clear of antenna strip (IS31 ds EMI note)."),
    (15, 104, 132, 26,
     "CHARGER (spec 4.1/4.7): ISET 1.82k -> ICHG ~110 mA; ILIM 1k -> 200 mA; IPRETERM 4.99k -> 10%; VBATREG default 4.2 V.\n"
     "OD-8: /CD -> P0.08 (B402 pin 50, moved from P0.25 - P0.25 does not exist) + 100k PU to V_PMID (ship-mode capable).\n"
     "BQ INT -> 100k PU +1V8, NC on MCU (read FAULTS reg, AD-029); /PG TP only. /MR, RESET NC (Nicla P0.18->MR net dropped, C3).\n"
     "LS/LDO unused (C4): LSCTRL->GND, VINLS->SYS, LS/LDO 1uF. TS: R_HI (VBUS->TS) + R_LO (TS->GND) + pack NTC - values TBD-JP (OD-4).\n"
     "i2c0 pull-ups 100k to +1V8 (Nicla parity; BQ ds suggests 10k - spec chose Nicla parity). USB-IF inrush note OD-14."),
    (15, 86, 40, 14,
     "USB-C: VBUS 10uF bulk at connector; D+/D- NC; shell stitched to GND.\n"
     "ESD D5 TBD-JP (OD-13: PESD5V0U2UT vs TPD4E05U06)."),
    (15, 176, 44, 16,
     "GAUGE (spec 4.2, AD-029): addr 0x36 fixed; VDD(3)=VBAT sense +0.1uF; CELL(2) NC->GND; CTG/QSTRT->GND.\n"
     "ALRT(5) OD -> 10k PU +1V8 -> P0.21 (B402 pin 26). OD-12: verify MAX17048 VIH vs 1.8 V pull-ups at review."),
    (240, 185, 68, 24,
     "I2C MAP (spec 2):\n"
     "i2c0 power bus P0.15/P0.16 (PU 100k -> +1V8): 0x36 MAX17048, 0x53 IS31FL3194, 0x6A BQ25120A.\n"
     "i2c1 sensor bus P0.22/P0.23: 0x2A LDC1612 (DNP v2), 0x52 RV-3028-C7 (DNP v2, /INT -> TP)."),
    (282, 100, 122, 22,
     "BHI CLUSTER (spec 4.4): BMP390L/BMM150/BME688 are sub-sensors on BHI M1 master port - exact M1/M2/M3 wiring = COPY\n"
     "ABX00050 (Nicla) at capture (TBD-JP). BME688 DNP per OD-6 (footprint retained). H-IF SPI 16 MHz (app.overlay) on\n"
     "P0.03-05 despite Table 4 low-freq remark - risk R1: drop to 8/4 MHz if RF regressions. RESETN -> P0.13 (moved, C3/OD-7).\n"
     "HOSTBOOT(3) NC v1 (TBD-JP vs Nicla); VREG(6) NC v1 (decoupling per Bosch ds - TBD-JP, not in spec 6); RESV1-3 NC (do not connect)."),
    (352, 148, 48, 16,
     "LED (spec 4.3): U5 VCC/SDB -> V_PMID (OD-17: confirm SDB net on ABX00050 at capture); decap value TBD-JP.\n"
     "IS31 OUT1-3 are current sinks - verify SMLP34RGB2W3 common polarity at capture (spec text says anodes to OUTs)."),
    (352, 196, 48, 22,
     "FLASH: U7 = RawRunStore 8 MB (flash layout per hardware doc). U2 = BHI FW flash (Arduino_BHY2 upload) -\n"
     "OD-5: ADD U2 to BOM (Nicla parity, populated; missing from BOM v4.3). WP#/HOLD# NC -> internal PU (Macronix Note 1).\n"
     "U7/U2 decap 100n = Nicla parity (TBD-JP keep, spec 6)."),
    (125, 185, 105, 46,
     "PERIPHERALS / v2 DNP (spec 2/4.3/4.6/4.8):\n"
     "SK6812-mini x5 strip: DIN <- P0.19 (B402 pin 38) through dual-supply level translator (VCCA +1V8 / VCCB +5V,\n"
     "TXB0102/TXS0102-class - part TBD-JP, OD-9). Strip supply +5V from MT3608 boost (EN -> P0.17 = B402 pin 49, moved\n"
     "from P0.24 - does not exist; OD-9b: keep P0.19 vs move to full-drive GPIO - decide after R2 bench).\n"
     "Boost BOM (hw doc 7.3): L 4.7uH/2A, Cin 10uF, Cout 22uF, Rfb 75k/10k, Cff 22pF. NOT drawn - capture at layout.\n"
     "32k: Y1 SIT1532AI (OD-10 default FIT) -> XL1 (pin 17); XL2 (18) -> GND; Y1 decap TBD-JP. LFRC-only = EVK alternative.\n"
     "UART console TP: TX P0.20 (pin 10) / RX P0.09 (pin 22, NFC1 - no NFC coil on board). TP set per OD-3:\n"
     "+1V8, +5V, VBAT, V_PMID, GND, BQ INT, SWD row, UART row, ALRT, LED-data.\n"
     "DNP v2 (footprints retained): LDC1612 U6 + 14mm coil (ADDR=GND -> 0x2A; INTB -> BUTTON net per OD-11; CLKIN tie\n"
     "GND if internal osc - v2), RV-3028-C7 @0x52 (/INT -> TP, no GPIO steal), BZ1 beeper (P0.09 via 100R/NPN).\n"
     "nRF P0.24/P0.25 DO NOT EXIST on B402 (spec C1/C2 pin moves). Module USB (pins 21/34/35) unused - NC."),
    (15, 216, 110, 30,
     "BATTERY / POWER TREE (spec 4.8/5): Renata ICP622540PMT 600 mAh 3.7 V LiPo, 3-wire NTC pack, JST-3 user-replaceable\n"
     "(connector PN TBD-JP, OD-4; pinout per ABX00050 = VBAT/NTC/GND). VSYS is the +1V8 rail (buck default 1.8 V, 300 mA).\n"
     "VBAT -> MAX17048 VDD only. V_PMID -> IS31FL3194. Charge 110 mA + system ~25 mA + LED ~23 mA bat-side = 158 mA < ILIM 200 mA.\n"
     "Session ~48 mAh -> ~12.5 sessions (hw doc 7); gauge adds ~3 uA (AD-029). Battery capacity 600 mAh per docs (OD-4 confirm)."),
    (15, 274, 92, 14,
     "J3 = ARM Cortex Debug 2x5 1.27 mm: 1 VTref(+1V8) 2 SWDIO 3 GND 4 SWDCLK 5 GND 6 SWO 7 KEY-NC 8 TDI-NC 9 GND 10 nRESET(=RESET_N, B402 pin 12 - nothing else on pin 12 except TP).\n"
     "OD-2: Tag-Connect TC2050 footprint alternative (castellated pads). SWO = B402 pin 16 (optional)."),
    (196, 240, 52, 16,
     "U6 LDC1612 = DNP v2 footprint (unpopulated v1, spec 1/4.6). Coil = 14 mm spiral on CH0 (footprint retained, v2)."),
]

# ---------------------------------------------------------------------------
# Power-symbol zone anchors: (net, x, y). Each anchor = power symbol + a short
# wire + global label with the same net name (wire down for rails, up for GND).
# Power nets reach every pin via global labels; anchors give each zone its
# KiCad-idiomatic #PWR symbols.
# ---------------------------------------------------------------------------
POWER_ANCHORS = [
    ("VBUS_USB", 30, 35),   # zone A (USB/charger)
    ("+1V8", 204, 32),
    ("VBAT", 204, 52),
    ("V_PMID", 204, 68),
    ("GND", 30, 134),
    ("+1V8", 95, 157),      # zone B (gauge/battery)
    ("VBAT", 25, 192),
    ("GND", 115, 133),
    ("+1V8", 200, 180),     # zone C (B402)
    ("GND", 250, 180),
    ("+1V8", 405, 38),      # zone D (sensors)
    ("GND", 405, 96),
    ("V_PMID", 400, 168),   # zone E (LED/flash)
    ("GND", 405, 230),
    ("+1V8", 405, 245),
    ("GND", 230, 258),      # zone F (LDC v2)
    ("+1V8", 25, 245),      # zone G (SWD)
    ("GND", 22, 262),
]

#nets that exist only as reserved/v2 or TP-only (documented, intentionally floating besides stubs)
NET_NOTES = {
    "USER_SPI_SCK": "v2 user-SPI header net - NC v1 (spec 3.2)",
    "USER_SPI_MISO": "v2 user-SPI header net - NC v1",
    "USER_SPI_MOSI": "v2 user-SPI header net - NC v1",
    "UWB_PWR": "v2 UWB rail enable - NC v1",
    "UWB_CS": "v2 UWB chip select - NC v1",
    "LDC_CH0A": "v2 LDC coil ch0 (DNP)",
    "LDC_CH0B": "v2 LDC coil ch0 (DNP)",
    "LDC_CH1A": "v2 LDC coil ch1 (DNP)",
    "LDC_CH1B": "v2 LDC coil ch1 (DNP)",
    "LSLDO_C": "BQ LS/LDO decap node (LS/LDO disabled, spec C4)",
}
