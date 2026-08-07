/**
 * @file    flash_ring.cpp
 * @brief   3-region flash FIFO — live window 1000, erase region on entry (v4.73).
 */

#include "flash_ring.h"
#include "spi_flash.h"
#include <cstring>
#include <Arduino.h>

FlashRing::FlashRing(SPIFlash& flash)
    : m_flash(flash), m_head(0), m_count(0), m_last_ts(0)
{
}

void FlashRing::erase_region(uint16_t reg)
{
    if (reg >= NUM_REGIONS) return;
    uint32_t base = RING_FLASH_BASE + (uint32_t)reg * REGION_STRIDE;
    /* 3 sectors per region */
    m_flash.erase_block(base + 0x0000);
    m_flash.erase_block(base + 0x1000);
    m_flash.erase_block(base + 0x2000);
}

void FlashRing::reset()
{
    for (uint16_t r = 0; r < NUM_REGIONS; r++)
        erase_region(r);
    m_head    = 0;
    m_count   = 0;
    m_last_ts = 0;
}

void FlashRing::clear()
{
    /* Soft discard — no SPI. Next write() erases the region it enters. */
    m_count   = 0;
    m_last_ts = 0;
}

void FlashRing::write(const RawFrame& f)
{
    /*
     * Entering a region: live window ≤ MAX_COUNT (= 2 regions), so the
     * region we enter cannot hold live frames. Safe to erase, then program.
     */
    if ((m_head % REGION_SIZE) == 0)
        erase_region((uint16_t)(m_head / REGION_SIZE));

    RingEntry entry;
    memcpy(&entry.frame, &f, sizeof(RawFrame));
    entry.arrival_ms = millis();

    m_flash.write_page(slot_addr(m_head),
                       reinterpret_cast<const uint8_t*>(&entry),
                       sizeof(RingEntry));

    m_head = (uint16_t)((m_head + 1u) % TOTAL_SLOTS);

    if (m_count < MAX_COUNT)
        m_count++;
    /* else full: oldest drops out (tail advances as head moves). */
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
