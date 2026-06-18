/**
 * @file    flash_ring.h
 * @brief   Flash-based ring buffer — 4 blocks, 1000-slot circular region.
 *          Always holds the most recent 500 RawFrames (5 s at 100 Hz).
 *
 *   Half A: slots 0-499   (blocks 0-1, addr 0x0000-0x1FFF)
 *   Half B: slots 500-999 (blocks 2-3, addr 0x2000-0x3FFF)
 *
 *   Algorithm:
 *     1. Fill to 500 (HEAD advances, COUNT grows)
 *     2. At HEAD=500: erase Half B (blocks 2-3), HEAD=501, COUNT=500, TAIL=1
 *     3. HEAD advances through Half B (501-999)
 *     4. At HEAD=1000: erase Half A (blocks 0-1), HEAD=0, COUNT=500, TAIL=500
 *     5. Repeat: HEAD 0→499, then erase Half B at HEAD=500, etc.
 *
 *   Physical: 4 blocks × 4096B = 16384B = 1024 slots × 16B (use 1000)
 *   Run data: starts at block 4 (addr 0x4000)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_buffer.h"

static constexpr uint16_t MAX_COUNT        = 500;
static constexpr uint16_t HALF_SIZE        = 500;   /* slots per half */
static constexpr uint16_t TOTAL_SLOTS      = 1000;  /* 2 halves */
static constexpr uint32_t RING_FLASH_START = 0;
static constexpr uint32_t RING_FLASH_SIZE  = 4 * 4096;     /* 16KB */

/* Run data starts at block 5 (block 4 = index sector). Defined in main.cpp. */

class FlashRing
{
public:
    FlashRing(class SPIFlash& flash);

    void reset();                  /* erase all 4 blocks, reset pointers */
    void write(const RawFrame& f); /* write one frame at HEAD */
    RawFrame read();               /* read oldest frame at TAIL */

    bool is_full()  const { return m_count >= MAX_COUNT; }
    bool is_empty() const { return m_count == 0; }
    size_t count()  const { return m_count; }

private:
    SPIFlash& m_flash;
    uint16_t  m_head;    /* next write slot (0..999) */
    uint16_t  m_count;   /* 0..500 */

    uint16_t tail() const { return (m_head - m_count + TOTAL_SLOTS) % TOTAL_SLOTS; }
    uint32_t  slot_addr(uint16_t slot) const {
        return RING_FLASH_START + slot * sizeof(RawFrame);
    }
};
