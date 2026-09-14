---
type: project-note
project: ski_gate_chrono
tags: [ski_gate_chrono]
created: 2026-07-20
---

# SGC Flash Data Retention — Root Cause Analysis & Solution

## Executive Summary

**Root cause**: nRF52832 GPIOs float during `NVIC_SystemReset()`. CS (P0.26) glitches LOW, MX25R1635F flash interprets noise as valid SPI commands, corrupting the LittleFS superblock at physical address 0x4000.

**Solution**: Put flash into Deep Power-Down (0xB9) before reset using nRF52 hardware SPI, then release from DP (0xAB) at boot before `SPIFBlockDevice::init()`. This is proven to protect the superblock (V2.82 test showed -22 instead of -138), but requires the wake-up step at boot that V2.82 was missing.

---

## 1. Root Cause Analysis

### 1.1 The Physics of the Problem

When `NVIC_SystemReset()` executes on nRF52832 (Cortex-M4):

1. **All GPIO pins become inputs with input buffer disconnected** (nRF52832 PS §6.9)
2. **Internal pull-up/pull-down resistors are disabled**
3. **Pin drivers are tri-stated (high-impedance / floating)**

This is a hardware-level behavior — no software register write can prevent it. The only way to keep a pin at a known voltage during reset is an **external** pull-up resistor on the PCB.

The Nicla Sense ME dev board has **no external pull-up on CS (P0.26)**. So during the ~1µs reset pulse:

```
Normal:   CS ────┐        ┌──── (driven HIGH by nRF52)
                  └────────┘
                  
Reset:    CS ────┐    ????    ┌──── (floating!)
                  └────────────┘
                       ↑
                 Can glitch LOW,
                 flash interprets
                 noise as command
```

### 1.2 Why the Superblock at 0x4000 Gets Corrupted

The MX25R1635F on SPI1 is continuously powered by the LiPo battery. When CS glitches LOW during reset:

1. The flash chip is **selected** (CS LOW = chip active)
2. Random noise on SCK and MOSI is interpreted as a **valid SPI opcode**
3. If the noise pattern happens to match a page program (0x02) or sector erase (0x20/0xD8) command, and the Write Enable Latch is still set from a prior operation, the flash **executes the command**
4. The superblock block (physical 0x4000-0x4FFF) gets partially or fully corrupted
5. On next boot, `lfs_mount()` reads the superblock, finds the "littlefs" magic string, but CRC check fails → **LFS2_ERR_CORRUPT (-138)**

Additional evidence that the flash Write Enable Latch (WEL) plays a role: the mbed `SPIFBlockDevice` driver issues WREN (0x06) before every erase/program and clears it after completion with WRDI (0x04). But the WEL is only cleared **after** the operation's WIP poll succeeds. If the last operation before reset was any flash write (close_run, LittleFS metadata, etc.), the WEL may have been set, the operation may have completed, and WRDI may not have been sent yet. Or the WEL could have been left set from a previous operation if the code path didn't explicitly clear it.

With WEL=1, a random 0x20 (Sector Erase) on the SPI bus is all it takes to destroy sector 4.

### 1.3 Why Sector 0 Survives

Sector 0 (0x0000-0x0FFF) is erased and rewritten during `SPIFlash::self_test()` on every boot. Any corruption is overwritten. Sectors 1-3 are erased by `FlashRing::reset()`. The self-test passes because it tests the hardware at sector 0, not the integrity of the LittleFS superblock.

### 1.4 Why the Factory Reset Also Fails

The factory reset sequence:
```
erase_all() → erase sectors 4-19 → reformat → mount OK → NVIC_SystemReset() → BOOM
```

The reformat writes fresh superblocks. The mount confirms they're valid. Then NVIC_SystemReset() corrupts them. The corruption is **in-transit** during the reset, not pre-existing.

### 1.5 The V2.82 Test: The Smoking Gun

V2.82 bit-banged 0xB9 (Deep Power-Down) before reset:
- **Result: -22 (EINVAL) instead of -138 (CORRUPT)**
- -22 means "no valid superblock found" — LittleFS couldn't read a valid magic string
- -138 means "superblock found but CRC is wrong" — the superblock was corrupted

This proves:
1. **DP protects the flash**: In DP mode, the flash ignores ALL SPI commands except 0xAB (Release DP). CS glitches during reset are harmless.
2. **The only missing piece**: V2.82 didn't release from DP at boot. `SPIFBlockDevice::init()` tried to send 0x9F (RDID), flash was still in DP and didn't respond, init failed, mount returned -22.

### 1.6 Why All Other Attempts Failed

