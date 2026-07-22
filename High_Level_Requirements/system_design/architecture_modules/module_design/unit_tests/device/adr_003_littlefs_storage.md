# ADR-003: LittleFS + Flash Ring Storage Architecture

**Status:** ACCEPTED (2026-07-11), AMENDED (2026-07-22)
**Replaces:** FlashManager (Phase 10 circular raw flash)
**Rationale:** Wear-leveling, power-loss resilience, simplify code (900→250 lines).

## ⚠️ 2026-07-22 Amendment: LittleFS v1 (NOT v2)

**LittleFileSystem2 (v2.0.2) is BROKEN on Arduino-mbed for nRF52832.**
Root-directory metadata-pair compaction corrupts the filesystem after any write.
Switched to `LittleFileSystem` (v1) — same API, stable, matches Arduino Nicla example.

- Include: `<LittleFileSystem.h>` (NOT LittleFileSystem2)
- Constructor: `LittleFileSystem("littlefs")` — name only, BD passed to mount()
- All File/Dir/stat API is identical — drop-in replacement

## Architecture

```
┌──────────────────────┬─────────────────────────────────────────┐
│ Sectors 0-3 (16KB)   │ Sectors 4-511 (~1.98MB)                │
│                      │                                        │
│ FlashRing buffer     │ LittleFileSystem (mbed) [← was v2]    │
│ Raw SPI, zero RAM BSS│ SlicingBlockDevice(0x4000, 0x200000)   │
│ write_page() @ 100Hz │ mount/reformat, file create/append     │
│ erase_block() halves │ wear-leveled, COW atomic, journal      │
│ (6 bytes RAM: head   │                                        │
│  + count + drain_buf)│ Runs stored as files: /run_NNNNN.dat   │
│                      │ [RunHeader 16B][frames…][CRC32 6B]     │
└──────────────────────┴─────────────────────────────────────────┘
```

## Key Decisions

### 1. LittleFS v1 over v2 (2026-07-22)
v2.0.2 has metadata-pair compaction bug on this platform. v1 is stable (3+ consecutive runs, BLE file transfer verified). v1 is deprecated upstream but works correctly — matches official Arduino Nicla example.

### 2. Flash ring (not RAM ring)
RAM ring cost 5.6KB BSS → 76.3% RAM → Cordio BLE OOM. Flash ring at 63.5% RAM (below 64.1% threshold). One-frame RAM drain cache (16 bytes) covers SPI bus busy period.

### 3. SlicingBlockDevice partitioning
LittleFS must NOT own sectors 0-3 (flash ring). `SlicingBlockDevice(raw_bd, 0x4000, 0x1FC000)` creates a 1.98MB view for LittleFS starting at sector 4.

### 3. bd->init() called exactly once
Only `LittleFSStorage::begin()` → `mount(sliced)` calls `bd->init()`. `SPIFlash::begin()` must NOT call it. Double init leaves MX25R1635F in bad state (reads work, writes fail silently → LFS2_ERR_CORRUPT → -138/EILSEQ).

### 4. Incremental CRC32
CRC accumulated byte-by-byte in `append_data()`. Finalized in `close_run()`. No seek/read on open file.

### 5. Crash-safe factory reset
`erase_all()` does raw sector erases (0x4000-0x14000) — no `fs->unmount()` or `fs->reformat()`. These can HardFault on corrupt metadata. Boot init handles fresh format+mount via auto-reformat.

### 6. POSIX flag values
System defines may be wrong. Undef + redefine in .cpp: `O_RDONLY=0, O_WRONLY=1, O_RDWR=2`.

## Init Order (critical)
```
1. nicla::begin()
2. BLE.begin()              ← Cordio heap first
3. g_flash.begin()          ← BlockDevice handle (no init)
4. g_ldc.begin()            ← LDC1612 proximity
5. g_fs.begin()             ← bd->init() + mount/reformat
6. g_ring.reset()           ← erase sectors 0-3 (BD now initialized)
7. BHY2.begin(STANDALONE)   ← sensor hub last
```

## Build
- RAM: 63.5% (40816/64288)
- Flash: 65.5%
- MAX_ENTRIES=8, cache_size=64, lookahead=8

### 8. O_WRONLY file mode (not O_RDWR)
RunHeader fields saved in RAM at `create_run()` time and used in `close_run()` — no read-back needed.

## Error Codes
- `-138 / EILSEQ` = LittleFS ERR_CORRUPT (corrupted metadata — factory reset needed)
- `CR_OPEN_ERR_XXX` = create_run failed with f->open() error code
- `CR_NO_FS` = filesystem not initialized
- `CR_ALREADY_OPEN` = previous run never closed
- `NOT_OPEN` = close_run called without open file

## Version Bumping
Bump `FW_VERSION` in `src/config.h` at EVERY code change. Boot JSON and `V` command confirm binary.
