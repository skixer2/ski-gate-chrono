/**
 * @file    raw_run_store.h
 * @brief   Opt-A production run storage — pre-erased raw SPI slots (v4.63/v5.01).
 *
 * Replaces LittleFS for run payloads. Proven by S04 @ 99.4 fps (v4.62 spike).
 *
 * Flash map (size-aware, v5.01 — pre-roll FIXED):
 *   0x0000–0x13FFF     Linear pre-roll 4000×20B (FlashRing — unchanged)
 *   0x14000–config     Run slots (N × ~244 KB; N from SFDP chip size)
 *   chip-0x4000        Config
 *   chip-0x3000        Index (RRS2)
 *   chip-0x2000        SPI self-test
 *
 * Classic 2 MB → N=8, addresses identical to v4.77–v5.00.
 * 4 MB → 16 slots; 8 MB → 32 slots (cap).
 *
 * On-disk run (BLE-compatible):
 *   [RunHeader 16B][compressed frames…][0xC3 0x32 CRC32_LE 4B]
 *
 * Hot path: program() only.
 * Erases: prepare_next_run() full-slot erase at POST_RUN (10 s) or boot — NOT ARM.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "littlefs_storage.h"  /* RunHeader, RunEntry, CRC32_* */
#include "flash_layout.h"

/* Back-compat aliases used by comments/tests. Prefer flash_layout() at runtime. */
static constexpr uint32_t RRS_SECTOR    = FLASH_SECTOR_SIZE;
static constexpr uint16_t RRS_MAX_SLOTS = RRS_MAX_SLOTS_CAP; /* array dim only */

class RawRunStore
{
public:
    RawRunStore();

    /** Bind flash, init layout from SFDP size, load index (or empty). */
    bool begin(class SPIFlash& flash);

    /**
     * Pick free/oldest slot and pre-erase the FULL slot.
     * Call from POST_RUN (10 s cooldown) or boot — NOT from ARM.
     * Safe to call multiple times; no-op if already prepared and unused.
     */
    bool prepare_next_run();

    /**
     * Start a run on the prepared slot: write RunHeader, reset cursor.
     * If nothing prepared, prepares now (S04 force-l path).
     */
    bool create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy, uint32_t ts_utc);

    /** Append compressed bytes (page-buffer flushes). */
    bool append_data(const uint8_t* data, size_t len);

    void flush_write_buf() { /* no internal buf — caller owns page */ }

    /**
     * Write CRC trailer, commit index entry. Returns run_id or 0xFFFF.
     */
    uint16_t close_run(uint32_t frame_count);

    bool     active() const { return m_writing; }
    uint32_t run_bytes() const { return m_payload_bytes; }
    uint32_t write_err() const { return m_write_err; }

    uint16_t run_count() const { return m_entry_count; }
    uint16_t total_run_count() const { return m_next_run_id; }
    uint16_t max_slots() const { return m_max_slots; }
    uint32_t slot_size() const { return m_slot_size; }

    uint8_t  flash_used_pct() const;
    uint32_t oldest_run_age() const;

    bool read_run_header(uint16_t run_id, RunHeader& hdr) const;
    bool read_run_data(uint16_t run_id, uint32_t offset, uint8_t* buf, size_t len) const;

    const RunEntry* get_entry(uint16_t idx) const;
    const RunEntry* get_entry_by_id(uint16_t run_id) const;

    const char* build_run_list(char* buf, size_t buf_size) const;

    void erase_all();
    void unmount() {}
    void metadata_sync() { persist_index(); }
    void list_files() const;
    void delete_oldest_run();
    void ensure_space_for_new_run();
    void scan_runs() { /* RAM index is authoritative; reload optional */ load_index(); }

    static uint32_t crc32_update(uint32_t crc, uint8_t byte);
    static uint32_t crc32_buffer(const uint8_t* data, size_t len);
    static uint32_t crc32_initial() { return 0xFFFFFFFF; }
    static uint32_t crc32_finalize(uint32_t crc) { return crc ^ 0xFFFFFFFF; }

private:
    /* RRS2: 32-bit slot mask (was RRS1 8-bit). 2 MB chips still N=8. */
    static constexpr uint32_t INDEX_MAGIC = 0x52525332u; /* "RRS2" */
    static constexpr uint32_t INDEX_MAGIC_LEGACY = 0x52525331u; /* "RRS1" */

    struct __attribute__((packed)) IndexHeader {
        uint32_t magic;
        uint16_t next_run_id;
        uint16_t entry_count;
        uint32_t slot_used_mask;  /* bit i = slot i has a live run */
        uint16_t max_slots;       /* geometry that wrote this index */
        uint8_t  _pad[2];
    };

    class SPIFlash* m_flash;
    bool     m_ok;
    bool     m_writing;
    bool     m_prepared;
    uint8_t  m_prep_slot;
    uint8_t  m_write_slot;
    uint16_t m_write_run_id;
    uint32_t m_slot_base;
    uint32_t m_slot_end;
    uint32_t m_cursor;         /* absolute addr next program */
    uint32_t m_erased_end;     /* exclusive end of pre-erased region in slot */
    uint32_t m_payload_bytes;  /* bytes after header */
    uint32_t m_run_crc;
    uint32_t m_write_err;
    uint8_t  m_pending_arm_side;
    int16_t  m_pending_baro_temp;
    uint8_t  m_pending_cal;
    uint32_t m_pending_ts_utc;

    uint16_t m_next_run_id;
    uint16_t m_entry_count;
    uint32_t m_slot_used_mask;
    uint16_t m_max_slots;
    uint32_t m_slot_size;
    uint32_t m_index_addr;
    RunEntry m_entries[RRS_MAX_SLOTS_CAP];
    uint8_t  m_entry_slot[RRS_MAX_SLOTS_CAP]; /* slot index for m_entries[i] */

    uint32_t slot_addr(uint8_t slot) const {
        return flash_data_base() + (uint32_t)slot * m_slot_size;
    }
    uint32_t slot_sectors() const { return m_slot_size / RRS_SECTOR; }

    bool erase_range(uint32_t addr, uint32_t len);
    bool ensure_erased(uint32_t need_end); /* lazy sector erase ahead of cursor */
    bool program_raw(uint32_t addr, const uint8_t* data, size_t len);
    int  find_entry_idx(uint16_t run_id) const;
    int  find_free_slot() const;
    int  find_oldest_entry_idx() const;
    void remove_entry_at(int idx);
    bool load_index();
    bool persist_index();
};
