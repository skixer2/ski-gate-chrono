# ADR-003: LittleFS + Flash Ring Storage Architecture

**Status:** ACCEPTED (2026-07-11)  
**Replaces:** FlashManager (Phase 10 circular raw flash)  
**Rationale:** Wear-leveling, power-loss resilience, simplify code (900→250 lines).

## Architecture

```
┌──────────────────────┬─────────────────────────────────────────┐
│ Sectors 0-3 (16KB)   │ Sectors 4-511 (~1.98MB)                │
│                      │                                        │
│ FlashRing buffer     │ LittleFileSystem2 (mbed)               │
│ Raw SPI, zero RAM BSS│ SlicingBlockDevice(0x4000, 0x200000)   │
│ write_page() @ 100Hz │ mount/reformat, file create/append     │
│ erase_block() halves │ wear-leveled, COW atomic, journal      │
│ (6 bytes RAM: head   │                                        │
│  + count + drain_buf)│ Runs stored as files: /run_NNNNN.dat   │
│                      │ [RunHeader 16B][frames…][CRC32 6B]     │
└──────────────────────┴─────────────────────────────────────────┘
```

## Key Decisions

### 1. Flash ring (not RAM ring)
RAM ring cost 5.6KB BSS → 76.3% RAM → Cordio BLE OOM. Flash ring at 63.5% RAM (below 64.1% threshold). One-frame RAM drain cache (16 bytes) covers SPI bus busy period.

### 2. SlicingBlockDevice partitioning
LittleFS must NOT own sectors 0-3 (flash ring). `SlicingBlockDevice(raw_bd, 0x4000, 0x200000)` creates a 1.98MB view for LittleFS starting at sector 4.

### 3. bd->init() called exactly once
Only `LittleFSStorage::begin()` → `mount(sliced)` calls `bd->init()`. `SPIFlash::begin()` must NOT call it. Double init leaves MX25R1635F in bad state (reads work, writes fail silently → LFS2_ERR_CORRUPT → -138/EILSEQ).

### 4. O_WRONLY file mode (not O_RDWR)
mbed LittleFileSystem2 returns `-138/ENOTSUP` for `O_RDWR`. RunHeader fields saved in RAM at `create_run()` time and used in `close_run()` — no read-back needed.

### 5. Incremental CRC32
CRC accumulated byte-by-byte in `append_data()`. Finalized in `close_run()`. No seek/read on open file.

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

## Error Codes
- `-138 / EILSEQ` = LFS2_ERR_CORRUPT (flash write verify mismatch — double init or ring/LFS overlap)
- `CR_OPEN_ERR_-138` = create_run failed with f->open() error
- `CR_NO_FS` = filesystem not initialized
- `CR_ALREADY_OPEN` = previous run never closed
- `NOT_OPEN` = close_run called without open file

## Version Bumping
Increment version in `main.cpp` at EVERY code change: `json_kv("ver", "2.X")`. Boot JSON confirms binary was actually flashed.
