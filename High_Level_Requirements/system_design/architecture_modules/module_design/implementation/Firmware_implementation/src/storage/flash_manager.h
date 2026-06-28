/**
 * @file    flash_manager.h
 * @brief   Circular flash storage manager — read/write heads, auto-overwrite
 *          of oldest run when full (F08), CRC32 per run, power-loss recovery.
 *
 * Implements sgc_architecture_devices.md §4 and sgc_system_design.md §5.
 *
 * Flash Layout (MX25R1635F, 2 MB = 512 sectors × 4096 bytes):
 *   Sectors 0-3:    Flash ring buffer (16 KB, managed by flash_ring.cpp)
 *   Sectors 4-509:  Run data — circular, starts at address RUN_DATA_START
 *   Sectors 510-511: Index sector (8 KB, reserved)
 *
 * Index Sector (last 8 KB):
 *   Byte 0-3:    Magic: 0x53474300 ("SGC\0")
 *   Byte 4-5:    run_count (uint16) — total runs ever created
 *   Byte 6-9:    read_head (uint32) — absolute addr of oldest valid run, or 0 if empty
 *   Byte 10-13:  write_head (uint32) — absolute addr of next free space
 *   Byte 14-17:  write_counter (uint32) — incremented on close_run()
 *   Byte 18-19:  entry_count (uint16) — number of valid entries in the run table
 *   Byte 20-31:  reserved
 *
 *   Run entry array (from byte 32, 32 bytes each, max entries = (8192-32)/32 = 255):
 *     Byte 0-1:   run_id (uint16)
 *     Byte 2-5:   page_start (uint32) — absolute flash addr of run start
 *     Byte 6-9:   page_end (uint32) — absolute flash addr of run end
 *     Byte 10-13: timestamp (uint32, unixtime)
 *     Byte 14:    arm_side (uint8, 0=left, 1=right)
 *     Byte 15:    format_version (uint8)
 *     Byte 16-19: compressed_size (uint32)
 *     Byte 20-23: frame_count (uint32)
 *     Byte 24-31: reserved
 *
 * Run File On-Disk Format:
 *   [16-byte RunHeader] [compressed frames ...] [0xC3] [0x32] [CRC32_LE:4B]
 *   CRC32 covers compressed frames only (from header end to magic bytes).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* ── Flash geometry ──────────────────────────────────────────── */
static constexpr uint32_t FLASH_TOTAL_SIZE  = 2 * 1024 * 1024;   /* 2 MB */
static constexpr uint32_t FLASH_SECTOR_SIZE = 4096;
static constexpr uint32_t FLASH_TOTAL_SECTORS = 512;
static constexpr uint32_t FLASH_PAGE_SIZE   = 256;

/* Ring buffer: sectors 0-3 (defined in flash_ring.h) — only use forward refs */
/* Run data: sectors 4 through 509 (506 sectors = 2,023,424 bytes) */
static constexpr uint32_t RUN_DATA_START    = 4 * 4096;          /* 0x4000 */
static constexpr uint32_t RUN_DATA_SECTORS  = 506;
static constexpr uint32_t RUN_DATA_SIZE     = RUN_DATA_SECTORS * FLASH_SECTOR_SIZE;

/* Index: sectors 510-511 (2 × 4096 = 8 KB) */
static constexpr uint32_t INDEX_SECTOR      = 510;
static constexpr uint32_t INDEX_ADDR        = INDEX_SECTOR * FLASH_SECTOR_SIZE;  /* 0x1FE000 */
static constexpr uint32_t INDEX_SIZE        = 2 * FLASH_SECTOR_SIZE;             /* 8192 */

/* CRC32 trailer */
static constexpr uint8_t  CRC32_MAGIC_HI    = 0xC3;
static constexpr uint8_t  CRC32_MAGIC_LO    = 0x32;
static constexpr uint32_t CRC32_TRAILER_SIZE = 6;  /* magic(2) + crc32(4) */

/* ── Data structures ──────────────────────────────────────────── */

/**
 * Run file header (16 bytes, packed).
 * Written at the start of each run. Phone uses format_version for
 * backward compatibility. data_size is uint32 (fixed from uint16 in Phase 7b).
 */
struct __attribute__((packed)) RunHeader {
    uint8_t  format_ver;     /* 1 */
    uint8_t  arm_side;       /* 0=left, 1=right */
    uint32_t ts_utc;         /* UNIX timestamp at run start */
    int16_t  baro_temp;      /* barometric temperature, tenths of °C */
    uint32_t data_size;      /* compressed data size (excl. header+trailer), uint32 per Phase 7b */
    uint16_t frame_count;    /* number of compressed frames in the run */
    uint8_t  cal_accuracy;   /* BHI260AP fusion accuracy 0-3 */
    uint8_t  _pad;           /* align → 16 bytes */
};

/**
 * Run metadata entry (32 bytes, packed) in the index sector.
 * One entry per run. Entries are ordered by run_id (not by flash address).
 */
