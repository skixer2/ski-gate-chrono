/**
 * @file    flash_ring.h
 * @brief   Flash FIFO pre-roll — 3 regions × 500 slots, live window 1000 (v4.73).
 *
 * Layout (MX25R1635F, 20 B/slot RingEntry):
 *   Region 0  slots    0..499   @ 0x0000  sectors 0-2
 *   Region 1  slots  500..999   @ 0x3000  sectors 3-5
 *   Region 2  slots 1000..1499  @ 0x6000  sectors 6-8
 *   RawRunStore starts at 0x9000
 *
 * Why three regions:
 *   NOR cannot rewrite without erase. Live window = 1000 (≈10 s @ 100 Hz).
 *   With only 2×500, live would span both halves at wrap → erase kills history.
 *   With 3×500, live ≤ 1000 always leaves ≥1 region with no live frames to erase.
 *
 * Rules:
 *   1. m_head always in [0, TOTAL_SLOTS)
 *   2. m_count always in [0, MAX_COUNT]
 *   3. Before write at head == k*REGION_SIZE, erase region k
 *   4. tail = (head - count) mod TOTAL_SLOTS
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_buffer.h"

static constexpr uint16_t REGION_SIZE  = 500;
static constexpr uint16_t NUM_REGIONS  = 3;
static constexpr uint16_t MAX_COUNT    = 1000;  /* ≈10 s @ 100 Hz */
static constexpr uint16_t TOTAL_SLOTS = REGION_SIZE * NUM_REGIONS; /* 1500 */

static constexpr uint32_t REGION_BYTES =
    (uint32_t)REGION_SIZE * 20u; /* RingEntry size; keep in sync with struct */
/* 500*20=10000 → use 3 sectors (12288) per region */
static constexpr uint32_t REGION_STRIDE = 0x3000u; /* 3 × 4096 */

static constexpr uint32_t RING_FLASH_BASE = 0x0000u;
static constexpr uint32_t RING_FLASH_END  = RING_FLASH_BASE + NUM_REGIONS * REGION_STRIDE; /* 0x9000 */

struct __attribute__((packed)) RingEntry {
    RawFrame frame;      /* 16 B */
    uint32_t arrival_ms; /*  4 B — millis() at write time */
};

static_assert(sizeof(RingEntry) == 20, "RingEntry must be 20 bytes");

class FlashRing
{
public:
    explicit FlashRing(class SPIFlash& flash);

    void reset();                  /* erase all regions, clear pointers (boot) */
    void clear();                  /* drop live count only — no erase (POST_RUN) */
    void write(const RawFrame& f); /* push; drops oldest if full */
    RawFrame read();               /* pop oldest; empty → zeroed frame */
    uint32_t last_read_ts() const { return m_last_ts; }
    bool peek(RawFrame& f) const;

    bool   is_full()  const { return m_count >= MAX_COUNT; }
    bool   is_empty() const { return m_count == 0; }
    size_t count()    const { return m_count; }
    uint16_t head()   const { return m_head; }
    uint16_t max_count() const { return MAX_COUNT; }

private:
    SPIFlash& m_flash;
    uint16_t  m_head;    /* next write slot ∈ [0, TOTAL_SLOTS) */
    uint16_t  m_count;   /* live frames ∈ [0, MAX_COUNT] */
    uint32_t  m_last_ts; /* arrival_ms of last read() */

    uint16_t tail() const {
        return (uint16_t)((m_head + TOTAL_SLOTS - m_count) % TOTAL_SLOTS);
    }

    static uint32_t slot_addr(uint16_t slot) {
        uint16_t reg = (uint16_t)(slot / REGION_SIZE);
        uint16_t off = (uint16_t)(slot % REGION_SIZE);
        return RING_FLASH_BASE + (uint32_t)reg * REGION_STRIDE
             + (uint32_t)off * (uint32_t)sizeof(RingEntry);
    }

    void erase_region(uint16_t reg);
};
