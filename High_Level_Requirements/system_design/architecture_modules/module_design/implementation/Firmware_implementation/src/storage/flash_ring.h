/**
 * @file    flash_ring.h
 * @brief   Flash-based ring buffer — 500-frame sliding window on external flash.
 *          Always holds the most recent 500 frames (5 s at 100 Hz).
 *
 *   Half A: slots   0-499 (sectors 0-2, addr 0x0000-0x2FFF)
 *   Half B: slots 500-999 (sectors 3-5, addr 0x3000-0x5FFF)
 *
 *   V4.07: Each slot stores a RingEntry (RawFrame + uint32_t arrival_ms,
 *   20 bytes).  The arrival timestamp is written at frame arrival time
 *   and read back at pop time — eliminating the ~50% delta=0 bug when
 *   popping 2 frames per iteration.
 *
 *   Algorithm:
 *     1. Fill to 500 (HEAD advances, COUNT grows)
 *     2. At HEAD=500: erase Half B (sectors 3-5), HEAD=501, COUNT=500, TAIL=1
 *     3. HEAD advances through Half B (501-999)
 *     4. At HEAD=1000: erase Half A (sectors 0-2), HEAD=0, COUNT=500, TAIL=500
 *     5. Repeat: HEAD 0→499, then erase Half B at HEAD=500, etc.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_buffer.h"

static constexpr uint16_t MAX_COUNT        = 500;
static constexpr uint16_t HALF_SIZE        = 500;   /* slots per half */
static constexpr uint16_t TOTAL_SLOTS      = 1000;  /* 2 halves */
static constexpr uint32_t RING_FLASH_START = 0;
static constexpr uint32_t RING_FLASH_SIZE  = 6 * 4096;     /* 24 KB — 6 sectors */
static constexpr uint32_t HALF_A_BASE      = 0x0000;       /* sectors 0-2 */
static constexpr uint32_t HALF_B_BASE      = 0x3000;       /* sectors 3-5 */
static constexpr uint32_t RING_FLASH_END   = 0x6000;       /* LittleFS starts here */

/* RingEntry: RawFrame + arrival timestamp (20 bytes, stored on external flash).
   Kept separate from RawFrame (16 bytes) so the serial stream protocol
   and test framework are unaffected. */
struct __attribute__((packed)) RingEntry {
    RawFrame frame;        /* 16 bytes — sensor data */
    uint32_t arrival_ms;   /*  4 bytes — millis() at write time */
};

class FlashRing
{
public:
    FlashRing(class SPIFlash& flash);

    void reset();                  /* erase all 6 sectors, reset pointers */
    void write(const RawFrame& f); /* write one frame + millis() timestamp at HEAD */
    RawFrame read();               /* read oldest frame at TAIL (consumes) */
    uint32_t last_read_ts() const { return m_last_ts; }  /* timestamp of last-read frame */
    bool peek(RawFrame& f) const;  /* read oldest frame at TAIL (no consume) */

    bool is_full()  const { return m_count >= MAX_COUNT; }
    bool is_empty() const { return m_count == 0; }
    size_t count()  const { return m_count; }

private:
    SPIFlash& m_flash;
    uint16_t  m_head;      /* next write slot (0..999) */
    uint16_t  m_count;     /* 0..500 */
    uint32_t  m_last_ts;   /* arrival_ms of last frame returned by read() */

    uint16_t tail() const { return (m_head - m_count + TOTAL_SLOTS) % TOTAL_SLOTS; }
    uint32_t  slot_addr(uint16_t slot) const {
        if (slot < HALF_SIZE)
            return HALF_A_BASE + slot * sizeof(RingEntry);
        else
            return HALF_B_BASE + (slot - HALF_SIZE) * sizeof(RingEntry);
    }
};
