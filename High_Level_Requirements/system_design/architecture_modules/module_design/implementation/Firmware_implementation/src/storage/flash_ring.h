/**
 * @file    flash_ring.h
 * @brief   Flash FIFO ring — 500 live frames on 2 erase-halves (1000 slots).
 *
 * Layout (MX25R1635F, 20 B/slot RingEntry):
 *   Half A  slots 0..499    @ 0x0000  sectors 0-2
 *   Half B  slots 500..999  @ 0x3000  sectors 3-5
 *   LittleFS starts at 0x6000
 *
 * Why two halves:
 *   NOR flash cannot rewrite a page without erase. Live window ≤ 500, so
 *   when HEAD enters a half the other half holds all live data — safe to
 *   erase the half being entered.
 *
 * Rules (V4.54 — keep this simple):
 *   1. m_head always in [0, TOTAL_SLOTS)  — ALWAYS advance with % TOTAL_SLOTS
 *   2. m_count always in [0, MAX_COUNT]
 *   3. Before write at head==0 erase Half A; at head==HALF_SIZE erase Half B
 *   4. tail = (head - count) mod TOTAL_SLOTS
 *
 * Bug fixed in V4.54 (enc_baro period = 1000 on v4.53):
 *   Non-full write path did m_head++ with NO modulo. After ~1000 frames
 *   head ran past TOTAL_SLOTS while tail() still wrapped → re-read slot 0.
 *   Pressure sequence repeated every 1000 encoded frames.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_buffer.h"

static constexpr uint16_t MAX_COUNT   = 500;
static constexpr uint16_t HALF_SIZE   = 500;
static constexpr uint16_t TOTAL_SLOTS = 1000;

static constexpr uint32_t HALF_A_BASE    = 0x0000;
static constexpr uint32_t HALF_B_BASE    = 0x3000;
static constexpr uint32_t RING_FLASH_END = 0x6000;  /* LittleFS starts here */

struct __attribute__((packed)) RingEntry {
    RawFrame frame;      /* 16 B */
    uint32_t arrival_ms; /*  4 B — millis() at write time */
};

class FlashRing
{
public:
    explicit FlashRing(class SPIFlash& flash);

    void reset();                  /* erase both halves, clear pointers (boot) */
    void clear();                  /* drop live count only — no erase (POST_RUN) */
    void write(const RawFrame& f); /* push; drops oldest if full */
    RawFrame read();               /* pop oldest; empty → zeroed frame */
    uint32_t last_read_ts() const { return m_last_ts; }
    bool peek(RawFrame& f) const;

    bool   is_full()  const { return m_count >= MAX_COUNT; }
    bool   is_empty() const { return m_count == 0; }
    size_t count()    const { return m_count; }

private:
    SPIFlash& m_flash;
    uint16_t  m_head;    /* next write slot ∈ [0, TOTAL_SLOTS) */
    uint16_t  m_count;   /* live frames ∈ [0, MAX_COUNT] */
    uint32_t  m_last_ts; /* arrival_ms of last read() */

    uint16_t tail() const {
        return (uint16_t)((m_head + TOTAL_SLOTS - m_count) % TOTAL_SLOTS);
    }

    static uint32_t slot_addr(uint16_t slot) {
        if (slot < HALF_SIZE)
            return HALF_A_BASE + (uint32_t)slot * (uint32_t)sizeof(RingEntry);
        return HALF_B_BASE
             + (uint32_t)(slot - HALF_SIZE) * (uint32_t)sizeof(RingEntry);
    }

    void erase_half_a();
    void erase_half_b();
};