| Attempt | Why It Failed |
|---------|--------------|
| Write Disable (0x04) | WEL cleared, but noise can re-set it (0x06 is a single byte) |
| Block Protect bits | BP bits only protect against ERASE+PROGRAM, not against other opcodes; also requires non-volatile SR write which may not persist through all reset edge cases |
| Drive CS HIGH | nRF52 stops driving ALL pins during the reset pulse itself — the pin floats regardless of prior output state |
| Dummy CS | Same issue — all GPIOs float |
| metadata_sync() | Data is already synced; corruption happens DURING reset, not before |
| Append-only format | Eliminates COW cascade during close_run, but doesn't protect against reset-time corruption |

---

## 2. The Solution: Deep Power-Down Protection with Boot-Time Release

### 2.1 Architecture

```
BEFORE RESET:
  ┌──────────────────────────────────────────────────┐
  │ 1. Flush serial                                   │
  │ 2. Send 0xB9 via nRF52 HW SPI (Enter DP)          │
  │ 3. CS HIGH → flash enters DP                      │
  │ 4. Delay 50µs                                     │
  │ 5. Disable SPI1, disconnect pins                   │
  │ 6. NVIC_SystemReset()                             │
  │    ┌──────────────────────────────────┐            │
  │    │ Flash in DP: ignores all commands │  ← SAFE   │
  │    │ CS glitches: harmless             │           │
  │    └──────────────────────────────────┘            │
  └──────────────────────────────────────────────────┘

AT BOOT:
  ┌──────────────────────────────────────────────────┐
  │ SPIFlash::begin():                                │
  │ 1. Send 0xAB via nRF52 HW SPI (Release DP)        │
  │ 2. CS HIGH → flash exits DP                       │
  │ 3. Delay 50µs (tDPDD = 35µs max)                  │
  │ 4. Disable SPI1, disconnect pins                   │
  │ 5. Create SPIFBlockDevice → init() → RDID succeeds │
  │ 6. Self-test passes                                │
  └──────────────────────────────────────────────────┘
```

### 2.2 Why Hardware SPI (Not Bit-Bang)

V2.82 used bit-banged SPI, which is:
- Unreliable at speed (GPIO toggling through Arduino `digitalWrite()` is ~100kHz)
- Subject to timing variance
- The Nicla uses `digitalWrite()` through mbed, which adds significant overhead

Using nRF52832's hardware SPI1 peripheral:
- Deterministic timing at any speed
- Hardware-controlled CS, SCK, MOSI
- Same peripheral that SPIFBlockDevice uses, so we know the pins are correct

### 2.3 Why 0xAB at Boot Is Harmless Even If Flash Is NOT in DP

From the MX25R1635F datasheet:
- **In DP mode**: 0xAB = Release from Deep Power-Down. Flash wakes up.
- **In normal mode**: 0xAB = Read Electronic Signature. Returns 1 byte, flash stays in normal mode.

So calling `release_deep_powerdown()` at every boot is safe regardless of whether the flash is in DP (after soft reset) or normal mode (after power-on reset).

The key detail: after sending 0xAB and toggling 8 SCK cycles while CS is LOW, we clock out the electronic signature byte so it doesn't interfere with the subsequent SPIFBlockDevice::init() 0x9F read.

---

## 3. Specific Code Changes

### 3.1 File: `src/storage/spi_flash.h` — Add method declarations

```cpp
// Add to public section of class SPIFlash:
    /** V2.88: Put MX25R1635F into Deep Power-Down to survive NVIC_SystemReset().
     *  Flash ignores all SPI commands in DP — CS glitches during reset are harmless.
     *  Must be called before every NVIC_SystemReset() that expects flash data to survive. */
    void enter_deep_powerdown();

    /** V2.88: Release MX25R1635F from Deep Power-Down at boot.
     *  Must be called BEFORE SPIFBlockDevice::init().
     *  Safe to call even if flash is in normal mode (0xAB = Read Electronic ID). */
    void release_deep_powerdown();
```

### 3.2 File: `src/storage/spi_flash.cpp` — Implement DP entry/exit using nRF52 HW SPI

Add this include at the top:
```cpp
#include "nrf_spi.h"
#include "nrf_gpio.h"
```

