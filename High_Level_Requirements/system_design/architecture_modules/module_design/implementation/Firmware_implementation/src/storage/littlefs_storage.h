/**
 * @file    littlefs_storage.h
 * @brief   Run storage on LittleFS v2 — replaces FlashManager (circular raw flash).
 *
 *   Runs stored as files: /run_NNNNN.dat
 *   Each file (format_ver=2, V2.80+):
 *     [RunHeader 16B — data_size=0,frame_count=0 at create]
 *     [compressed frames ...]
 *     [0xC3][0x32][CRC32 LE 4B]  — 6-byte trailer
 *
 *   APPEND-ONLY: no seek(0)+write(header) in close_run().
 *   data_size = file.size() - 16 - 6 (computed at scan time).
 *   Eliminates COW cascade (LittleFS in-place write copies entire file).
 *
 *   Wear-leveling, power-loss resilience handled by LittleFS.
 *
 * @requires mbed::BlockDevice (already initialized via SPIFlash::begin()).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* ── On-disk data structures (shared with BLE transfer / serial dump) ──── */

/**
 * Run file header (16 bytes, packed).
 *
 * format_ver=2: data_size and frame_count are always 0 in the on-disk
 * header (append-only). Real data_size = file.size() - 22.
 * format_ver=1: data_size and frame_count written by seek(0)+rewrite on close.
 */
struct __attribute__((packed)) RunHeader {
    uint8_t  format_ver;     /* 1=legacy, 2=append-only */
    uint8_t  arm_side;       /* 0=left, 1=right */
    uint32_t ts_utc;         /* UNIX timestamp at run start */
    int16_t  baro_temp;      /* barometric temperature, tenths of °C */
    uint32_t data_size;      /* 0 on-disk for v2; computed from file.size() */
    uint16_t frame_count;    /* 0 on-disk for v2 */
    uint8_t  cal_accuracy;   /* BHI260AP fusion accuracy 0-3 */
    uint8_t  _pad;           /* align → 16 bytes */
};

/**
 * Run metadata entry (32 bytes, packed) for run list / BLE enumeration.
 */
struct __attribute__((packed)) RunEntry {
    uint16_t run_id;
    uint32_t page_start;     /* unused (LittleFS) — kept for struct compat */
    uint32_t page_end;       /* unused (LittleFS) — kept for struct compat */
    uint32_t timestamp;      /* UNIX timestamp at run start */
    uint8_t  arm_side;
    uint8_t  format_version;
    uint32_t compressed_size;
    uint32_t frame_count;
    uint8_t  _reserved[8];
};

/* CRC32 trailer (appended at end of every run file) */
static constexpr uint8_t  CRC32_MAGIC_HI    = 0xC3;
static constexpr uint8_t  CRC32_MAGIC_LO    = 0x32;
static constexpr uint32_t CRC32_TRAILER_SIZE = 6;  /* magic(2) + crc32(4) */

/* ── LittleFSStorage class ─────────────────────────────────────── */

class LittleFSStorage
{
public:
    LittleFSStorage();
    ~LittleFSStorage();

    /**
     * Mount LittleFS on the default BlockDevice. Formats if first boot.
     * Must be called after SPIFlash::begin().
     * Returns true on success.
     */
    bool begin();

    /* ── Run lifecycle ────────────────────────────────────────── */

