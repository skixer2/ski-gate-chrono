---
type: project-note
project: ski_gate_chrono
tags: [ski_gate_chrono]
created: 2026-07-19
---

# SGC Flash Persistence Hypotheses

> Research conducted 2026-07-19 against LittleFS DESIGN.md, SPEC.md, GitHub issues, MX25R1635F datasheet, UCSD DAC 2011 power-cut paper, nRF52832 PS v1.8, and mbed SPIFBlockDevice documentation.

---

## Hypothesis 1: Power Loss During Sector Erase → Superblock/Metadata Corruption

### The Mechanism

The MX25R1635F uses 4 KiB erase sectors. Flash erase is a **multi-phase, non-atomic** operation:
1. **Pre-program** — all bits in the sector are brought to "0" to ensure uniform starting state
2. **Erase pulses** — multiple high-voltage pulses discharge floating gates to "1"
3. **Soft-program** — tighten threshold voltage distribution, prevent over-erase

If power is lost at **any point** during this sequence, the sector is left in an **indeterminate state** — some bits 0, some 1, some in partially-changed threshold levels. Reading such a sector returns garbage.

**LittleFS's fundamental assumption is violated here.** LittleFS's DESIGN.md states that metadata pairs provide atomicity because either the new revision or old revision will survive. But this assumes the underlying block device either completes a write or leaves the old data intact. NOR flash erase does NOT preserve old data — it actively destroys it. If the sector being erased contains block 0 of a metadata pair, and power is lost mid-erase, that block is **unrecoverable**.

The worst case: if the superblock itself is being relocated (triggered by `block_cycles=500` wear-leveling threshold) and power is lost during the erase of the old superblock sector, you get the exact failure mode reported in **LittleFS GitHub issue #953**: "superblock seems to have lost its superness — missing the littlefs tag." Two devices became bricks, even without any logged power-loss event.

### The Trigger Scenario

- Battery voltage sag during any flash erase operation
- Erases are triggered by:
  - **Metadata relocation** — when `block_cycles=500` is hit on a metadata pair, LittleFS moves it to fresh blocks and erases the old ones
  - **File deletion** — `delete_oldest_run()` frees a run file, triggering COW metadata updates + erase of old data blocks
  - **Garbage collection** — directory compaction when commits accumulate
  - **New file creation** — `create_run()` allocates fresh blocks, which may trigger erases if GC is needed
- BLE TX current peaks (5–12 mA) coincide with erase operation → battery voltage dips below nRF52832 brownout threshold (1.7V)
- Cold temperature increases LiPo internal resistance, making voltage sag worse
- **Device remains unresponsive after reboot** — filesystem mount fails with EINVAL (-22), effectively bricked

### Evidence/Sources

1. **LittleFS DESIGN.md** — metadata pairs are two-block logs; both blocks must be viable for recovery. If one is mid-erase, recovery relies on the other having a valid revision count with intact CRC.
2. **LittleFS GitHub #953** — Two production devices lost superblock "littlefs" tag without power-loss events; both occurred "shortly after a soft reboot." The old superblock sectors were likely erased during a COW relocation, then a crash or brownout interrupted the metadata pair update before the new superblock was fully committed.
3. **Infineon/Cypress NOR Flash Erase KB** — confirms erase is multi-phase (pre-program → pulses → soft-program), not atomic.
4. **UCSD DAC 2011 "Understanding the Impact of Power Loss on Flash Memory"** (Tseng, Grupp, Swanson) — Key findings:
   - "Operations closer to completion do not necessarily exhibit fewer bit errors"
   - **"Power failure can corrupt data already present in the flash device, not just the data being written"**
   - "Power failure can negatively impact the integrity of future data written to the device"
5. **StackExchange: "What would happen in case of power outage during NOR flash erase"** — confirms sector becomes indeterminate, requires re-erase and rewrite for recovery.
6. **Macronix AN0247** — MX25R supports Erase Suspend/Resume, but SGC firmware does NOT use this feature. An in-progress erase has no graceful interruption path.

### Detection Method

