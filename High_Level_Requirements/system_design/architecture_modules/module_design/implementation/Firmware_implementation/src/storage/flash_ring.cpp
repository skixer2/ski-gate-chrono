/**
 * @file    flash_ring.cpp
 * @brief   Linear pre-roll + drain headroom (v4.77).
 */

#include "flash_ring.h"
#include "spi_flash.h"
#include <cstring>
#include <Arduino.h>

FlashRing::FlashRing(SPIFlash& flash)
    : m_flash(flash), m_head(0), m_tail(0), m_count(0),
      m_last_ts(0), m_prepared(false)
{
}

void FlashRing::prepare_preroll()
{
    for (uint32_t s = 0; s < RING_SECTORS; s++) {
        m_flash.erase_block(RING_FLASH_BASE + s * 4096u);
    }
    m_head     = 0;
    m_tail     = 0;
    m_count    = 0;
    m_last_ts  = 0;
    m_prepared = true;
}

void FlashRing::reset()
{
    prepare_preroll();
}

void FlashRing::clear()
{
    m_head    = 0;
    m_tail    = 0;
    m_count   = 0;
    m_last_ts = 0;
}

bool FlashRing::write(const RawFrame& f, bool arm_limit)
{
    if (m_head >= TOTAL_SLOTS)
        return false;
    if (arm_limit && m_count >= ARM_FILL_CAP)
        return false;

    RingEntry entry;
    memcpy(&entry.frame, &f, sizeof(RawFrame));
    entry.arrival_ms = millis();

    m_flash.write_page(slot_addr(m_head),
                       reinterpret_cast<const uint8_t*>(&entry),
                       sizeof(RingEntry));

    m_head++;
    m_count++;
    m_prepared = false;
    return true;
}

RawFrame FlashRing::read()
{
    RawFrame f;
    memset(&f, 0, sizeof(f));
    if (m_count == 0)
        return f;

    RingEntry entry;
    m_flash.read_data(slot_addr(m_tail),
                      reinterpret_cast<uint8_t*>(&entry),
                      sizeof(RingEntry));
    memcpy(&f, &entry.frame, sizeof(RawFrame));
    m_last_ts = entry.arrival_ms;
    m_tail++;
    m_count--;
    return f;
}

bool FlashRing::peek(RawFrame& f) const
{
    if (m_count == 0)
        return false;
    RingEntry entry;
    m_flash.read_data(slot_addr(m_tail),
                      reinterpret_cast<uint8_t*>(&entry),
                      sizeof(RingEntry));
    memcpy(&f, &entry.frame, sizeof(RawFrame));
    return true;
}

void FlashRing::trim_to_newest(uint16_t keep)
{
    if (keep >= m_count)
        return;
    uint16_t drop = (uint16_t)(m_count - keep);
    m_tail  = (uint16_t)(m_tail + drop);
    m_count = keep;
    /* head unchanged — free space [head, TOTAL_SLOTS) is drain headroom */
}
