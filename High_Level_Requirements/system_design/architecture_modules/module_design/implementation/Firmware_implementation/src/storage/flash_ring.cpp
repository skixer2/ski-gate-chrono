/**
 * @file    flash_ring.cpp
 * @brief   Two-half flash FIFO — wrap always, erase half on entry (V4.54).
 */

#include "flash_ring.h"
#include "spi_flash.h"
#include <cstring>
#include <Arduino.h>

FlashRing::FlashRing(SPIFlash& flash)
    : m_flash(flash), m_head(0), m_count(0), m_last_ts(0)
{
}

void FlashRing::erase_half_a()
{
    m_flash.erase_block(0x0000);
    m_flash.erase_block(0x1000);
    m_flash.erase_block(0x2000);
}

void FlashRing::erase_half_b()
{
    m_flash.erase_block(0x3000);
    m_flash.erase_block(0x4000);
    m_flash.erase_block(0x5000);
}

void FlashRing::reset()
{
    erase_half_a();
    erase_half_b();
    m_head    = 0;
    m_count   = 0;
    m_last_ts = 0;
}

void FlashRing::write(const RawFrame& f)
{
    /*
     * Entering a half: live window is ≤ MAX_COUNT, so the half we are
     * about to fill cannot contain live frames. Erase it, then program.
     * This covers both first fill and every wrap — no separate "full" path.
     */
    if (m_head == 0)
        erase_half_a();
    else if (m_head == HALF_SIZE)
        erase_half_b();

    RingEntry entry;
    memcpy(&entry.frame, &f, sizeof(RawFrame));
    entry.arrival_ms = millis();

    m_flash.write_page(slot_addr(m_head),
                       reinterpret_cast<const uint8_t*>(&entry),
                       sizeof(RingEntry));

    /* ALWAYS wrap. V4.53 bug: non-full path did bare m_head++. */
    m_head = (uint16_t)((m_head + 1u) % TOTAL_SLOTS);

    if (m_count < MAX_COUNT)
        m_count++;
    /* else full: oldest slot was outside the new live window (tail advanced
       implicitly because head moved and count stayed MAX_COUNT). */
}

RawFrame FlashRing::read()
{
    RawFrame f;
    memset(&f, 0, sizeof(f));
    if (m_count == 0)
        return f;

    const uint16_t t = tail();
    RingEntry entry;
    m_flash.read_data(slot_addr(t),
                      reinterpret_cast<uint8_t*>(&entry),
                      sizeof(RingEntry));
    memcpy(&f, &entry.frame, sizeof(RawFrame));
    m_last_ts = entry.arrival_ms;
    m_count--;
    return f;
}

bool FlashRing::peek(RawFrame& f) const
{
    if (m_count == 0)
        return false;

    const uint16_t t = tail();
    RingEntry entry;
    m_flash.read_data(slot_addr(t),
                      reinterpret_cast<uint8_t*>(&entry),
                      sizeof(RingEntry));
    memcpy(&f, &entry.frame, sizeof(RawFrame));
    return true;
}
