/**
 * @file    bit_packer.cpp
 * @brief   Adaptive bit-packing — implements sgc_system_design.md §3.
 *
 * Frame header (4 bytes):
 *   [0-1] time_delta_and_flags: bits 15-14=type(2), 13-10=seq(4), 9-0=ms(10)
 *   [2-3] baro_pa_div4 — always uncompressed
 *
 * Payload:
 *   T1: 7 × int4  deltas (28 bits, padded to 4 bytes)
 *   T2: 7 × int8  deltas (56 bits = 7 bytes)
 *   T3: 7 × int16 values (112 bits = 14 bytes) — full values, no delta
 */

#include "bit_packer.h"
#include <Arduino.h>

void BitPacker::reset()
{
    memset(&m_last, 0, sizeof(m_last));
    memset(m_buf, 0, sizeof(m_buf));
    m_frame_count = 0;
    m_last_ms     = 0;
    m_last_size   = 0;
    m_last_type   = PKT_T3;
}

uint8_t BitPacker::encode(const RawFrame& cur, uint32_t ts_ms)
{
    int32_t deltas[7] = {0};

    // Delta vs. previous frame. First frame or anchor → full values.
    if (m_frame_count == 0) {
        // First frame: always Type 3 (anchor)
        m_last = cur;
        m_last_ms = ts_ms;
        m_frame_count = 1;

        uint32_t delta_ms = 0;  // first frame has delta=0
        write_header(delta_ms, PKT_T3);
        m_buf[2] = (uint8_t)(cur.baro_pa_div4 & 0xFF);
        m_buf[3] = (uint8_t)(cur.baro_pa_div4 >> 8);
        write_t3_payload(cur);
        m_last_size = 18;
        m_last_type = PKT_T3;
        return 18;
    }

    // Compute deltas
    deltas[0] = (int32_t)cur.q_w - (int32_t)m_last.q_w;
    deltas[1] = (int32_t)cur.q_x - (int32_t)m_last.q_x;
    deltas[2] = (int32_t)cur.q_y - (int32_t)m_last.q_y;
    deltas[3] = (int32_t)cur.q_z - (int32_t)m_last.q_z;
    deltas[4] = (int32_t)cur.la_x - (int32_t)m_last.la_x;
    deltas[5] = (int32_t)cur.la_y - (int32_t)m_last.la_y;
    deltas[6] = (int32_t)cur.la_z - (int32_t)m_last.la_z;

    // Determine packet type
    PktType type = (PktType)classify(cur, deltas);

    // Forced anchor every 100 frames
    if (m_frame_count % ANCHOR_EVERY == 0)
        type = PKT_T3;

    uint32_t delta_ms = ts_ms - m_last_ms;
    // If delta > 1023 ms, force Type 3 (delta stored in payload)
    if (delta_ms > 1023) {
        type = PKT_T3;
    }

    // Write header (4 bytes)
    uint32_t header_delta = (delta_ms > 1023) ? 0 : delta_ms;
    write_header(header_delta, type);

    // Baro — always uncompressed
    m_buf[2] = (uint8_t)(cur.baro_pa_div4 & 0xFF);
    m_buf[3] = (uint8_t)(cur.baro_pa_div4 >> 8);

    // Payload
    switch (type) {
    case PKT_T1:
        write_t1_payload(deltas);
        m_last_size = 8;
        break;
    case PKT_T2:
        write_t2_payload(deltas);
        m_last_size = 11;
        break;
    case PKT_T3:
        write_t3_payload(cur);
        m_last_size = 18;
        break;
    }

    m_last_type = type;
    m_last = cur;
    m_last_ms = ts_ms;
    m_frame_count++;

    return m_last_size;
}

/* ── Classification ──────────────────────────────────────────── */

uint8_t BitPacker::classify(const RawFrame& cur, int32_t* deltas) const
{
    (void)cur;  // may be used for future heuristics
    bool all_fit_4bit = true;
    bool all_fit_8bit = true;
    for (int i = 0; i < 7; i++) {
        if (deltas[i] < -8 || deltas[i] > 7)  all_fit_4bit = false;
        if (deltas[i] < -128 || deltas[i] > 127) all_fit_8bit = false;
    }
    if (all_fit_4bit) return (uint8_t)PKT_T1;
    if (all_fit_8bit) return (uint8_t)PKT_T2;
    return (uint8_t)PKT_T3;
}

/* ── Header (4 bytes) ────────────────────────────────────────── */
/*  bits 15-14: packet type, 13-10: sequence counter, 9-0: delta ms */

void BitPacker::write_header(uint32_t delta_ms, PktType type)
{
    uint8_t  seq = m_frame_count & 0x0F;
    uint16_t hdr = ((uint8_t)type << 14) | (seq << 10) | (delta_ms & 0x3FF);
    m_buf[0] = (uint8_t)(hdr & 0xFF);
    m_buf[1] = (uint8_t)(hdr >> 8);
}

/* ── Type 1 payload (4 bytes) ────────────────────────────────── */
/*  7 × signed 4-bit deltas packed into 28 bits */

void BitPacker::write_t1_payload(const int32_t* deltas)
{
    uint8_t nibbles[7];
    for (int i = 0; i < 7; i++) {
        // Clamp to -8..+7 and store as unsigned 4-bit (two's complement in 4 bits)
        int32_t v = deltas[i];
        if (v < -8) v = -8;
        if (v > 7)  v = 7;
        nibbles[i] = (uint8_t)(v & 0x0F);
    }
    m_buf[4] = (nibbles[0] << 4) | nibbles[1];        // q_w | q_x
    m_buf[5] = (nibbles[2] << 4) | nibbles[3];        // q_y | q_z
    m_buf[6] = (nibbles[4] << 4) | nibbles[5];        // la_x | la_y
    m_buf[7] = (nibbles[6] << 4);                      // la_z | 0000
}

/* ── Type 2 payload (7 bytes) ────────────────────────────────── */
/*  7 × signed 8-bit deltas */

void BitPacker::write_t2_payload(const int32_t* deltas)
{
    for (int i = 0; i < 7; i++) {
        int32_t v = deltas[i];
        if (v < -128) v = -128;
        if (v > 127)  v = 127;
        m_buf[4 + i] = (uint8_t)(v & 0xFF);
    }
}

/* ── Type 3 payload (14 bytes) ───────────────────────────────── */
/*  7 × int16 full absolute values (no delta) */

void BitPacker::write_t3_payload(const RawFrame& cur)
{
    auto put16 = [this](uint8_t off, int16_t val) {
        m_buf[off]     = (uint8_t)(val & 0xFF);
        m_buf[off + 1] = (uint8_t)((val >> 8) & 0xFF);
    };
    put16(4,  cur.q_w);
    put16(6,  cur.q_x);
    put16(8,  cur.q_y);
    put16(10, cur.q_z);
    put16(12, cur.la_x);
    put16(14, cur.la_y);
    put16(16, cur.la_z);
}
