/**
 * @file    flash_ring.h
 * @brief   Linear ARMED pre-roll + drain headroom (v4.77).
 *
 * Design (JP):
 *   - TOTAL_SLOTS = 4000 = 3000 ARMED cap (~30 s) + 1000 drain headroom (~10 s)
 *   - prepare_preroll() on enter IDLE + boot: erase all, program-only fill
 *   - ARMED: forward write, stop at 3000 (ARM_FILL_CAP) for product arm window
 *   - LOGGING start: trim_to_newest(PREROLL_KEEP=1000)
 *   - LOGGING drain: pop 2 + push 1 live (net −1/tick → ~10 s to empty 1000)
 *     Live samples need the +1000 headroom when head was already at 3000.
 *
 * Flash: 0x0000–0x13FFF (20 × 4 KB), RawRunStore from 0x14000.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_buffer.h"

static constexpr uint16_t PREROLL_KEEP  = 1000;  /* ~10 s kept at start / phone */
static constexpr uint16_t ARM_FILL_CAP  = 3000;  /* ~30 s ARMED product window */
static constexpr uint16_t DRAIN_HEADROOM = 1000; /* live pushes while draining */
static constexpr uint16_t TOTAL_SLOTS =
    ARM_FILL_CAP + DRAIN_HEADROOM;               /* 4000 */
static constexpr uint16_t MAX_COUNT = TOTAL_SLOTS;

static constexpr uint32_t RING_FLASH_BASE = 0x0000u;
static constexpr uint32_t RING_ENTRY_SIZE = 20u;
/* 4000*20 = 80000 → 20 sectors */
static constexpr uint32_t RING_SECTORS    = 20u;
static constexpr uint32_t RING_FLASH_END  =
    RING_FLASH_BASE + RING_SECTORS * 4096u; /* 0x14000 */

struct __attribute__((packed)) RingEntry {
    RawFrame frame;
    uint32_t arrival_ms;
};

static_assert(sizeof(RingEntry) == 20, "RingEntry must be 20 bytes");

class FlashRing
{
public:
    explicit FlashRing(class SPIFlash& flash);

    void prepare_preroll();
    void reset();
    void clear();

    /** Forward program. arm_limit: if true, refuse past ARM_FILL_CAP (ARMED).
     *  LOGGING drain uses arm_limit=false to use drain headroom. */
    bool write(const RawFrame& f, bool arm_limit = true);

    RawFrame read();
    uint32_t last_read_ts() const { return m_last_ts; }
    bool peek(RawFrame& f) const;

    void trim_to_newest(uint16_t keep);

    bool   is_full()  const { return m_count >= ARM_FILL_CAP; } /* ARMED full */
    bool   is_empty() const { return m_count == 0; }
    bool   at_absolute_end() const { return m_head >= TOTAL_SLOTS; }
    size_t count()    const { return m_count; }
    uint16_t head()   const { return m_head; }
    uint16_t max_count() const { return ARM_FILL_CAP; } /* status rm = arm cap */
    uint16_t total_slots() const { return TOTAL_SLOTS; }
    bool     prepared() const { return m_prepared; }

private:
    SPIFlash& m_flash;
    uint16_t  m_head;
    uint16_t  m_tail;
    uint16_t  m_count;
    uint32_t  m_last_ts;
    bool      m_prepared;

    static uint32_t slot_addr(uint16_t slot) {
        return RING_FLASH_BASE + (uint32_t)slot * RING_ENTRY_SIZE;
    }
};
