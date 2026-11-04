# Nicla (P0) → custom N1 board: firmware migration assessment

**JP question 2026-11-04:** *"In general, will we need to change the code? How likely is it that we will get
problems when migrating from the Nicla to the custom boosted version? Let's introduce the eventual
differences behind macro switches, so that a macro declaration is enough to compile for one module or
another."*

**Short answer:** Yes, code changes — but only **3 real ones**, and the "one declaration" mechanism you want
**already exists** and is better than macros. Honestly measured below.

---

## 1. What is actually hardcoded today (measured, not estimated)

The whole app is **9,122 lines** of C. A full grep for every pin, port and register constant finds:

| Location | What | Portable? |
|---|---|---|
| `app.overlay:32` | `cs-gpios = <&gpio0 26>, <&gpio0 31>` — flash CS, BHI CS | ✅ devicetree (move to board) |
| `app.overlay:63` | button `gpios = <&gpio0 2>` | ✅ devicetree (move to board) |
| `app.overlay:34-44` | `&spi2` node, `bhi260@1`, 16 MHz | ✅ devicetree (move to board) |
| `src/button.c:23` | `#define BTN_PIN 2` | ⚠ duplicates DT — should read DT instead |
| `src/ble_config.c:368-380` | BQ CD commented/written as **P0.25** | ❌ **wrong on N1** (P0.25 absent; CD = P0.08) |
| `src/bhy2/bhy2_zephyr.c:363-371` | **raw `PIN_CNF` block**, `cnf[18]=0x301` = BHI reset | ❌ **dangerous on N1** (P0.18 = module RESET_N) |
| `bq_probe()` (same file) | writes BQ reg `LDO_CTRL <- 0xA8` (`enable1V8LDO()`) | ❌ pointless on N1 (LS/LDO unused) |

**That is the entire non-portable surface of 9,122 lines** — 4 items, 2 of them in one forensic probe
function (`bhy2_bitbang_probe`) that is a *debug artifact* from the M1 bring-up saga and should not ship
to N1 at all.

Everything else in the firmware — BLE/GATT, OTA/mcumgr, RawRunStore/SGL log, recorder state machine,
bitpacking, ring buffer, LED state, power state, pull protocol — is **board-agnostic already**. That is the
payoff of the V-cycle discipline.

---

## 2. The mechanism: board target, not `#ifdef`

**Your instinct is right, but macros are the wrong tool — and you already have the better one.**

`tools/zbuild.bat` takes the board as an argument:
```
zbuild.bat <app-dir> <board> <build-dir> [overlay]
buildfw.bat:  ... C:\sgc\apps\03_pull_proto arduino_nicla_sense_me ...
```

So the "one declaration" you're asking for is **one word on the build line**:

```bat
buildfw.bat  →  BOARD = arduino_nicla_sense_me   (P0, today)
buildfw.bat  →  BOARD = sgc_n1                   (N1, new board dir)
```

`-DBOARD=sgc_n1` is literally a single switch that produces two firmwares. Pins then live in the board
devicetree (`boards/sgc_n1/*.dts` + `.dtsi`), which is exactly what Zephyr created the board abstraction
for. **No source change at all** for anything pin-related — the three DT items above vanish into the board
file.

**Where a real compile-time conditional *is* justified** — only because these are register pokes and BQ
register writes that cannot be expressed in devicetree:

```c
/* one Kconfig symbol, three uses, that's all */
CONFIG_SGC_BOARD="n1"          // prj.conf / board defconfig
```
```c
#if IS_ENABLED(CONFIG_BOARD_SGC_N1)
    cnf[18] = 0x0;   /* BHI reset is P0.13 on N1; P0.18 is our own RESET_N — never drive it */
    cnf[13] = 0x301;
#else
    cnf[18] = 0x301; /* Nicla: mbed core drives BHI reset released */
#endif
```

My recommendation: **delete `bhy2_bitbang_probe()` for N1** rather than conditionalise it. It exists to
excavate a bug that is already solved (BHI on Zephyr works since M1). Shipping it to a board where
`cnf[18]` drives our own reset line is asking for a mystery-brick.

---

## 3. Likelihood of problems — honest, per risk

