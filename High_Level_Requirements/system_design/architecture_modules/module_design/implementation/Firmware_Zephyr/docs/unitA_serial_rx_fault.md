# Unit A — USB serial command channel dead (RX only): RESOLVED as hardware fault

**Status: closed.** Confirmed single-ended UART RX line fault. No further serial-RX
debugging is warranted; the remaining work is a workaround for the BHI260 restore.

- Report date: 2026-11-20
- Unit A: SWD probe `0AFB5B3B`, port COM3 -> later COM7
- Unit B (healthy control): SWD probe `00C48A96`, port COM4, BLE `CA:9C:0B:D6:BE:9F` ("SGC JP 01")
- Firmware at diagnosis: 0.25.11 / 0.25.12 / 0.25.13 (all reproduce), 0.25.13-bd7f228 on B

---

## 1. Architecture (corrected — the original brief was wrong)

The **nRF52832 has no USB device controller.** USB CDC-ACM and CMSIS-DAP are
implemented by the onboard **SAMD11 interface MCU**; the two chips are linked by
ordinary traces through a level translator.

Evidence in-repo (`apps/03_pull_proto/prj.conf`):

```
# the console UART has RX on P0.9 (board pinctrl) — the DAPLink bridge
# forwards PC->board.
```

Consequently SWD, BLE GATT and printk TX keep working while PC->device serial RX is
dead: they do not share the broken conductor. Any nRF52 USBD / EPSTATUS hypothesis is
**void** — there is no such peripheral on this part.

Measured pin assignment (UARTE0 PSEL, both units identical):

| Field | Register | Unit A | Unit B |
|---|---|---|---|
| PSEL.RXD | 0x40002514 | 9 (P0.09) | 9 (P0.09) |
| PSEL.TXD | 0x4000251C | 10 (P0.10) | 10 (P0.10) |

> NOTE: an external recap stated TX = P0.20. The hardware reads **P0.10** on both
> units. If P0.20 is what the SAMD11 drives, then this is a second open question —
> testable by generating console output and sampling P0.10 vs P0.20. Unresolved.

## 2. Symptom

- Device -> PC: perfect. 1 Hz `printk` telemetry, ~1566 B / 8 s.
- PC -> device: **zero bytes**, for every command (`Q`, `D`, `b`, `B`, `F*`).
- Boot log still prints `serial: rx isr armed` (arming an interrupt does not verify
  electrical continuity).
- BLE on A is alive: the phone app reads version and runs over GATT while RX is dead.
- Unit B answers `E 4` on demand from the same PC, same script, same minute.

## 3. Measurements (all SWD, read-only)

Method note: **halt the running app** (`halt`, pc ~0x0002f76a). Do **not** use
`reset halt` — that drops into the MCUboot DFU-wait loop (pc 0x2688) where UARTE is
unconfigured and every read is meaningless.

| Measurement | Unit A | Unit B | Conclusion |
|---|---|---|---|
| PSEL.RXD | 9 (P0.09) | 9 (P0.09) | pinmux identical -> not a devicetree error |
| UARTE0 ENABLE (0x40002500) | 0x08 | 0x08 | enabled on both |
| GPIO0.IN (0x50000510) bit 9 | **0** | **1** | A's line is low; healthy idle is high |
| bit 9 while streaming 0x55 | stayed 0 (5 samples) | — | line never toggles |
| bit 9 with pull-up `PIN_CNF=0x0e` | **still 0** | — | pull-up cannot lift an actively driven line |

**The pull-up test is the decisive one.** An internal pull-up (~13 kOhm) was engaged
and its readback confirmed (`0x0e`), yet the input stayed 0. A floating or
misconfigured pin would have gone high. Something external is clamping P0.09 low.

`ERRORSRC` (0x40002480) reads `0x0c` on **both** units — sticky, non-diagnostic.
Do not cite it as evidence.

### What was NOT measured here

The external recap also lists an unpowered RX->GND resistance (5 MOhm) and a powered
pad voltage (0.4 V on A vs ~1.8-3.3 V on B). Those are **good, discriminating numbers**
but they were not taken in this session (no meter was in the loop). Treat them as
reported, not verified — worth re-measuring before any rework decision.

## 4. Ruled out

| # | Hypothesis | Result |
|---|---|---|
| 1 | Firmware regression | 0.25.11 (hex that answered that morning) still dead |
| 2 | pyserial / driver | PowerShell `System.IO.Ports` same result |
| 3 | Stray process holding the port | Killed PIDs 8704, 23268, 7036, 33832 |
| 4 | Phone app interference | App fully closed |
| 5 | USB port on the PC | Different port; re-enumerated COM3 -> COM7, still dead |
| 6 | USB cable | Different cable |
| 7 | Warm-reset artefact | SWD reset + RESUME path |
| 8 | VBUS cold-boot of the SAMD11 | Battery was **already desoldered** during the port/cable swaps, so VBUS was cycled -> closed |
| 9 | Zephyr pinmux / devicetree | PSEL identical to healthy unit |
| 10 | Host-side stale pipe | Fresh device node on a new port still dead |

## 5. Conclusion

P0.09 on Unit A is **externally held at 0 V** — not floating, not misconfigured.
Fault lies between the level shifter and the nRF52 RX pad: a short to GND, a level
shifter output stuck low, or a damaged pad. The differential pair is provably healthy
(working TX requires it); only the single-ended RX line is broken.

Unit A capability profile:

- **Healthy:** SWD flash/debug (`0AFB5B3B`), BLE GATT, USB serial TX (printk).
- **Dead:** USB serial RX -> the `F*` flash service and all console commands.

## 6. Consequence and options for the BHI260 restore (err 0x21)

The hub restore was designed to run over the serial `F` service, which is now
unreachable. Options:

1. **One-off recovery build** embedding the BHI260 firmware, flashed via SWD.
   Keeps everything on a channel that is known good.
2. **Route `F*` over BLE GATT** — a larger change, but it makes the device
   recoverable without a probe.
3. **Board rework** (U13 / ANNA-B112 P0.09). Not recommended: 0201/BGA rework on a
   Nicla-class module is high-risk for a unit that is otherwise fully usable over
   SWD + BLE.

Recommendation: option 1 first — it is bounded, needs no new BLE surface, and
restores the hub using the exact transport already proven on this unit.
