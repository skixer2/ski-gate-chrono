# SGC Zephyr Dev Tools (JP-PC)

Firmware platform: **Zephyr OS + nRF Connect SDK v3.4.0** (see
[`../../ZEPHYR_PORT_PLAN.md`](../../ZEPHYR_PORT_PLAN.md)).
Target: Arduino Nicla Sense ME (nRF52832), flashed via on-board CMSIS-DAP (SWD).

## The toolchain (VS Code-managed — do not self-install)

The tools are **PC-independent**: paths are auto-discovered, not hardcoded.

| Piece | Discovery order (zbuild/zflash/zserial) |
|---|---|
| SDK + toolchain | `%NCS_DIR%` env var, else `C:\ncs` — any version dir under `toolchains\` with `environment.json` |
| OpenOCD | `%USERPROFILE%\.platformio\packages\tool-openocd` first, then `openocd` on PATH |
| Serial port | arg to `zserial.py` (default COM3) |
| VS Code extension | nRF Connect pack (bundles nrfutil-device; sees the Nicla) |

Current JP-PC layout (for reference): SDK `C:\ncs\v3.4.0`, toolchain `dcbdc366a1`,
OpenOCD = PlatformIO xPack 0.12.0.

## Daily loop

```
edit -> Build in VS Code (nRF Connect: F7)  OR  zbuild.bat
     -> zflash.bat <build>\zephyr\zephyr.hex
     -> zserial.py COM3 10        (optional: watch boot log)
```

## Commands

| Command | What it does |
|---|---|
| `zbuild.bat <src> <board> <builddir> [overlay]` | west build with full NCS env (e.g. board `arduino_nicla_sense_me`) |
| `zflash.bat <hex>` | SWD flash + verify + reset run. Bootloader-independent. |
| `zreset.bat` | Hard reset via SWD — works even if firmware is wedged. No serial port needed. |
| `zserial.py [port] [secs]` | SWD reset + capture console output (default COM3, 5 s). |
| `zpeek.bat` | Memory dump via SWD — edit the `mdw` line for your address. |

## Hardware notes (measured 2026-09-18)

- SWD probe: on-board "Nicla Sense CMSIS-DAP", serial `0AFB5B3B`, console on **COM3 @ 115200**
- Chip: nRF52832-CIAA, 512 KB flash / 64 KB RAM
- Hardware reset pin = **P0.00** (UICR PSELRESET[0/1]=0x00000000, Arduino factory setting)
- User button = **P0.21**, plain GPIO (`sw0` in Zephyr board files) — not a hard reset
- Plain Zephyr builds link at 0x0 → the old Arduino bootloader is overwritten by zflash
  (SWD makes this safe; bootloader hex can be restored the same way if ever needed)
- VS Code "Flash" button does NOT work on this board (pyocd runner not installed) — use zflash.bat
