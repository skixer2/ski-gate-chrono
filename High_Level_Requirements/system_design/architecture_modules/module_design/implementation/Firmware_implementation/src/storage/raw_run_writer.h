/**
 * @file    raw_run_writer.h
 * @brief   Opt-A spike (v4.62): pre-erased raw SPI run payload for force-'l' / S04.
 *
 * Layout (MX25R 2 MB):
 *   0x0000–0x5FFF   FlashRing (sectors 0–5)
 *   0x6000–0x1EFFFF LittleFS (stop moved down — see littlefs_storage.cpp)
 *   0x1F0000–0x1FBFFF RAW RUN SLAB (48 KB, this module)
 *   0x1FC000+       config / reserved
 *
 * LOGGING hot path: caller's 256 B page buf → SPI program only
 * (no erase, no LittleFS, no COW). Erases once in begin() before fps clock.
 *
 * Extra BSS: ~16 bytes of scalars only. Page buffer is owned by caller
 * (reuse existing g_page_buf — Cordio RAM ceiling).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

static constexpr uint32_t RAW_RUN_BASE   = 0x1F0000u;
static constexpr uint32_t RAW_RUN_END    = 0x1FC000u;
static constexpr uint32_t RAW_RUN_SIZE   = RAW_RUN_END - RAW_RUN_BASE; /* 49152 */
static constexpr uint32_t RAW_RUN_SECTOR = 4096u;

class RawRunWriter
{
public:
    RawRunWriter();

    /**
     * Pre-erase the entire slab and reset write pointer.
     * Call at force-'l' LOGGING entry BEFORE the rate window.
     */
    bool begin(class SPIFlash& flash);

    /**
     * Program exactly `len` bytes at the current cursor (must be ≤ 256 and
     * fit in remaining slab). Caller flushes full pages; close() may pad.
     * Prefer flush_page() for the normal 256 B path.
     */
    bool program(const uint8_t* data, size_t len);

    /** Finalize: no more programs. Returns total payload bytes written. */
    uint32_t close();

    bool     active() const { return m_active; }
    uint32_t bytes()  const { return m_bytes; }
    uint32_t write_err() const { return m_write_err; }
    uint32_t remaining() const {
        return (m_addr < RAW_RUN_END) ? (RAW_RUN_END - m_addr) : 0;
    }

private:
    class SPIFlash* m_flash;
    bool     m_active;
    uint32_t m_addr;
    uint32_t m_bytes;
    uint32_t m_write_err;
};