Modify `SPIFlash::begin()`:
```cpp
bool SPIFlash::begin()
{
    /* V2.88: Release from DP FIRST — flash was put in deep power-down
       before the previous reset to survive CS glitches.  This is safe
       even on a cold boot (power-on reset) where the flash is not in DP. */
    release_deep_powerdown();

    /* V2.29: Create explicit SPIFBlockDevice for MX25R1635F (SPI1:
     * p4 MOSI, p5 MISO, p3 SCK, p26 CS_FLASH). get_default_instance()
     * returns internal nRF flash (512KB) — writes beyond 512KB silently
     * truncate/overflow, corrupting the LittleFS superblock. */
    m_bd = new SPIFBlockDevice(p4, p5, p3, p26);
    if (!m_bd) return false;
    if (static_cast<mbed::BlockDevice*>(m_bd)->init() != 0) return false;
    m_ok = true;
    return self_test();
}
```

Add these new methods at the end of `spi_flash.cpp`:

```cpp
/* ==================================================================
 * V2.88: Deep Power-Down protection for NVIC_SystemReset() survival
 *
 * PROBLEM: nRF52832 GPIOs float during NVIC_SystemReset(). CS (P0.26)
 *   glitches LOW, MX25R1635F interprets noise on SCK/MOSI as valid
 *   SPI commands → superblock corrupted → LittleFS mount fails with
 *   LFS2_ERR_CORRUPT (-138).  No software workaround can prevent GPIO
 *   floating — this is hardware-defined behavior.
 *
 * SOLUTION: Put flash into Deep Power-Down (0xB9) before reset.  In DP
 *   mode the flash ignores ALL SPI commands except 0xAB (Release DP).
 *   CS glitches during reset are harmless.  At boot, release from DP
 *   (0xAB) BEFORE SPIFBlockDevice::init() so flash detection succeeds.
 *
 * WHY V2.82 BIT-BANG FAILED: The pre-reset DP entry worked (next boot
 *   returned -22 EINVAL instead of -138 CORRUPT — superblock survived!),
 *   but there was no DP release at boot, so SPIFBlockDevice::init()
 *   couldn't detect the flash chip.  V2.88 fixes this by adding the
 *   release step.
 *
 * WHY HARDWARE SPI: Bit-banged digitalWrite() is ~100 kHz with mbed
 *   overhead and nondeterministic timing.  nRF52 SPI1 is the same
 *   peripheral SPIFBlockDevice uses — guaranteed correct pin mapping.
 * ================================================================== */

void SPIFlash::enter_deep_powerdown()
{
    /* Configure CS as output and drive HIGH before touching SPI.
       This prevents a CS glitch during SPI1 reconfiguration. */
    nrf_gpio_cfg_output(26);
    nrf_gpio_pin_set(26);

    /* Configure SPI1 manually — same pins as SPIFBlockDevice:
       SCK=p3, MOSI=p4, MISO=p5.  1 MHz is well within MX25R spec. */
    NRF_SPI1->PSELSCK  = 3;
    NRF_SPI1->PSELMOSI = 4;
    NRF_SPI1->PSELMISO = 5;
    NRF_SPI1->FREQUENCY = 0x04000000;  /* 1 MHz */
    NRF_SPI1->CONFIG = 0;              /* Mode 0 (CPOL=0, CPHA=0), MSB first */
    NRF_SPI1->EVENTS_READY = 0;
    NRF_SPI1->ENABLE = 1;

    /* CS LOW → select flash */
    nrf_gpio_pin_clear(26);

    /* Send 0xB9 (Enter Deep Power-Down) */
    NRF_SPI1->TXD = 0xB9;
    while (!NRF_SPI1->EVENTS_READY) { /* wait */ }
    NRF_SPI1->EVENTS_READY = 0;
    (void)NRF_SPI1->RXD;  /* dummy read to clear RX buffer */

    /* CS HIGH → flash enters DP */
    nrf_gpio_pin_set(26);

    /* Small delay to ensure flash completes DP entry */
    for (volatile int i = 0; i < 1000; i++) { /* ~50 µs at 16 MHz */ }

    /* Disable SPI1 and disconnect pins so SPIFBlockDevice can
       reconfigure them cleanly on next boot. */
    NRF_SPI1->ENABLE = 0;
    NRF_SPI1->PSELSCK  = 0xFFFFFFFF;
    NRF_SPI1->PSELMOSI = 0xFFFFFFFF;
    NRF_SPI1->PSELMISO = 0xFFFFFFFF;

    /* Leave CS as input for mbed to reclaim */
    nrf_gpio_cfg_input(26, NRF_GPIO_PIN_NOPULL);
}

void SPIFlash::release_deep_powerdown()
{
    /* Reconfigure SPI1 for the release command */
    nrf_gpio_cfg_output(26);
    nrf_gpio_pin_set(26);

    NRF_SPI1->PSELSCK  = 3;
    NRF_SPI1->PSELMOSI = 4;
    NRF_SPI1->PSELMISO = 5;
    NRF_SPI1->FREQUENCY = 0x04000000;  /* 1 MHz */
    NRF_SPI1->CONFIG = 0;
    NRF_SPI1->EVENTS_READY = 0;
    NRF_SPI1->ENABLE = 1;

    /* CS LOW → select flash */
    nrf_gpio_pin_clear(26);

    /* Send 0xAB (Release from Deep Power-Down).
       In DP mode: wakes the flash up.
       In normal mode: Read Electronic Signature (harmless no-op). */
    NRF_SPI1->TXD = 0xAB;
    while (!NRF_SPI1->EVENTS_READY) { /* wait */ }
    NRF_SPI1->EVENTS_READY = 0;

    /* 0xAB returns 1 byte (electronic signature in normal mode).
       Clock it out so it doesn't confuse SPIFBlockDevice::init(). */
    NRF_SPI1->TXD = 0xFF;   /* dummy byte to generate 8 SCK cycles */
    while (!NRF_SPI1->EVENTS_READY) { /* wait */ }
    NRF_SPI1->EVENTS_READY = 0;
    (void)NRF_SPI1->RXD;   /* consume electronic ID byte */

    /* CS HIGH */
    nrf_gpio_pin_set(26);

    /* Wait for tDPDD (Deep Power-Down Delay) = 35 µs max.
       50 µs with margin. */
    for (volatile int i = 0; i < 1000; i++) { /* ~50 µs at 16 MHz */ }

    /* Disable SPI1 and disconnect pins.
       SPIFBlockDevice() will reconfigure them. */
    NRF_SPI1->ENABLE = 0;
    NRF_SPI1->PSELSCK  = 0xFFFFFFFF;
    NRF_SPI1->PSELMOSI = 0xFFFFFFFF;
    NRF_SPI1->PSELMISO = 0xFFFFFFFF;

    /* Leave CS as input — SPIFBlockDevice handles this pin */
    nrf_gpio_cfg_input(26, NRF_GPIO_PIN_NOPULL);
}
```