struct __attribute__((packed)) RunEntry {
    uint16_t run_id;
    uint32_t page_start;     /* absolute flash address */
    uint32_t page_end;       /* absolute flash address (first byte after run) */
    uint32_t timestamp;      /* UNIX timestamp at run start */
    uint8_t  arm_side;
    uint8_t  format_version;
    uint32_t compressed_size;
    uint32_t frame_count;
    uint8_t  _reserved[8];
};

/**
 * Persistent index (32 bytes header + up to 255 run entries).
 */
struct __attribute__((packed)) FlashIndex {
    uint32_t magic;          /* 0x53474300 */
    uint16_t run_count;      /* total runs ever created */
    uint32_t read_head;      /* abs addr of oldest run, 0 if empty */
    uint32_t write_head;     /* abs addr of next free byte */
    uint32_t write_counter;  /* incremented on close_run() */
    uint16_t entry_count;    /* entries in the run table */
    uint8_t  _reserved[10];
};

static constexpr size_t   MAX_ENTRIES  = 16;   /* 16 × 32B = 512B RAM; flash holds ~18 DH runs */

/* ── FlashManager class ───────────────────────────────────────── */

class FlashManager {
public:
    FlashManager(class SPIFlash& flash);

    /**
     * Initialize: read index, recover if corrupted. Call once after SPIFlash::begin().
     * Returns true if index was valid, false if recovered or fresh.
     */
    bool begin();

    /**
     * Prepare a new run at write_head.
     *   - Estimates run size (108 KB worst-case)
     *   - Checks if write_head + estimate overlaps read_head → deletes oldest run(s)
     *   - Erases the target sector(s) before LOGGING starts
     *   - Writes the RunHeader
     * Returns the flash address where compressed frames should be written,
     * or 0 on failure (flash full/corrupt).
     */
    uint32_t create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy);

    /**
     * Finalize the run at run_start.
     *   - Writes CRC32 trailer (magic bytes + CRC32) after compressed data
     *   - Updates the index sector (increment run_count, advance write_head)
     *   - If write_head wraps past end of run data, wraps to RUN_DATA_START
     *   - Aligns write_head to next sector boundary
     * Returns the run_id, or 0 on failure.
     */
    uint16_t close_run(uint32_t run_start, uint32_t compressed_size, uint32_t frame_count);

    /* ── Queries ──────────────────────────────────────────────── */

    /** Number of valid runs currently stored (0..255). */
    uint16_t run_count() const { return m_entry_count; }

    /** Total runs ever created (monotonic counter). */
    uint16_t total_run_count() const { return m_index.run_count; }

    /** Flash used percentage (0-100). */
    uint8_t flash_used_pct() const;

    /** Age of oldest run (UNIX timestamp), or 0 if empty. */
    uint32_t oldest_run_age() const;

    /** Next run_id to assign. */
    uint16_t next_run_id() const { return m_index.run_count; }

    /** Read write_head (for diagnostics). */
    uint32_t write_head() const { return m_index.write_head; }

    /** Read read_head (for diagnostics). */
    uint32_t read_head() const { return m_index.read_head; }

    /**
     * Read a RunHeader at the given absolute flash address.
     * Returns true if the header passes sanity checks.
     */
    bool read_run_header(uint32_t addr, RunHeader& hdr) const;

    /**
     * Read a compressed frame chunk from flash.
     */
    bool read_data(uint32_t addr, uint8_t* buf, size_t len) const;

    /**
     * Write compressed frame data to flash during LOGGING.
     * Auto-erases the sector on first write to a new sector.
     */
    bool write_data(uint32_t addr, const uint8_t* data, size_t len);

    /**
     * Build a JSON run list (up to buf_size bytes). Returns the buffer pointer.
     * Format: [{"id":0,"ts":1234567890,"size":43000,"side":"left"}, ...]
     */
    const char* build_run_list(char* buf, size_t buf_size) const;

    /**
     * Get a RunEntry by index in the table (0-based, oldest first by flash position).
     * Returns nullptr if idx >= entry_count.
     */
    const RunEntry* get_entry(uint16_t idx) const;

    /**
     * Erase ALL run data and reset the index. Factory reset (F42).
     */
    void erase_all();

    /* ── CRC32 ────────────────────────────────────────────────── */

    /** Compute CRC32 over a buffer, updating an existing CRC. */
    static uint32_t crc32_update(uint32_t crc, uint8_t byte);
    static uint32_t crc32_buffer(const uint8_t* data, size_t len);
    static uint32_t crc32_initial() { return 0xFFFFFFFF; }
    static uint32_t crc32_finalize(uint32_t crc) { return crc ^ 0xFFFFFFFF; }

private:
    SPIFlash& m_flash;

    FlashIndex m_index;
    RunEntry   m_entries[MAX_ENTRIES];
    uint16_t   m_entry_count;

    bool       m_index_valid;
    uint32_t   m_last_erased_sector;  /* track which sector was last erased for write_data */

    /* Internal helpers */
    void read_index();
    void write_index();
    bool recover_index();
    int  find_entry_by_id(uint16_t run_id) const;
    void remove_entry(int idx);
    uint32_t wrap_address(uint32_t addr) const;
    uint32_t sector_of(uint32_t addr) const;
    bool ensure_erased(uint32_t addr);
    uint32_t advance_write_head(uint32_t run_start, uint32_t compressed_size);
};
