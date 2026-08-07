/**
 * @file    flash_ring.h
 * @brief   Linear ARMED pre-roll buffer (v4.75) — forward program only.
 *
 * Design (JP):
 *   - Capacity TOTAL_SLOTS = 3000 ≈ full ARM_TIMEOUT (30 s @ 100 Hz)
 *   - No wrap, no erase during ARMED fill (stable ~100 Hz)
 *   - prepare_preroll(): erase entire buffer on enter IDLE (+ boot)
 *   - On start: keep only newest PREROLL_KEEP (1000 ≈ 10 s) for encode/phone
 *
 * Flash map (MX25R 2 MB):
 *   0x0000–0xEFFF   Pre-roll 3000 × 20 B RingEntry (15 × 4 KB sectors)
 *   0xF000+         RawRunStore (see raw_run_store.h)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_buffer.h"

static constexpr uint16_t PREROLL_KEEP  = 1000;  /* frames delivered at start (~10 s) */
static constexpr uint16_t TOTAL_SLOTS = 3000;  /* max ARMED fill (~30 s @ 100 Hz) */
static constexpr uint16_t MAX_COUNT     = TOTAL_SLOTS;

static constexpr uint32_t RING_FLASH_BASE = 0x0000u;
static constexpr uint32_t RING_ENTRY_SIZE = 20u;
/* 3000*20 = 60000 → ceil to 15 sectors */
static constexpr uint32_t RING_SECTORS    = 15u;
static constexpr uint32_t RING_FLASH_END  =
    RING_FLASH_BASE + RING_SECTORS * 4096u; /* 0xF000 */

struct __attribute__((packed)) RingEntry {
    RawFrame frame;      /* 16 B */
    uint32_t arrival_ms; /*  4 B — millis() at write time */
};

static_assert(sizeof(RingEntry) == 20, "RingEntry must be 20 bytes");

class FlashRing
{
public:
    explicit FlashRing(class SPIFlash& flash);

    /** Erase all pre-roll sectors + reset pointers. Call on enter IDLE / boot. */
    void prepare_preroll();

    void reset();   /* alias prepare_preroll for boot */
    void clear();   /* soft: drop count/head without erase (rare) */

    void write(const RawFrame& f); /* forward program only; no-op if full */
    RawFrame read();               /* pop oldest */
    uint32_t last_read_ts() const { return m_last_ts; }
    bool peek(RawFrame& f) const;

    /** Drop oldest until count <= keep (before drain encode). */
    void trim_to_newest(uint16_t keep);

    bool   is_full()  const { return m_count >= TOTAL_SLOTS; }
    bool   is_empty() const { return m_count == 0; }
    size_t count()    const { return m_count; }
    uint16_t head()   const { return m_head; }      /* next write index */
    uint16_t max_count() const { return TOTAL_SLOTS; }
    bool     prepared() const { return m_prepared; }

private:
    SPIFlash& m_flash;
    uint16_t  m_head;     /* next write index ∈ [0, TOTAL_SLOTS] */
    uint16_t  m_tail;     /* oldest live index */
    uint16_t  m_count;
    uint32_t  m_last_ts;
    bool      m_prepared; /* true after prepare_preroll until dirtied */

    static uint32_t slot_addr(uint16_t slot) {
        return RING_FLASH_BASE + (uint32_t)slot * RING_ENTRY_SIZE;
    }
};