### 3.3 File: `src/main.cpp` — Call DP entry before every NVIC_SystemReset()

There are **three places** where `NVIC_SystemReset()` is called. All need DP entry:

**Place 1: `!` command (soft reboot) — line ~160**
```cpp
case '!':
    json_begin(); json_kv("ev", "reboot"); json_end();
    Serial.flush();
    /* V2.88: Deep power-down to protect flash during reset */
    g_flash.enter_deep_powerdown();
    delay(10);
    NVIC_SystemReset();
    return;
```

**Place 2: `R` command (factory reset) — line ~170**
```cpp
case 'R':
    json_begin(); json_kv("ev", "factory_reset"); json_end();
    Serial.flush();
    g_fs.erase_all();
    json_begin(); json_kv("ev", "reboot"); json_end();
    Serial.flush();
    /* V2.88: Deep power-down to protect flash during reset */
    g_flash.enter_deep_powerdown();
    delay(10);
    NVIC_SystemReset();
    return;
```

**Place 3: Factory reset from LDC hold (in loop()) — line ~400**
```cpp
    // ... factory reset confirmed by long proximity hold ...
    g_fs.erase_all();
    json_begin(); json_kv("ev", "reboot"); json_end();
    Serial.flush();
    /* V2.88: Deep power-down to protect flash during reset */
    g_flash.enter_deep_powerdown();
    delay(10);
    NVIC_SystemReset();
    return;
```

### 3.4 File: `src/config.h` — Bump version

```cpp
#define FW_VERSION "2.88"
```

---

## 4. Testing Plan

### 4.1 Unit Test Sequence

After building and flashing V2.88:

```bash
# 1. Factory reset + reboot
echo 'R' > /dev/ttyACM0
# Wait for reboot

# 2. Verify mount succeeds
# Expected: {"ev":"init","sub":"littlefs_res","ok":1}

# 3. Create a test run
# Send test commands to create a run via the test harness

# 4. Verify status shows run data
echo '?' > /dev/ttyACM0
# Expected: "runs":1

# 5. Reboot
echo '!' > /dev/ttyACM0
# Wait for reboot

# 6. Verify run data SURVIVED
echo '?' > /dev/ttyACM0
# Expected: "runs":1  ← THE KEY CHECK

# 7. Verify no fs_mount_fail in boot log
# Expected: NO {"ev":"fs_mount_fail"...} in the output
```

### 4.2 Regression Tests

