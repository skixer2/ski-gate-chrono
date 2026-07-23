/**
 * @file    flash_ring.cpp
 * @brief   Flash-based ring buffer — 500-frame sliding window on 4 blocks.
 *          TAIL = (HEAD - COUNT) mod TOTAL_SLOTS — computed, not stored.
 *
 * V4.07: Each frame stores its arrival timestamp (uint16_t at 10 ms
 *        resolution) in a parallel RAM ring.  When popping frames in
 *        pairs during LOGGING, the bit-packer receives the original
 *        arrival time — not millis() at pop time — preventing the ~50%
 *        delta=0 undercount that halved run duration.
 */

#include "flash_ring.h"
#include "spi_flash.h"
#include <cstring>
#include <Arduino.h>

FlashRing::FlashRing(SPIFlash& flash)
    : m_flash(flash), m_head(0), m_count(0), m_last_ts(0), m_ts_wr(0)
{
    memset(m_ts, 0, sizeof(m_ts));
}

void FlashRing::reset()
{
    m_flash.erase_block(0);
    m_flash.erase_block(4096);
    m_flash.erase_block(8192);
    m_flash.erase_block(12288);
    m_head = m_count = 0;
    m_ts_wr = 0;
    m_last_ts = 0;
}

void FlashRing::write(const RawFrame& f)
{
    /* ── Timestamp: store now at 10 ms resolution ── */
    uint16_t ts16 = (uint16_t)(millis() / 10);
    uint16_t ts_idx = (uint16_t)(m_ts_wr % MAX_COUNT);
    m_ts[ts_idx] = ts16;
    m_ts_wr++;

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

    /* ── Retrieve stored timestamp (arrival time, not pop time) ──
       m_ts_wr is the write counter (monotonic). The oldest frame's
       timestamp was written at m_ts_wr - m_count - 1 (before this
       read decrements m_count).  Map to array via % MAX_COUNT. */
    uint16_t ts16 = m_ts[(uint16_t)((m_ts_wr - m_count - 1) % MAX_COUNT)];
    /* Reconstruct uint32_t millis() from uint16_t at 10ms resolution.
       The ring spans 5 s (500 frames × 10 ms), so no wrap correction
       needed — zero-extension is correct for this window. */
    m_last_ts = (uint32_t)ts16 * 10;
    return f;
}

bool FlashRing::peek(RawFrame& f) const
{
    if (m_count == 0) return false;

    uint16_t t = tail();
    m_flash.read_data(slot_addr(t), (uint8_t*)&f, sizeof(RawFrame));
    return true;
}