| # | Risk | Likelihood | Why | Cost if it bites |
|---|---|---|---|---|
| R-a | **Pin map errors** (wrong GPIO in board file) | 🟡 Medium | 33 signals to transcribe; three already moved (C1/C2/C3) | 1 hour each, caught at bring-up LED/button test |
| R-b | **`cnf[18]=0x301` drives RESET_N** | 🔴 **High if probe kept** | P0.18 *is* RESET_N on B402. Writing output-HIGH to it → reset held/glitched | Brick-loop; `netdiff` + code review catches it. **Delete the probe** |
| R-c | **BHI260AP bring-up regression** | 🟡 Medium | This exact block consumed weeks on Nicla (PIN_CNF/bitbang saga). Same SPI, same CS pin, same cluster wiring copied verbatim → physics identical, but any 100 ns timing difference re-opens it | Days, *if* it goes wrong. Mitigated by verbatim cluster copy + keeping CS/SCK/MOSI/MISO identical |
| R-d | **32 kHz clock** | 🟢 Low | Nicla fits Y1 SIT1532AI → LFXO. **N1 adds Y1 (gap G3)** → same config | Zero, *provided* G3 lands. If Y1 is ever omitted → LFRC Kconfig change |
| R-e | **BQ LDO / VDDIO_EXT** | 🟢 Low | Nicla has software-programmable VDDIO_EXT for the headers; N1 drops it (§C4, LSCTRL→GND). `LDO_CTRL <- 0xA8` becomes a no-op | Write returns ACK, does nothing. Harmless |
| R-f | **Flash size / OTA partitions** | 🟢 Low | nRF52833 = 512 KB flash = same as 52832. MCUboot layout unchanged | Zero |
| R-g | **RAM** | 🟢 **Improvement** | 128 KB vs 64 KB — we were at 7.3 KB free. OTA reassembly (+11 KB) becomes *possible* | Positive |
| R-h | **Console/UART** | 🟢 Low | Both use TX P0.20 / RX P0.09 (Nicla LPIO1/LPIO2 parity) | Zero |
| R-i | **New sensors appear** (BME688, BMM150, MAX17048, Y1) | 🟢 Low, additive | All new code is *guarded by presence*; existing paths untouched | Additive only |
| R-j | **Rev B SAMD11 bridge** | 🟢 Low | Omitted rev A (your OK). If added later: USB CDC console + programming path changes | Scoped separately |

**Overall verdict: low-to-moderate.** The one item that could genuinely hurt (R-b) is a debug function we
should delete, not port. The one that could cost days (R-c) is mitigated by the fact that we are copying
the sensor cluster *verbatim* — deliberately, which is precisely why your "clone, don't reinterpret"
doctrine is the low-risk path.

**Biggest real risk is not in this list:** it is *believing* the board file without verifying. Mitigation is
a defined N1 bring-up gate — LED → button → console → flash JEDEC → BHI → sensors, each a pass/fail line,
exactly like the M0/M1 gates that worked.

---

## 4. Work plan (small, ordered, each step verifiable)

1. **Create `boards/sgc_n1/`** — board yaml, `.dts`/`.dtsi`, `Kconfig.defconfig`, pinmap header. Transcribe
   the 33 signals from `sgc_schematic_spec.md` §3.2. **No app source touched.**
2. **Move the 3 DT items** (2× cs-gpios, button, `&spi2`) from `app.overlay` into the board devicetree.
   Build for Nicla → must still pass the existing CORE suite (regression proof).
3. **Kill/guard the two hazards:** remove `bhy2_bitbang_probe()` for N1; make BQ CD pin DT-driven instead of
   the `P0.25` comment/literal.
4. **`#define BTN_PIN 2` → DT lookup** (removes the last duplicate pin).
5. **Build both targets in CI-ish fashion**: `zbuild.bat <app> arduino_nicla_sense_me` and
   `zbuild.bat <app> sgc_n1` must both link green before any N1 board exists. This proves the "one switch"
   promise *before* hardware is in hand.
6. When N1 rev A comes back: run the bring-up gate above.

**What I will NOT do:** litter the 9,122 lines with `#ifdef`. Two targets, one board argument, three guarded
register-level spots — everything else stays single-source.

---

## 5. Answer to "will we need to change the code?"

**Yes — 3 spots, ~15 lines total**, plus one new board directory. Not a port; a pin remap.

The reason it is this small is that we already respect the Nicla pin map exactly. The migration is cheap
**because** you chose the clone strategy — the further the board drifts from Nicla, the more firmware forks.
That is a strong argument for closing the G1–G5 parity gaps rather than "improving" them.