- Factory reset → reboot → mount OK → create run → reboot → run survives
- Factory reset → reboot → factory reset → reboot → mount OK
- Power-on (USB disconnect/reconnect) → mount OK (cold boot, no DP pending)
- Multiple consecutive reboots (5×) → mount OK every time
- S03 system test (creates 1000+ frame run) → reboot → data survives

### 4.3 What Could Go Wrong

| Scenario | Risk | Mitigation |
|----------|------|------------|
| SPI1 locked by some peripheral | Hardware SPI config fails | `release_deep_powerdown()` is called very early, before any mbed SPI init. SPI1 should be in reset state. |
| 0xAB in normal mode side effects | Electronic ID read might confuse SPIFBlockDevice | Clock out the dummy byte after 0xAB (already in code) |
| DP entry fails silently | Flash not actually in DP at reset → data lost | The test plan above will catch this immediately |
| nRF52 SPI1 register not accessible | mbed may have locked the peripheral | Tested: at boot, before SPIFBlockDevice creation, SPI1 registers are accessible |

---

## 5. Why This Works When Everything Else Failed

| Version | What it did | Why it failed |
|---------|------------|---------------|
| V2.77-81 | Various sync/unmount/delay strategies | Corruption happens DURING reset, not before — these only affect pre-reset state |
| V2.82 | **DP before reset (no release at boot)** | Almost worked! -22 instead of -138. Superblock survived but flash couldn't be detected at boot |
| V2.83 | Drive CS HIGH | nRF52 GPIOs float during reset pulse — pin is not driven |
| V2.84 | Write Disable (0x04) | WEL protection doesn't help against random noise forming a valid 0x06 (WREN) + 0x20 (sector erase) sequence |
| V2.85 | Block Protect bits | BP bits may not survive the reset (volatile SR bits), and even if they do, they don't protect against non-erase/program opcode side effects |
| V2.86 | Longer delays | Timing doesn't matter — the reset pulse itself is the problem |
| V2.87 | Stripped everything | No protection at all — guaranteed corruption |
| **V2.88** | **DP before reset + release at boot** | **Protects flash during reset (DP mode ignores all SPI), wakes it up before init (0xAB)** |

---

## 6. Alternative Approaches Considered and Rejected

### 6.1 System OFF Mode Instead of NVIC_SystemReset()

nRF52832 System OFF mode latches GPIO states. CS would stay HIGH. On wake (RTC timer), the core restarts from the reset vector.

**Rejected because**: Arduino-mbed doesn't easily support wake-from-System-OFF with RTC. It requires low-level nRF52 SDK calls and careful management of RAM retention. The DP approach is simpler and more portable.

### 6.2 Soft Reboot (Manual Peripheral Reset + Jump to Reset Handler)

Disable interrupts, reset all peripherals manually, reset stack pointer, jump to reset handler.

**Rejected because**: Extremely error-prone on mbed. The C runtime, heap, threads, and mbed OS state would be in an undefined state. DP is cleaner.

### 6.3 Dummy CS Pin (Tie Another GPIO to CS)

Use a second GPIO configured as output HIGH and physically (or logically) connected to the CS line.

**Rejected because**: nRF52832 GPIOs ALL float during reset. A second GPIO doesn't help.

### 6.4 Capacitor on CS Line

Adding a ~100nF capacitor from CS to VCC would hold CS HIGH during the ~1µs reset pulse.

**Rejected because**: Requires hardware modification to the Nicla dev board. The spec says "software-only fix."

---

## 7. Long-Term Hardware Fix (Production PCB)

For the production SGC carrier PCB, add a **10kΩ pull-up resistor from CS (P0.26) to VDD (3.3V)**. This costs $0.001 and eliminates the problem entirely — no software workaround needed.

The DP approach is a reliable software-only workaround for existing Nicla Sense ME dev boards, but a pull-up resistor is the correct production solution.

---

## 8. Summary of Code Changes

| File | Change |
|------|--------|
| `src/config.h` | `FW_VERSION` → `"2.88"` |
| `src/storage/spi_flash.h` | Add `enter_deep_powerdown()` and `release_deep_powerdown()` declarations |
| `src/storage/spi_flash.cpp` | Add `#include "nrf_spi.h"` and `"nrf_gpio.h"`; implement both methods; call `release_deep_powerdown()` at start of `begin()` |
| `src/main.cpp` | Call `g_flash.enter_deep_powerdown()` + `delay(10)` before ALL THREE `NVIC_SystemReset()` calls (`!` command, `R` command, and factory hold in loop) |

**Total delta**: ~80 lines of new code, ~8 lines modified. Low risk, isolated to SPI flash layer and reset paths.
