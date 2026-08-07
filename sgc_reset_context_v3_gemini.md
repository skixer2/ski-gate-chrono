> **SUPERSEDED for production storage (2026-08-07):** Run payloads no longer use LittleFS. Opt-A `RawRunStore` (v4.63+) eliminates LFS mount/-138 payload path. This document remains useful history of the LittleFS retention investigation. Current map/ADR: `adr_003_littlefs_storage.md`, AD-015, MEMORY.md 2026-08-07.

# SGC Flash Data Retention After Reset — Gemini Context v3

## The Problem (1 sentence)

**After any CPU reset (NVIC_SystemReset), LittleFS mount fails with -138 (LFS2_ERR_CORRUPT), and ALL stored data is lost.**

This happens on EVERY reset — factory reset, soft reboot, any path that calls NVIC_SystemReset(). Even with ZERO operations between data sync and reset.

---

## Hardware

- **MCU:** nRF52832 (Cortex-M4, 64KB RAM, 512KB internal flash)
- **Board:** Arduino Nicla Sense ME (dev board — no custom PCB yet)
- **External flash:** Macronix MX25R1635F, 2MB SPI NOR
- **Flash connection:** SPI1 — MOSI=p4, MISO=p5, SCK=p3, CS=p26
- **Power:** LiPo battery (~3.7-4.2V), no external pull-up on CS
- **Reset type:** NVIC_SystemReset() (soft system reset, not power-on)

---

## Software Stack

- **Framework:** Arduino-mbed (mbed OS 6.17, Nordic nRF52 Arduino core)
- **Filesystem:** LittleFS v2 via `mbed::LittleFileSystem2` class
- **Block device:** `mbed::SPIFBlockDevice` + `mbed::SlicingBlockDevice`
- **Flash layout:**
  - Sectors 0-3 (0x0000-0x3FFF): Raw flash ring buffer (pre-start IMU data)
  - Sectors 4-511 (0x4000-0x1FFFFF): LittleFS via SlicingBlockDevice(0x4000, 0x1FC000)
- **LittleFS config:** block_size=4096, block_cycles=500, cache_size=256, lookahead_size=64

---

## What We've Tried (chronological)

| Version | Change | Result |
|---------|--------|--------|
| V2.77 | Original code: `unmount()` + `delay(800)` before reset | -138 every boot |
| V2.80 | Removed seek(0)+write(header) from close_run (append-only) | Still -138 |
| V2.80 | Added f->sync() after each flush_write_buf | Still -138 |
| V2.80 | Added BLE.stopAdvertise() during LOGGING | Still -138 |
| V2.80 | Added POFCON 2.5V early warning (REVERTED — caused boot freeze) | Hung |
| V2.80 | Added auto-reformat on mount failure | -138 → auto-reformat → data wiped |
| V2.81 | metadata_sync() before reset, no unmount() | Still -138 |
| V2.81 | 500ms boot delay for terminal | Still -138 |
| V2.82 | Bit-bang MX25R deep power-down (0xB9) before reset | -22 (EINVAL — no superblock) |
| V2.83 | Drive CS HIGH before reset (no bit-bang) | Still -138 |
| V2.84 | Write Disable (0x04) before reset | Still -138 |
| V2.84 | Removed auto-reformat, added mount retry (3x, 200ms) | Still -138 |
| V2.85 | Block Protect bits (0x06+0x01 0x3C) before reset + unlock at boot | Still -138 |
| V2.86 | Bumped BP write delay 10ms→50ms, 100ms boot stabilization | Still -138 |
| V2.87 | **STRIPPED EVERYTHING** — zero pre-reset manipulation | **Still -138** |

---

## Key Evidence

### 1. Mount failure on EVERY boot
```
{"ev":"fs_mount_fail","err":-138,"blk":504,"sz":2064384,"es":4096}
```
- `-138` = LFS2_ERR_CORRUPT in LittleFS
- This means the superblock exists but CRC check fails
- Not a format issue (would be -22 EINVAL), not a hardware issue (would be -5 EIO)
- The superblock block IS found but its content is corrupted

