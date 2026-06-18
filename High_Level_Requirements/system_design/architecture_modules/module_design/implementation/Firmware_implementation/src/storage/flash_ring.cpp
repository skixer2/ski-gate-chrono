/**
 * @file    flash_ring.cpp
 * @brief   Flash-based ring buffer — 500-frame sliding window on 4 blocks.
 *          TAIL = (HEAD - COUNT) mod TOTAL_SLOTS — computed, not stored.
 */

#include "flash_ring.h"
#include "spi_flash.h"
#include <cstring>

FlashRing::FlashRing(SPIFlash& flash)
    : m_flash(flash), m_head(0), m_count(0)
{
}

void FlashRing::reset()
{
    m_flash.erase_block(0);
    m_flash.erase_block(4096);
    m_flash.erase_block(8192);
    m_flash.erase_block(12288);
    m_head = m_count = 0;
}

void FlashRing::write(const RawFrame& f)
{
    if (m_count < MAX_COUNT) {
        m_flash.write_page(slot_addr(m_head), (const uint8_t*)&f, sizeof(RawFrame));
        m_head++;
        m_count++;
        return;
    }

    /* Buffer full. Erase incoming half before writing the crossing frame. */
    if (m_head == HALF_SIZE) {
        m_flash.erase_block(8192);   /* blocks 2-3 = Half B */
        m_flash.erase_block(12288);
    } else if (m_head == 0) {
        m_flash.erase_block(0);      /* blocks 0-1 = Half A */
        m_flash.erase_block(4096);
    }

    m_flash.write_page(slot_addr(m_head), (const uint8_t*)&f, sizeof(RawFrame));
    m_head = (m_head + 1) % TOTAL_SLOTS;
    /* COUNT stays 500 — oldest frame implicitly discarded as TAIL shifts with HEAD */
}

RawFrame FlashRing::read()
{
    RawFrame f;
    memset(&f, 0, sizeof(f));
    if (m_count == 0) return f;

    uint16_t t = tail();
    m_flash.read_data(slot_addr(t), (uint8_t*)&f, sizeof(RawFrame));
    m_count--;
    return f;
}
