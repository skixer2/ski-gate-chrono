/**
 * @file    flash_ring.cpp
 * @brief   Flash-based ring buffer — 500-frame sliding window on 6 sectors.
 *          TAIL = (HEAD - COUNT) mod TOTAL_SLOTS — computed, not stored.
 *
 * V4.07: Each slot stores a RingEntry (RawFrame + uint32_t arrival_ms,
 * 20 bytes) on external flash.  The timestamp is written at frame
 * arrival time (millis()) and read back at pop time, giving the
 * bit-packer correct deltaMs even when popping 2 frames per iteration.
 */

#include "flash_ring.h"
#include "spi_flash.h"
#include <cstring>
#include <Arduino.h>

FlashRing::FlashRing(SPIFlash& flash)
    : m_flash(flash), m_head(0), m_count(0), m_last_ts(0)
{
}

void FlashRing::reset()
{
    m_flash.erase_block(0x0000);   /* sector 0 — Half A */
    m_flash.erase_block(0x1000);   /* sector 1 — Half A */
    m_flash.erase_block(0x2000);   /* sector 2 — Half A */
    m_flash.erase_block(0x3000);   /* sector 3 — Half B */
    m_flash.erase_block(0x4000);   /* sector 4 — Half B */
    m_flash.erase_block(0x5000);   /* sector 5 — Half B */
    m_head = m_count = 0;
    m_last_ts = 0;
}

void FlashRing::write(const RawFrame& f)
{
    RingEntry entry;
    memcpy(&entry.frame, &f, sizeof(RawFrame));
    entry.arrival_ms = millis();   /* V4.07: stored on flash, not RAM */

    if (m_count < MAX_COUNT) {
        m_flash.write_page(slot_addr(m_head), (const uint8_t*)&entry, sizeof(RingEntry));
        m_head++;
        m_count++;
        return;
    }

    /* Buffer full. Erase incoming half before writing the crossing frame. */
    if (m_head == HALF_SIZE) {
        m_flash.erase_block(0x3000);   /* sectors 3-5 = Half B */
        m_flash.erase_block(0x4000);
        m_flash.erase_block(0x5000);
    } else if (m_head == 0) {
        m_flash.erase_block(0x0000);   /* sectors 0-2 = Half A */
        m_flash.erase_block(0x1000);
        m_flash.erase_block(0x2000);
    }

    m_flash.write_page(slot_addr(m_head), (const uint8_t*)&entry, sizeof(RingEntry));
    m_head = (m_head + 1) % TOTAL_SLOTS;
    /* COUNT stays 500 — oldest frame implicitly discarded as TAIL shifts with HEAD */
}

RawFrame FlashRing::read()
{
    RawFrame f;
    memset(&f, 0, sizeof(f));
    if (m_count == 0) return f;

    uint16_t t = tail();

    /* Read the full RingEntry to get the stored arrival_ms.
       The timestamp was written when the frame arrived — not at pop time. */
    RingEntry entry;
    m_flash.read_data(slot_addr(t), (uint8_t*)&entry, sizeof(RingEntry));
    memcpy(&f, &entry.frame, sizeof(RawFrame));
    m_last_ts = entry.arrival_ms;
    m_count--;
    return f;
}

bool FlashRing::peek(RawFrame& f) const
{
    if (m_count == 0) return false;

    uint16_t t = tail();
    RingEntry entry;
    m_flash.read_data(slot_addr(t), (uint8_t*)&entry, sizeof(RingEntry));
    memcpy(&f, &entry.frame, sizeof(RawFrame));
    return true;
}