### 2. Data IS written successfully before reset
After saving a run, status shows `runs:1,total_runs:1`. The S03 system test passes:
```
✓ SUCCESS: id=0 frames=1063 compressed=11042 bytes
```

### 3. Factory reset also fails
Even after `erase_all()` (erases sectors 4-19, reformats, mounts fresh), the next boot fails with -138:
```
{"ev":"factory_reset"}
{"ev":"reboot"}
{"ev":"boot","ver":"2.87","rr":4}
...
{"ev":"fs_mount_fail","err":-138,...}
```

### 4. Reset reason is always SREQ
`"rr":4` = soft reset (NVIC_SystemReset). Not BOR, not pin reset, not watchdog.

### 5. SPIFlash self-test PASSES
`{"ev":"init","sub":"flash","ok":1}` — the SPI flash hardware is working. It erases+writes+reads sector 0 successfully.

---

## Critical Code

### setup() — boot sequence (main.cpp)
```cpp
void setup() {
    nicla::begin();
    Serial.begin(115200);
    delay(500);   // terminal connection delay

    // Print boot JSON
    Serial.print("{\"ev\":\"boot\",\"ver\":\"");
    Serial.print(FW_VERSION);
    Serial.print("\",\"rr\":");
    Serial.print(NRF_POWER->RESETREAS);
    Serial.println("}");

    g_led.begin();

    // SPIFlash init — self_test passes (erases+writes+reads sector 0)
    g_flash.begin();  // OK

    // Flash ring reset
    g_ring.reset();

    // LDC1612 init — OK
    g_ldc.begin();

    // BLE init — OK
    BLE.begin();

    // ⬇ LITTLEFS MOUNT — FAILS with -138 ⬇
    g_fs.begin();  // Calls LittleFSStorage::begin() below
}
```

### LittleFSStorage::begin() — mount logic (littlefs_storage.cpp)
```cpp
bool LittleFSStorage::begin() {
    extern SPIFlash g_flash;
    m_bd = g_flash.get_bd();  // mbed::SPIFBlockDevice*

    // Slice sectors 4–507 for LittleFS
    auto* sliced = new SlicingBlockDevice(raw, 0x4000, 0x1FC000);

    // Create LittleFS with standard config
    auto* fs = new LittleFileSystem2("littlefs", NULL, 4096, 500, 256, 64);

    // ⬇ THIS FAILS ⬇
    int err = fs->mount(sliced);  // Returns -138 (CORRUPT)

    if (err != 0) {
        // Retry 3x with 200ms delays
        for (int attempt = 1; attempt <= 3; attempt++) {
            delay(200);
            err = fs->mount(sliced);
            if (err == 0) break;
        }
        if (err != 0) {
            // No auto-reformat. Return false → storage offline.
            delete fs; delete sliced;
            return false;
        }
    }
    m_fs = fs;
    scan_runs();
    return true;
}
```

### Reboot handler (main.cpp — V2.87, stripped)
```cpp
case '!':
    json_begin(); json_kv("ev", "reboot"); json_end();
    Serial.flush();
    delay(200);
    NVIC_SystemReset();  // ⬇ CORRUPTS SUPERBLOCK ⬇
    return;
```

### Factory reset (main.cpp)
```cpp
case 'R':
    json_begin(); json_kv("ev", "factory_reset"); json_end();
    Serial.flush();
    g_fs.erase_all();  // erases sectors 4-19, reformats, mounts
    json_begin(); json_kv("ev", "reboot"); json_end();
    delay(200);
    NVIC_SystemReset();
    return;
```