    /**
     * Create a new run file and write the RunHeader.
     * If flash is full, deletes oldest run(s) to make space.
     * Returns true on success. After this, use append_data() to write frames.
     */
    bool create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy);

    /**
     * Append compressed frame data to the open run file.
     * Buffered in RAM — flush to flash only when buffer is full
     * or at close_run() to avoid blocking the main loop.
     */
    bool append_data(const uint8_t* data, size_t len);

    /**
     * Flush the write buffer to flash immediately.
     * Called automatically when buffer fills or at close_run().
     */
    void flush_write_buf();

    /**
     * Finalize the run: flush buffer, write CRC32 trailer, sync, close.
     * APPEND-ONLY (format_ver=2): NO seek(0)+write(header) — eliminates
     * LittleFS COW cascade that caused data_size=0 on power loss.
     * Returns the new run_id, or 0xFFFF on failure.
     */
    uint16_t close_run(uint32_t frame_count);

    /* ── Queries ──────────────────────────────────────────────── */

    /** Number of valid runs currently stored. */
    uint16_t run_count() const { return m_entry_count; }

    /** Total runs ever created (monotonic counter). */
    uint16_t total_run_count() const { return m_next_run_id; }

    /** Compressed bytes written to current open run (before CRC trailer). */
    uint32_t run_bytes() const { return m_run_bytes; }

    /** Flash used percentage (0-100). */
    uint8_t flash_used_pct() const;

    /** Age of oldest run (UNIX timestamp), or 0 if empty. */
    uint32_t oldest_run_age() const;

    /* ── File access (BLE transfer, serial dump) ──────────────── */

    /**
     * Read a RunHeader for the given run_id.
     */
    bool read_run_header(uint16_t run_id, RunHeader& hdr) const;

    /**
     * Read data from a run file at byte offset (header-relative, 0 = first byte of RunHeader).
     */
    bool read_run_data(uint16_t run_id, uint32_t offset, uint8_t* buf, size_t len) const;

    /* ── Run list ─────────────────────────────────────────────── */

    /** Get an entry by index (0-based, sorted by run_id). Returns nullptr if out of range. */
    const RunEntry* get_entry(uint16_t idx) const;

    /** Get an entry by run_id. Returns nullptr if not found. */
    const RunEntry* get_entry_by_id(uint16_t run_id) const;

    /**
     * Build a JSON run list. Format:
     *   [{"id":0,"ts":1234567890,"size":43000,"side":"left"}, ...]
     */
    const char* build_run_list(char* buf, size_t buf_size) const;

    /* ── Factory reset ────────────────────────────────────────── */

    /** Format the filesystem — deletes ALL runs. */
    void erase_all();

    /** V2.63: Flush FS metadata for clean reboot. */
    void unmount();

    /** V2.69: Force superblock/directory metadata commit (silent). */
    void metadata_sync();

    /** V2.21: List all files in the LittleFS root dir via Serial (debug). */
    void list_files() const;

    /** V4.20: Access underlying filesystem for single-pass file reads. */
    void* get_fs() const { return m_fs; }

    /** Delete the oldest run (by timestamp) to make space.
     *  V2.19: public — called from main.cpp at ARM time, not from
     *  create_run() at LOGGING entry. */
    void delete_oldest_run();

    /* ── CRC32 (stateless helpers) ────────────────────────────── */

    static uint32_t crc32_update(uint32_t crc, uint8_t byte);
    static uint32_t crc32_buffer(const uint8_t* data, size_t len);
    static uint32_t crc32_initial()  { return 0xFFFFFFFF; }
    static uint32_t crc32_finalize(uint32_t crc) { return crc ^ 0xFFFFFFFF; }

    /* V2.95: scan_runs() is public — called from status command (?)
       to ensure flash-truth counts. NOT called during normal operation
       (BLE updates, transitions) to avoid concurrent read+write on
       files that may be open for logging. */
    void scan_runs();

private:
    /* Opaque pointers — mbed types included only in .cpp */
    void* m_fs;        /* mbed::LittleFileSystem* */
    void* m_bd;        /* mbed::BlockDevice*        */
    void* m_file;      /* fs_file_t* (open during logging) */

    bool   m_file_open;
    uint16_t m_run_count;     /* runs created (monotonic, survives reboots) */
    uint16_t m_next_run_id;
    uint32_t m_run_bytes;     /* compressed bytes in current open run */
    uint32_t m_run_crc;       /* incremental CRC32 */

    /* Write buffer: accumulate compressed data in RAM and flush to flash
     * in batches to avoid blocking the 100 Hz feed_sensors() loop during
     * LOGGING. Each LittleFS f->write() can take 1-10ms+. */
    static constexpr size_t WRITE_BUF_SIZE = 256;
    uint8_t  m_write_buf[WRITE_BUF_SIZE];
    uint16_t m_write_buf_pos;

    /* Pending run metadata (set in create_run, consumed by close_run
     * for RunEntry population). No longer used for header rewrite
     * (V2.80: append-only format, no seek+write on close). */
    uint8_t  m_pending_arm_side;
    int16_t  m_pending_baro_temp;
    uint8_t  m_pending_cal_accuracy;

    /* Cached run list (populated by scan_runs()) */
    static constexpr uint16_t MAX_ENTRIES = 8;   /* 8 runs = 8 KB buffer (enough per session) */
    RunEntry m_entries[MAX_ENTRIES];
    uint16_t m_entry_count;

    /* Internal helpers */
    void write_run_list_entry(uint16_t id, const RunEntry& entry);
    int  find_entry_idx(uint16_t run_id) const;
    void remove_entry_at(int idx);
};