- After a mount failure, dump raw sector 0x4000–0x7FFF (the first LittleFS superblock region) via debug probe
- Check if any sector in the superblock pair contains:
  - Neither valid "littlefs" tag (byte sequence `6C 69 74 74 6C 65 66 73`)
  - Partially erased pattern (mix of 0xFF and non-0xFF that doesn't match any valid LittleFS metadata)
  - Both superblock copies corrupted → confirms Hypothesis 1
- Monitor supply voltage with oscilloscope during flash writes while BLE is active — look for dips below 2.5V
- Add a boot-time diagnostic: on mount failure, log raw first 64 bytes of blocks 0 and 1 of the superblock pair

### Mitigation Recommendation

1. **Add bulk capacitance on the flash VCC rail** — 10–47 µF MLCC near the MX25R VCC pin to ride through BLE TX current pulses during erase (~50ms max erase time per sector). This is the hardware fix.
2. **Configure nRF52832 POF (Power-Fail) warning** at ~2.5V — generate interrupt before brownout, immediately abort any in-progress flash operation and set a "dirty shutdown" flag.
3. **Implement a "write intent" journal** — before any erase or critical metadata operation, write a small intent record to a dedicated sector. On boot, check for dangling intents and re-erase/recover the affected sector.
4. **Increase `block_cycles`** from 500 to 1000 — reduces metadata relocation frequency, shrinking the window.
5. **Never erase the old superblock until the new one is fully committed** — but this is a LittleFS internal change, not configurable from user space.

---

## Hypothesis 2: close_run() Header Rewrite Triggers COW Cascade — Power Loss → Truncated/Zero-Length File

### The Mechanism

This is the **most likely cause of data_size=0 runs**. The current close_run() sequence:

```
1. flush_buffer()        — flush 256-byte RAM buffer to LittleFS
2. write CRC32 trailer   — append [0xC3][0x32][CRC32 LE 4B] to end of file
3. sync()                — commit all buffered data to flash
4. seek(0, SEEK_SET)     — go to beginning of file
5. write(header)         — rewrite 16-byte header with actual data_size
6. sync()                — commit header change
7. close()               — close file
```

**Step 5 is the danger zone.** LittleFS is fundamentally a **copy-on-write** filesystem. When you seek to position 0 and write 16 bytes to a file that may be several KB long, LittleFS does NOT modify those 16 bytes in place. Instead, it must:

- Copy the **entire file** (all data blocks) to newly allocated blocks
- Write the modified header to the first block of the new copy
- Update directory metadata to point to the new blocks
- Eventually erase the old blocks (during GC)

This COW cascade takes time proportional to file size. For a 5KB run file, this means rewriting 5KB of data just to change 16 bytes of header. If power is lost during this cascade:

- The new blocks may be partially written → discarded as invalid
- The old blocks may have already been marked for erasure in metadata
- The directory entry may point to the new (incomplete) location
- Result: **file appears to exist but data_size=0** (header never written) or file is entirely absent

This gets worse because the CRC32 trailer was written in step 2, BEFORE the header rewrite. So the old file data + CRC were successfully committed, but then the whole file gets torn down and rebuilt for the header update.

**Key LittleFS design constraint** (from multiple sources including esp32/esp8266 discussions): "LittleFS does not seek efficiently. If data is written to the middle of a file, the filesystem may need to rewrite everything from the point of the write to the end of the file."

### The Trigger Scenario

- Large run file (many KB of compressed IMU data) — longer COW cascade time
- Power loss during steps 5–6 (between the two sync calls)
- Specifically: battery dies during the COW rewrite of the file data blocks
- This is **more likely with longer runs** because the COW rewrite window scales with file size
- The test harness window being too short (7s) masked this — POST_RUN fires ~5s after stream end, and the full COW rewrite may not complete until ~6-8s after stream start for a long run

### Evidence/Sources

1. **LittleFS DESIGN.md Section "The move problem"** — describes how filesystem operations that modify any part of a file require rewriting the entire COW structure from the modification point to the root.
2. **LittleFS GitHub Issue #27 and #485** — discuss O_WRONLY seek+write behavior, confirming that in-place modification is not truly in-place.
3. **ESP32 LittleFS forums** — multiple reports of zero-byte files after power loss when close() was not called, specifically noting that `seek(0)` + `write()` on an existing file is a multi-block COW operation vulnerable to power loss.
4. **SGC observed failure mode** — `{"ev":"scan_skip","id":X,"reason":"data_size=0"}` logs confirm exactly this pattern: files exist but have data_size=0 in the header. This is exactly what you'd see if power was lost after file creation + data writing + CRC trailer, but before/during the header rewrite commit.
5. **LittleFS SPEC.md** — metadata checksums protect filesystem structure but NOT file data. A partially-COW-copied file has valid metadata but corrupted/zeroed data blocks.

### Detection Method

- Add a timestamp log before and after the seek(0)+write(header) sequence
- Correlate data_size=0 runs with any brownout/reset events in the same time window
- Check: do data_size=0 runs have valid CRC32 trailers? If the CRC32 was written (step 2 succeeded) but the header is zero (step 5 failed), the file content is actually recoverable — the CRC is at the end but the header says 0 length
- Instrument close_run() to log at each step: "close:flush", "close:crc", "close:sync1", "close:seek", "close:header", "close:sync2", "close:done"
- If power is lost between "close:sync1" and "close:sync2", Hypothesis 2 is confirmed

### Mitigation Recommendation

1. **Store the header at the END of the file, not the beginning.** Better yet, store it as a separate small file (e.g., `/run_NNNNN.hdr`). LittleFS can atomically modify a small header file with much lower COW overhead. Alternatively, encode the length in the CRC32 trailer itself — no separate header needed.
2. **Use a sentinel-based approach instead of a header:** writer appends data + CRC trailer; on mount, scanner reads from end of file backward to find the CRC32 magic bytes. The file length IS the data size — no header rewrite needed at all. This eliminates the seek+write+COW-cascade entirely.
3. **Write the final frame count into the CRC32 trailer** (reserve 2 bytes of the 6-byte trailer for frame count) — the scanner can recover the exact frame count by reading the last 6 bytes, without seeking to byte 0.
4. **Pre-allocate the header with correct size** — if you know the expected frame count before writing (from IMU data rate × duration), write the header FIRST, then append compressed data. If power is lost, the header overstates the data, but the CRC32 at actual data end lets you recover the real boundary.
5. **Alternative: append-only format** — never seek backward. Write a JSON footer at close that contains metadata. The scanner reads forward to find the JSON footer, which contains data_size.

---

## Hypothesis 3: Double-Buffering Data Loss — Application RAM Buffer + LittleFS Internal Cache

### The Mechanism

There are **two independent layers of volatile buffering** between IMU data and physical flash:

**Layer 1: Application 256-byte write buffer**
- IMU frames arrive at 100 Hz
- Compressed frames are accumulated in a RAM buffer
- Buffer is flushed to LittleFS when full or at stream end
- **If power is lost before flush, all buffered frames are lost**

**Layer 2: LittleFS internal 256-byte cache** (`cache_size=256`)
- When `lfs_file_write()` is called, data goes into LittleFS's internal 256-byte program cache
- Cache is flushed to flash only when:
  - Cache is full
  - `lfs_file_sync()` is called
  - `lfs_file_close()` is called (which calls sync)
- **If power is lost between writes and sync, cached data is lost**

In the worst case, up to **512 bytes of the most recent compressed frame data** can be lost:
- Up to 256 bytes in application buffer (not yet passed to LittleFS)
- Up to 256 bytes in LittleFS cache (not yet synced to flash)

Since the buffer is flushed periodically (not per-frame), the data at highest risk is the **most chronologically recent** — exactly the final gate-crossing moments that users care most about.

This is distinct from the data_size=0 problem. Here the file exists and has valid data, but the tail is truncated.

### The Trigger Scenario

- Stream is actively logging (LOGGING state), data accumulating in app buffer
- Power is lost before the buffer is full enough to trigger a flush
- OR: buffer was flushed to LittleFS, but `lfs_file_sync()` hasn't been called yet
- This is particularly likely with short runs or right at the start of a run (few frames, buffer not yet filled)

### Evidence/Sources

1. **LittleFS README & DESIGN.md** — "File updates are not actually committed to the filesystem until sync or close is called on the file." Confirmed in multiple embedded forum discussions (ESP32/ESP8266/LittleFS behavior differs between platforms on when flush actually commits).
2. **mbed LittleFileSystem2 API** — `cache_size` parameter defines the internal RAM buffer size used for read/program caching.
3. **ESP32 vs ESP8266 LittleFS behavior** — documented on esp32.com: "On ESP8266, data might be synced on file.flush() even without file.close(), while on ESP32, this might not be the case, potentially leading to zero-byte files." The mbed platform may have its own idiosyncrasies.
4. **SGC TOOLS.md** notes: "buffered in RAM (256-byte buffer), flushed periodically" — confirms the app-level buffering exists and acknowledges it as a loss window.

### Detection Method

- Do a controlled test: start a 5-second stream, cut power at 3 seconds
- Mount filesystem, scan for the run file
- If the file exists with non-zero data_size but decompresses fewer frames than expected → lost tail data
- Compare lost frame count to buffer size: does it correlate with ~256 bytes / average compressed frame size?
- Add a "flush on N frames" counter — flush every 50 frames regardless of buffer fullness → see if data loss window shrinks

### Mitigation Recommendation

1. **Sync after every flush** — currently flush writes to LittleFS but sync is deferred. Call `lfs_file_sync()` after each buffer flush. Tradeoff: more flash wear, more SPI traffic, but data loss window shrinks from ~512 bytes to just the app buffer.
2. **Reduce app buffer size** — use 64-byte or 128-byte buffer instead of 256. Flushes happen more frequently, reducing maximum data loss per power-cut event.
3. **Flush on state transitions** — when the scanner detects a gate crossing (IMU spike), immediately flush the buffer. The most critical frames are preserved.
4. **Use LittleFS's `cache_size=0`** — disables internal caching entirely, every write goes directly to flash. Tradeoff: much slower, more wear, but no double-buffering loss.
5. **Consider FlashRing as safety net** — the FlashRing raw SPI buffer in sectors 0-3 already captures pre-start data. Could it also serve as a post-mortem recovery buffer? On reboot, scan FlashRing for the last N seconds of IMU data and recover lost frames.

---

## Hypothesis 4: Brownout During Flash Page Program → Silent Bit-Level Corruption

### The Mechanism

The MX25R1635F has a **256-byte page program buffer**. When data is sent via SPI for programming:

1. Data is loaded into the flash chip's internal volatile page buffer
2. An internal high-voltage programming cycle writes the buffer to the NOR array
3. This cycle takes typically 0.6–3ms per page (MX25R spec: tPP = 1.4ms typ)

If nRF52832 power fails during step 2:
- **Some bits may be programmed, others not** — the page is left in a partially-written state
- **Adjacent cells can be disturbed** — the DAC 2011 paper proved that power loss during flash programming can corrupt data in cells **adjacent to** those being programmed, not just the target cells
- The flash chip's internal write-enable latch and status register are volatile — on power restoration, the chip comes up in a clean state with no indication that a write was interrupted

This is the **most insidious failure mode** because:
- The filesystem appears to mount correctly
- Metadata blocks pass CRC checks
- Individual pages within a data block may contain corrupted bytes
- The file's CRC32 trailer might pass (if the corruption is in data, the CRC32 at the end was written earlier and is fine)
- **The file decompresses and seems valid, but gate-crossing times are wrong**

### The Trigger Scenario

- Device is running on a partially discharged battery (3.3–3.6V)
- BLE connection is active — TX bursts draw 5–12mA
- A flash page program is in progress (app buffer flush → LittleFS write → SPIF driver → SPI → MX25R page program)
- BLE TX peak + flash program current combine to sag VCC below ~2.7V
- The nRF52832 itself may survive (BOR at 1.7V), but the MX25R flash chip requires minimum 1.65V — if the SPI bus glitches or the flash chip's internal high-voltage charge pump can't sustain programming voltage, the page program is silently corrupted
- **No reset occurs** — the system continues running with silently corrupted data on flash

This is worse in cold weather (ski racing!) because LiPo internal resistance rises sharply below 0°C, making voltage sag deeper for the same current draw.

### Evidence/Sources

1. **UCSD DAC 2011 "Understanding the Impact of Power Loss on Flash Memory"** (Tseng, Grupp, Swanson, UCSD) — Directly tested power-cut during flash program/erase on real devices. Key finding: **"Power failure can corrupt data already present in the flash device, not just the data being written."** The paper built a test rig that repeatedly cut power during flash operations and measured bit error patterns.
2. **Infineon Knowledge Base: "Power Loss during the Write Register (WRR) Operation in Serial NOR Flash Devices"** — Confirms power loss during WRR (which programs the status register flash sector) can corrupt critical chip configuration.
3. **Macronix MX25R1635F Datasheet v1.6** — Section on DC Characteristics: VCC must be ≥1.65V for reliable operation. During programming, ICC2 = 15mA typical. Combined with BLE TX (5–12mA), total pulse current can exceed 27mA. A LiPo with 200mΩ internal resistance sags 5.4V at 27mA — obviously that's a model oversimplification but the principle holds: even a few hundred mV sag matters.
4. **nRF52832 PS v1.8** — Section on POWER: "The power supply must be stable before the chip can operate. A sudden drop in supply voltage can cause a reset or unintended behavior." BOR threshold is 1.7V ± 0.1V, but the MX25R flash has its own internal brownout that may trigger at a DIFFERENT threshold, causing desynchronized reset behavior.
5. **Nordic DevZone: "BLE connection lost after flash write"** — Documented cases where flash write operations cause BLE disconnection due to timing/current draw conflicts, confirming the concurrent operation problem.
6. **Electronics StackExchange: "How to prevent partial write data corruption during power loss"** — Recommends adding hold-up capacitors (beefy caps) on flash VCC rail specifically to allow in-progress page programs to complete during power loss.

### Detection Method

- **Golden data test**: Write a known pattern to flash, read it back, cut power during the next write to an adjacent sector, read the known pattern again. Check for bit flips in the "untouched" sector.
- **Run integrity audit**: On mount, for each run file, read the entire compressed data, verify CRC32, attempt decompression. Flag any run where decompression fails or yields unexpected frame count.
- **Flash page dump comparison**: After a suspicious run, dump the raw flash pages containing the run data. Compare to what LittleFS "thinks" should be there. Do any pages have bit patterns that aren't all-0xFF (erased), valid-LittleFS, or valid-compressed-data? Partial patterns = silent corruption.
- **Oscilloscope VCC monitoring**: Capture VCC during BLE TX + flash write. If dips below 2.5V are observed, Hypothesis 4 is confirmed as a risk.
- **CRC32-only runs**: If a run file has valid CRC32 but corrupted decompressed data, the CRC was computed on corrupted input (or the corruption happened after CRC write). This reveals timing of corruption.

### Mitigation Recommendation

1. **Hardware: Add hold-up capacitance** — 47–100 µF electrolytic or 22 µF MLCC on the MX25R VCC pin, with a Schottky diode to prevent back-feeding the nRF52832. This gives the flash chip enough energy to complete the current page program (1.4ms at 15mA needs only ~0.2 µC — a 10 µF cap charged to 3.3V holds 33 µC, giving 150x margin).
2. **Hardware: Dedicated LDO for flash** — Power the MX25R from its own 3.0V LDO, separate from the nRF52832 supply. BLE TX current spikes affect the nRF52832 rail but not the flash rail.
3. **Software: Avoid concurrent BLE TX + flash writes** — When LOGGING state is active, suspend BLE advertising or switch to non-connectable advertising (lower duty cycle, fewer TX bursts). If the device is connected, buffer data in RAM and flush flash during BLE connection intervals when the radio is idle.
4. **Software: Verify-after-write** — After every flash page program, read back the programmed page and compare to the write buffer. If mismatch, re-erase and reprogram the page. Catches silent corruption immediately.
5. **Per-frame CRC** — Instead of a single CRC32 at end of run, embed a 16-bit CRC per compressed frame (or per 64-byte chunk). The decompressor can detect individual corrupted frames and skip them rather than failing the entire run.

---

## Summary: Interaction Matrix

| Hypothesis | Lost Data Scope | Detectable by CRC32? | Mount Survives? | Primary Trigger |
|---|---|---|---|---|
| H1: Erase corruption | Entire filesystem | N/A (mount fails) | ❌ No | Battery sag during erase |
| H2: COW cascade | Single run (data_size=0) | ❌ (header is zero) | ✅ Yes | Power loss during close_run() header rewrite |
| H3: Buffer loss | Last ~512 bytes of run | ⚠️ (CRC covers truncated data) | ✅ Yes | Power loss between flushes |
| H4: Silent bit corruption | Random bytes in any run | ⚠️ (CRC of corrupted data) | ✅ Yes | Brownout during page program |

These hypotheses are **not mutually exclusive**. In a real power-loss event, multiple mechanisms may compound:
- BLE TX sags battery → flash page program starts corrupting (H4)
- Seconds later, battery dies completely → erase in progress corrupts superblock (H1)
- On next boot, mount fails → device is bricked

---

## Immediate Recommended Actions

1. **Eliminate the header rewrite (H2 fix)** — this is the easiest software change with the highest impact. Store file length in the CRC trailer or in a sidecar file. This removes the entire COW cascade vulnerability.

2. **Add hold-up capacitance (H4 fix)** — a single 10 µF MLCC on the MX25R VCC pin costs <$0.10 and protects against page program corruption during brownouts.

3. **Add sync-after-flush (H3 fix)** — accepts the flash-wear tradeoff for data integrity during critical logging sessions.

4. **Add power-fail early warning (H1 fix)** — configure nRF52832 POFCON to generate interrupt at 2.5V, giving the system 100ms+ to gracefully stop flash operations before the 1.7V BOR hits.

---

## References

1. LittleFS DESIGN.md — https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md
2. LittleFS SPEC.md — https://github.com/littlefs-project/littlefs/blob/master/SPEC.md
3. LittleFS GitHub Issue #953 (Superblock corruption) — https://github.com/littlefs-project/littlefs/issues/953
4. Tseng, Grupp, Swanson — "Understanding the Impact of Power Loss on Flash Memory", DAC 2011 — https://cseweb.ucsd.edu/~swanson/papers/DAC2011PowerCut.pdf
5. Macronix MX25R1635F Datasheet v1.6 — https://www.macronix.com/Lists/Datasheet/Attachments/8702/MX25R1635F,%20Wide%20Range,%2016Mb,%20v1.6.pdf
6. Macronix AN0247 — Using Erase Suspend Function — https://www.mxic.com.tw/Lists/ApplicationNote/Attachments/1901/AN0247Using%20Erase%20Suspend%20Function%20v2.2.pdf
7. nRF52832 Product Specification v1.8 — https://www.mouser.com/datasheet/2/297/nRF52832_PS_v1_8-2942485.pdf
8. Infineon — How Erase Operation Works in NOR Flash — https://community.infineon.com/t5/Knowledge-Base-Articles/How-Erase-Operation-Works-in-NOR-Flash/ta-p/251756
9. Infineon — Power Loss during WRR Operation — https://community.infineon.com/t5/Knowledge-Base-Articles/Power-Loss-during-the-Write-Register-WRR-Operation-in-Serial-NOR-Flash-Devices/ta-p/248409
10. mbed SPIFBlockDevice API — https://os.mbed.com/docs/mbed-os/v6.16/apis/spi-flash-block-device.html
11. mbed LittleFileSystem2 API — https://os.mbed.com/docs/mbed-os/v6.16/apis/littlefilesystem.html
12. Nordic DevZone — nRF52832 flash write / BLE conflicts — https://devzone.nordicsemi.com/f/nordic-q-a/52220/ble-connection-lost-after-flash-write
13. Particle Community — Flash corruption when truncating large files (LittleFS bug) — https://community.particle.io/t/flash-corruption-when-truncating-large-files-due-to-potential-bug-in-littlefs/61622
14. SGC TOOLS.md — System architecture and known problems (v2.17–v2.19) — workspace TOOLS.md
