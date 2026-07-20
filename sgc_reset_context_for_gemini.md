# SGC Flash Data Retention After Reset — Context for Gemini

## Hardware

- **MCU:** nRF52832 (Cortex-M4, 64KB RAM, 512KB flash) on Nicla Sense ME board
- **External flash:** Macronix MX25R1635F (2MB SPI NOR, 4KB sectors, 256B pages)
- **Flash connection:** SPI1 — MOSI=p4, MISO=p5, SCK=p3, CS=p26
- **Power:** LiPo battery (~3.7-4.2V), no external pull-up on CS pin
- **Production PCB:** Not yet fabbed — currently on Nicla dev board. Will add pull-up on CS in next rev, but need software workaround NOW.

## Software Stack

- **Framework:** Arduino-mbed (mbed OS 6.17, Nordic nRF52 Arduino core)
- **Filesystem:** LittleFS v2 via mbed `LittleFileSystem2` class
- **Block device:** `mbed::SPIFBlockDevice` + `mbed::SlicingBlockDevice` (sectors 4-511 for LittleFS, sectors 0-3 for raw flash ring buffer)
- **LittleFS config:** block_size=4096, block_cycles=500, cache_size=256, lookahead_size=64
- **File format:** Each run = one file `/run_NNNNN.dat`:
  - 16-byte header (format_ver, side, timestamp, data_size, frame_count, cal_accuracy)
  - compressed IMU frames (variable length, ~5-20 bytes/frame at 100Hz)
  - 6-byte CRC32 trailer [0xC3][0x32][CRC32 LE 4B]

## The Problem

**After any reboot (serial `!` command → `NVIC_SystemReset()`), LittleFS mount fails and ALL stored runs are lost.**

This is the ONLY reason we have flash — to retain ski run data across power cycles.

### What happens on reboot

1. Run is saved: `close_run()` → flush buffer → write CRC32 trailer → sync → close file
2. User sends `!` — reboot handler: `metadata_sync()` (creates+deletes temp file) → `delay(800ms)` → `NVIC_SystemReset()`
3. Device reboots, `setup()` runs, `LittleFS::begin()` calls `fs->mount(sliced)`
4. Mount fails → auto-reformat (wipes everything) → mount succeeds → scan shows 0 runs

### Error patterns observed

| What we tried | Mount error | Meaning |
|---|---|---|
| Original code (call `unmount()` before reset) | `-138` | LFS2_ERR_CORRUPT — superblock exists but CRC mismatch |
| Remove `unmount()`, only `metadata_sync()` | `-138` | Same — superblock corrupted |
| Bit-bang MX25R deep power-down (0xB9) before reset | `-22` | EINVAL — no superblock found at all |
| Drive CS HIGH via `digitalWrite(26, HIGH)` before reset | TBD (testing now) | |

### What we've confirmed is NOT the problem

- **Filesystem-level data loss:** `close_run()` completes successfully (S03 test passes, run shows in `runs:1` before reboot). Data IS written to flash.
- **close_run() COW cascade:** Fixed in V2.80 — removed `seek(0)+write(header)` that triggered LittleFS copy-on-write of entire file. Now append-only.
- **Double-buffering data loss:** Fixed — `f->sync()` after every `flush_write_buf()`.
- **BLE current spikes during logging:** Fixed — `BLE.stopAdvertise()` during LOGGING.
- **mbed `unmount()` not syncing:** Confirmed — `lfs_rawunmount()` frees caches without sync. Removed unmount call entirely.

### Our current hypothesis

The **nRF52832 GPIO pins float during `NVIC_SystemReset()`**. Per nRF52832 PS v1.8 §6.9: "During system reset, all GPIO pins are configured as inputs with input buffer disconnected."

CS (P0.26) can glitch LOW during the reset transition. The MX25R interprets this as the start of a valid SPI command. Combined with noise on SCK/MOSI, this can trigger:
- **Sector erase** → superblock erased → `-22` (EINVAL)
- **Page program** → partial write over superblock → `-138` (CORRUPT)

The only known reliable fix is an external 10kΩ pull-up resistor on CS to VDD. But the production PCB hasn't been fabricated yet — we need a software workaround for the Nicla dev board.

### Key constraints

- RAM: 63.6% used (40.9KB/64.3KB) — limited headroom
- Flash: 66.1% used (349KB/528KB)
- No access to raw LittleFS `lfs_t` struct from mbed abstraction
- Cannot add external pull-up resistor to Nicla dev board (CS is on an internal pin)
- Must work with `NVIC_SystemReset()`

### What we need from you

A **software-only** way to ensure the MX25R flash survives nRF52832 `NVIC_SystemReset()` without corruption. Options we've considered:

1. MX25R deep power-down (0xB9) before reset — tried with bit-bang SPI, got `-22`. Maybe the bit-banging conflicted with mbed's SPI driver?
2. JESD252 software reset (0x66, 0x99) — haven't tried, sends flash to power-on state
3. MX25R write-disable before reset — send 0x04 to prevent any write/erase
4. Some nRF52832 register we haven't found that holds GPIO state during reset
5. LittleFS config change we're missing (different mount options, recovery flags)

**Please help us find a working software solution.**
