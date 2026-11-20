# SGC app-03 `pull_proto` — product console + pull protocol

Current version: **0.22.1** (`src/main.c` → `APP_VERSION`; bump on every change).

**State machine diagram** (FW 0.23.x sleep era): [`system_design/diagrams/device_state_machine.html`](../../../../diagrams/device_state_machine.html) — SYSTEM_OFF / SLEEP / CONNECTED / ARMED / LOGGING / POST_RUN with the LED language.

Product console service over BLE, wire-compatible with the phone app (App ≥ 1.46):

| Handle | Purpose |
|---|---|
| 0xABC8 | Run info: `count[2 LE] + age[4]` (read) |
| 0xABC9 | Run list: `[{"id":N,"ts":N,"size":PAYLOAD,"side":"l|r"}]` |
| 0xABCA | Console: phone writes ASCII commands, device NOTIFYs text lines / binary frames back |

⚠ These are FULL 128-bit UUIDs (`5347ABCX-…`), not BT UUID-16 forms — the app resolves by string.

## Command set (0xABCA console or serial UART)

Wire ids are **stable run_ids** since v0.22.1: listing `id`, `D` `run`, and the `s`/`H`/`d` arguments all address the monotonic run_id assigned at record time. Deletes leave holes; ids never shift.

| Cmd | Args | Action | Replies |
|---|---|---|---|
| `D` | — | Directory: one `{"run":id,"ts":…,"sz":…,…}` line per run + `{"d_end":n}` | `E 4` if a transfer is active |
| `s` | `<id> <off> <len>` | Stream payload: frames `[seq:2 LE][len:1][payload ≤240 B]` paced 25 ms, END marker `FF FF 00` | `E 1` unknown id · `E 2` bad off/len · `E 3` malformed · `E 4` busy |
| `d` | `<id>` | **Delete run (F46, v0.22.0+):** index-only removal, lazy sector reclaim; idempotent | `OK deleted N` · `OK already gone` · `E 5` recording · `E 4` transfer · `E 3` malformed (digits only) |
| `H` | `<id>` | Hexdump one run's payload (24 B/line, JSON events; IDLE only, serial) | `rh_begin`/`rh`/`rh_end` · `rh_err bad_id` |
| `a` / `g` / `e` | — | Recorder arm / go / end (bench triggers) | `OK rec` |
| `b` | `<bytes>` | Set active run budget (SL 90 KB / GS-SG 120 / DH 150 / custom 60–600; app-side discipline mapping) | `OK budget N B` · `E 3`/`E 5` |
| `S` | `<secs> <gate_interval_s> [a\|r\|l]` | BENCH (`CONFIG_SGC_BENCH`): synthetic run through the real pipeline; ground truth printed on serial | `E 3` out of range · `E 5` busy / non-bench build |
| `w` | `<len>` | BENCH: raw storage-lifecycle writer (budget/rotation/wrap testing, not sensor frames) | `E 3`/`E 5` |
| `x` / `X` | — | Wipe index + reboot log (serial: two-step confirm — `x` arms, `X` within 3 s executes; BLE: single-step, phone never wipes) | `confirm: X within 3 s` · `OK wiped` |
| `B` | — | Re-arm the BHI bit-bang probe + reboot into it | — |

Unknown command → `E 4`. Error codes: `1` bad run · `2` bad off/len · `3` malformed · `4` unknown/busy · `5` busy/out of policy.

## Bench notes

- **`b1_rate` telemetry** is the `CONFIG_SGC_BENCH` 1 Hz stats thread (`sensor_prod.c`, own low-priority thread — never printk from drain ctx): `{"ev":"b1_rate","rv":…,"lacc":…,"baro":…}`. Bench builds only.
- **`?` is not a command** — there is no status query; unknown chars just answer `E 4`. Capture scripts must not send `?`.
- Ground truth for `S` runs (pole-impact times/amplitudes, turn schedule) prints on serial as it is placed — seed fixed → reproducible.
- Serial doctrine: one data treatment path (FWR-1/SR-5) — serial and BLE console bytes land in the same capture → `cmd_work` handler.

## Memory budget (measured, 0.25.19 / `b02519c`)

Two cores, and it is easy to attribute the numbers to the wrong one — the build log
prints the **app first** (line ~432) and the **MCUboot core second** (line ~696),
because sysbuild builds the app then the bootloader.

| Core | FLASH | RAM | Headroom |
|---|---|---|---|
| **app** (`03_pull_proto`) | 168,744 B / 188,080 B (89.72%) | **58,380 B / 64 KB (89.08%)** | ~5.9 KB RAM |
| **MCUboot** | 40,952 B / 64 KB (62.49%) | 15,744 B / 64 KB (24.02%) | ~48 KB RAM |

Cross-check: `zephyr.bin` sizes are 168,744 B and 40,952 B; the app map's FLASH origin
is `0x00010000` (slot after the bootloader) while MCUboot's is `0x00000000`.

**The app is the constrained core, not MCUboot.** Marketed "MCUboot eats 89% RAM" is
wrong — that figure belongs to the app.

Where the app's RAM goes (`zephyr.map`, sections >=512 B):

- `store_flash.c` **10,264 B**: `.bss.save` 4,096 + `.bss.buf` 4,096 (double-buffered
  flash staging) + `.bss.page` 1,048 + `.bss.entries` 1,024
- SoftDevice controller `.bss` 1,916 B, MPSL 554 B
- Stacks: MCUmgr 2,048 + system workqueue 2,048 + ISR 2,048 + BT HCI TX 1,536, plus
  several 1 KB BT/MPSL stacks
- `CONFIG_WFA_QT_THREAD_STACK_SIZE=5200` is a Kconfig default with **no WFA subsystem in
  this image** — a ~5 KB reclaim candidate, but it is a vendor default: change it only
  with a measurement.

The pre-roll ring is **not** in RAM: 4,096 slots x 20 B live in flash at
`0x0000-0x13FFF`. That is what allows a 30 s window on a 64 KB part.

Why MCUboot needs 15,744 B: `noinit` 12,416 (boot state that must survive its own
resets, e.g. swap status), `CONFIG_MAIN_STACK_SIZE=10240`, `bss` 2,873, `datas` 272.
Config: `CONFIG_BOOT_SWAP_USING_MOVE=y` (no scratch partition — swap-move instead),
`CONFIG_BOOT_MAX_IMG_SECTORS_AUTO=y`, `CONFIG_HEAP_MEM_POOL_SIZE=0`. So the cost is the
**main stack + persistent boot state**, not OTA algorithm buffers.

---

## Source map (`src/`)

`pull_proto.c` (console + D/s/d protocol) · `main.c` (version, boot) · `recorder.c` (arm/go/end state machine, run stats) · `store_flash.c` / `store_layout.h` / `store.h` (index + data sectors, run_id addressing) · `ring.c` · `sensor_prod.c` (BHY2 producer + bench stats thread) · `sensor_synth.c` (`S` generator) · `bitpacker.c` (24 B frames) · `ble_config.c` / `ble_lifecycle.c` (SG service, advertising) · `button.c` / `led_state.c` (triggers, RGB) · `bhy2/` (BHI260AP driver).

Tests: `tests/` (hosted unit tests for store/bitpacker/protocol parsing).
