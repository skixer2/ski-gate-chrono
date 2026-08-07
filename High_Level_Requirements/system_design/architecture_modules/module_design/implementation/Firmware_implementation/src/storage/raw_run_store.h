/**
 * @file    raw_run_store.h
 * @brief   Opt-A production run storage — pre-erased raw SPI slots (v4.63/v4.64).
 *
 * Replaces LittleFS for run payloads. Proven by S04 @ 99.4 fps (v4.62 spike).
 *
 * Flash map (MX25R 2 MB, v4.75+):
 *   0x0000–0xEFFF     Linear pre-roll 3000×20B (FlashRing)
 *   0xF000–0x1FBFFF   Run slots (8 × sector-aligned)
 *   0x1FC000          Config (BLE name etc. — sgc_service)
 *   0x1FD000          Run index (this module)
 *   0x1FE000–0x1FFFFF reserved
 *
 * On-disk run (BLE-compatible):
 *   [RunHeader 16B][compressed frames…][0xC3 0x32 CRC32_LE 4B]
 *
 * Hot path: program() only.
 * Erases: prepare_next_run() full-slot erase at POST_RUN (10 s) or boot — NOT ARM.
 * Extra BSS: 8×RunEntry + scalars (~300 B). No large RAM ring.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "littlefs_storage.h"  /* RunHeader, RunEntry, CRC32_* */

static constexpr uint32_t RRS_DATA_BASE   = 0xF000u;  /* after linear pre-roll 0x0000–0xEFFF */
static constexpr uint32_t RRS_DATA_END    = 0x1FC000u;
static constexpr uint32_t RRS_INDEX_ADDR  = 0x1FD000u;  /* sector 509 */
static constexpr uint32_t RRS_SECTOR      = 4096u;
static constexpr uint16_t RRS_MAX_SLOTS   = 8;

/* (0x1FC000-0xF000)/8 → floor to sector multiple */
static constexpr uint32_t RRS_SLOT_SIZE   =
    ((RRS_DATA_END - RRS_DATA_BASE) / RRS_MAX_SLOTS / RRS_SECTOR) * RRS_SECTOR;

class RawRunStore
{
public:
    RawRunStore();

    /** Bind flash + load index from sector 509 (or empty). */
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
    bool create_run(uint8_t arm_side, int16_t baro_temp, uint8_t cal_accuracy);

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
    static constexpr uint32_t INDEX_MAGIC = 0x52525331u; /* "RRS1" */

    struct __attribute__((packed)) IndexHeader {
        uint32_t magic;
        uint16_t next_run_id;
        uint16_t entry_count;
        uint8_t  slot_used_mask;  /* bit i = slot i has a live run */
        uint8_t  _pad[7];
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

    uint16_t m_next_run_id;
    uint16_t m_entry_count;
    uint8_t  m_slot_used_mask;
    RunEntry m_entries[RRS_MAX_SLOTS];
    uint8_t  m_entry_slot[RRS_MAX_SLOTS]; /* slot index for m_entries[i] */

    static uint32_t slot_addr(uint8_t slot) {
        return RRS_DATA_BASE + (uint32_t)slot * RRS_SLOT_SIZE;
    }
    static uint32_t slot_sectors() { return RRS_SLOT_SIZE / RRS_SECTOR; }

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
