/**
 * @file    bit_packer.cpp
 * @brief   Adaptive bit-packing — implements sgc_system_design.md §3.
 *
 * Phase 11 (2026-06-28): Delta-baro encoding.
 * Barometric pressure moved from uncompressed header (bytes 2-3)
 * into delta-encoded payload. Header shrinks 4→2 bytes.
 *
 * Frame header (2 bytes, was 4):
 *   [0-1] time_delta_and_flags: bits 15-14=type(2), 13-10=seq(4), 9-0=ms(10)
 *
 * Payload:
 *   T1: 8 × int4  (7 IMU deltas + 1 baro_delta) → 4 bytes — total 6
 *   T2: 8 × int8  (7 IMU deltas + 1 baro_delta) → 8 bytes — total 10
 *   T3: 7 × int16 + 1 × uint16 baro_full  → 16 bytes — total 18
 *
 * Baro stored as Pa/2 (uint16, 2 Pa/LSB).  Range 0–131070 Pa covers
 * sea level (≈101 kPa) to >4000 m altitude (≈62 kPa).
 * Delta steps are also 2 Pa/LSB.
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

    // First frame or anchor → full values (Type 3)
    if (m_frame_count == 0) {
        m_last = cur;
        m_last_ms = ts_ms;
        m_frame_count = 1;

        uint32_t delta_ms = 0;
        write_header(delta_ms, PKT_T3);
        write_t3_payload(cur);
        m_last_size = 18;
        m_last_type = PKT_T3;
        return 18;
    }

    // Compute IMU deltas
    deltas[0] = (int32_t)cur.q_w  - (int32_t)m_last.q_w;
    deltas[1] = (int32_t)cur.q_x  - (int32_t)m_last.q_x;
    deltas[2] = (int32_t)cur.q_y  - (int32_t)m_last.q_y;
    deltas[3] = (int32_t)cur.q_z  - (int32_t)m_last.q_z;
    deltas[4] = (int32_t)cur.la_x - (int32_t)m_last.la_x;
    deltas[5] = (int32_t)cur.la_y - (int32_t)m_last.la_y;
    deltas[6] = (int32_t)cur.la_z - (int32_t)m_last.la_z;

    // Baro delta (2 Pa/LSB steps)
    int32_t baro_delta = (int32_t)cur.baro_pa_div2 - (int32_t)m_last.baro_pa_div2;

    // Determine packet type
    PktType type = (PktType)classify(cur, deltas, baro_delta);

    // Forced anchor every 100 frames
    if (m_frame_count % ANCHOR_EVERY == 0)
        type = PKT_T3;

    uint32_t delta_ms = ts_ms - m_last_ms;
    if (delta_ms > 1023) {
        type = PKT_T3;
    }

    // Write header (2 bytes)
    uint32_t header_delta = (delta_ms > 1023) ? 0 : delta_ms;
    write_header(header_delta, type);

    // Payload
    switch (type) {
    case PKT_T1:
        write_t1_payload(deltas, baro_delta);
        m_last_size = 6;
        break;
    case PKT_T2:
        write_t2_payload(deltas, baro_delta);
        m_last_size = 10;
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

uint8_t BitPacker::classify(const RawFrame& cur, const int32_t* deltas, int32_t baro_delta) const
{
    (void)cur;
    bool all_fit_4bit = true;
    bool all_fit_8bit = true;

    for (int i = 0; i < 7; i++) {
        if (deltas[i] < -8 || deltas[i] > 7)   all_fit_4bit = false;
        if (deltas[i] < -128 || deltas[i] > 127) all_fit_8bit = false;
    }
    // Baro delta must also fit
    if (baro_delta < -8 || baro_delta > 7)   all_fit_4bit = false;
    if (baro_delta < -128 || baro_delta > 127) all_fit_8bit = false;

    if (all_fit_4bit) return (uint8_t)PKT_T1;
    if (all_fit_8bit) return (uint8_t)PKT_T2;
    return (uint8_t)PKT_T3;
}

/* ── Header (2 bytes) ────────────────────────────────────────── */
/*  bits 15-14: packet type, 13-10: sequence counter, 9-0: delta ms */

void BitPacker::write_header(uint32_t delta_ms, PktType type)
{
    uint8_t  seq = m_frame_count & 0x0F;
    uint16_t hdr = ((uint8_t)type << 14) | (seq << 10) | (delta_ms & 0x3FF);
    m_buf[0] = (uint8_t)(hdr & 0xFF);
    m_buf[1] = (uint8_t)(hdr >> 8);
}

/* ── Type 1 payload (4 bytes) ────────────────────────────────── */
/*  8 × signed 4-bit deltas packed into 32 bits — no padding */

void BitPacker::write_t1_payload(const int32_t* deltas, int32_t baro_delta)
{
    uint8_t nibbles[8];

    auto clamp4 = [](int32_t v) -> uint8_t {
        if (v < -8) v = -8;
        if (v > 7)  v = 7;
        return (uint8_t)(v & 0x0F);
    };

    for (int i = 0; i < 7; i++) nibbles[i] = clamp4(deltas[i]);
    nibbles[7] = clamp4(baro_delta);

    m_buf[2] = (nibbles[0] << 4) | nibbles[1];   // q_w | q_x
    m_buf[3] = (nibbles[2] << 4) | nibbles[3];   // q_y | q_z
    m_buf[4] = (nibbles[4] << 4) | nibbles[5];   // la_x | la_y
    m_buf[5] = (nibbles[6] << 4) | nibbles[7];   // la_z | baro
}

/* ── Type 2 payload (8 bytes) ────────────────────────────────── */
/*  8 × signed 8-bit deltas */

void BitPacker::write_t2_payload(const int32_t* deltas, int32_t baro_delta)
{
    auto clamp8 = [](int32_t v) -> uint8_t {
        if (v < -128) v = -128;
        if (v > 127)  v = 127;
        return (uint8_t)(v & 0xFF);
    };

    for (int i = 0; i < 7; i++)
        m_buf[2 + i] = clamp8(deltas[i]);
    m_buf[9] = clamp8(baro_delta);
}

/* ── Type 3 payload (16 bytes) ───────────────────────────────── */
/*  7 × int16 full IMU values + 1 × uint16 baro */

void BitPacker::write_t3_payload(const RawFrame& cur)
{
    auto put16 = [this](uint8_t off, int16_t val) {
        m_buf[off]     = (uint8_t)(val & 0xFF);
        m_buf[off + 1] = (uint8_t)((val >> 8) & 0xFF);
    };
    put16(2,  cur.q_w);
    put16(4,  cur.q_x);
    put16(6,  cur.q_y);
    put16(8,  cur.q_z);
    put16(10, cur.la_x);
    put16(12, cur.la_y);
    put16(14, cur.la_z);
    put16(16, cur.baro_pa_div2);
}