### erase_all() — reformat (littlefs_storage.cpp)
```cpp
void LittleFSStorage::erase_all() {
    auto* fs = static_cast<LittleFileSystem2*>(m_fs);
    if (fs && m_bd) {
        auto* raw = static_cast<mbed::BlockDevice*>(m_bd);
        fs->unmount();    // calls lfs_unmount → deinits BD
        delete fs;
        m_fs = nullptr;

        // Erase sectors 4-19 (0x4000-0x13FFF) — 64KB
        for (uint32_t addr = 0x4000; addr < 0x14000; addr += 4096)
            raw->erase(addr, 4096);

        delay(500);

        // Create fresh FS, reformat, mount
        auto* fs2 = new LittleFileSystem2(..., 4096, 500, 256, 64);
        auto* sliced = new SlicingBlockDevice(raw, 0x4000, 0x1FC000);
        fs2->reformat(sliced);   // writes fresh superblocks
        fs2->mount(sliced);      // ⬅ sometimes fails -12 (ENOMEM)!
        m_fs = fs2;
    }
    m_entry_count = 0; m_next_run_id = 0;
}
```

### close_run() — data write (littlefs_storage.cpp)
```cpp
uint16_t LittleFSStorage::close_run(uint32_t frame_count) {
    auto* f = static_cast<File*>(m_file);

    flush_write_buf();           // flush app buffer + sync
    f->write(trailer, 6);        // write CRC32 trailer
    f->sync();                   // commit to flash
    f->close();                  // close file (also syncs)

    // ⬆ NO seek(0)+write(header) — append-only format
    // File is committed to flash at this point

    // ... populate RAM entry ...
    return run_id;
}
```

### flush_write_buf() — with sync
```cpp
void LittleFSStorage::flush_write_buf() {
    if (m_write_buf_pos == 0) return;
    auto* f = static_cast<File*>(m_file);
    f->write(m_write_buf, m_write_buf_pos);
    f->sync();  // commit LittleFS cache to flash
    m_write_buf_pos = 0;
}
```

### SPIFlash::begin() — SPI init + self-test (spi_flash.cpp)
```cpp
bool SPIFlash::begin() {
    m_bd = new SPIFBlockDevice(p4, p5, p3, p26);  // SPI1
    if (!m_bd) return false;
    static_cast<mbed::BlockDevice*>(m_bd)->init();
    m_ok = true;
    return self_test();  // erase+write+read sector 0 (0x0000-0x00FF)
}
```

---

## Our Current Hypothesis

The nRF52832 GPIO pins float during `NVIC_SystemReset()`. Per nRF52832 PS v1.8 §6.9:
> "During system reset, all GPIO pins are configured as inputs with input buffer disconnected."

CS (P0.26) can glitch LOW during the reset transition. The MX25R interprets CS LOW + noise on SCK/MOSI as a valid SPI command — potentially a sector erase or page program. This corrupts the superblock sector (at physical address 0x4000).

But we've tried EVERY software workaround (deep sleep, write disable, block protect bits, CS hold) and NOTHING works. The V2.87 test proves that even with ZERO flash manipulation between close and reset, the superblock gets corrupted.

---

## Key Question for Gemini

Given that:
1. NVIC_SystemReset() unavoidably floats CS (no software fix possible)
2. External pull-up resistor on CS would solve it but requires PCB change (not available on Nicla dev board)
3. We've ruled out LittleFS sync issues (close_run already syncs)
4. mbed SPIFBlockDevice doesn't expose raw SPI for sending pre-reset commands

**Is there ANY way to make this work on the Nicla dev board without modifying hardware?**

Alternative questions:
- Is there an nRF52832 register or peripheral setting we haven't tried that prevents GPIO float during NVIC_SystemReset?
- Would watchdog reset or System OFF wake behave differently for GPIO retention?
- Does mbed's SPIFBlockDevice have a quirk or known issue with init/deinit that could be worked around?
- Could we avoid NVIC_SystemReset entirely and do a "soft reboot" by reinitializing everything in software?
- Could the SlicingBlockDevice wrapping be causing the mount failure (not the flash itself)?
