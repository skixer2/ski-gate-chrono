/**
 * @file    flash_ring.cpp
 * @brief   Linear pre-roll — prepare_preroll on IDLE, program-only ARMED (v4.75).
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
    /* Full buffer erase while athlete is not filling (IDLE / boot). */
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

void FlashRing::write(const RawFrame& f)
{
    if (m_count >= TOTAL_SLOTS)
        return; /* forward-only: stop at 30 s capacity */

    RingEntry entry;
    memcpy(&entry.frame, &f, sizeof(RawFrame));
    entry.arrival_ms = millis();

    m_flash.write_page(slot_addr(m_head),
                       reinterpret_cast<const uint8_t*>(&entry),
                       sizeof(RingEntry));

    m_head++;
    m_count++;
    m_prepared = false;
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
}
