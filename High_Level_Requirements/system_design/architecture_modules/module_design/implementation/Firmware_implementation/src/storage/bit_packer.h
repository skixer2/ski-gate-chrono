/**
 * @file    bit_packer.h
 * @brief   Adaptive bit-packing per sgc_system_design.md §3.
 *
 * Three packet types with variable-length output:
 *   Type 1 (coasting):  4-byte payload →  6 bytes total  (was  8)
 *   Type 2 (turning):   8-byte payload → 10 bytes total  (was 11)
 *   Type 3 (anchor):   16-byte payload → 18 bytes total  (unchanged)
 *
 * Phase 11 (2026-06-28): Barometric pressure moved from header
 * to delta-encoded payload.  Header shrunk 4→2 bytes.
 * Baro stored as Pa/2 (2 Pa/LSB) — double resolution, same uint16.
 * Delta encoding: int4 in T1, int8 in T2, uint16 full in T3.
 *
 * Target average: ~8.0 bytes/frame (was ~9.55).
 */

#pragma once

#include "ring_buffer.h"

class BitPacker
{
public:
    enum PktType : uint8_t { PKT_T1 = 0, PKT_T2 = 1, PKT_T3 = 2 };

    void reset();

    /**
     * @brief Encode one frame. Returns size in bytes (6/10/18).
     *        Call buffer() to get the packed data.
     */
    uint8_t encode(const RawFrame& cur, uint32_t ts_ms);

    const uint8_t* buffer() const { return m_buf; }
    uint8_t last_size() const { return m_last_size; }
    uint8_t last_type() const { return m_last_type; }

private:
    uint8_t classify(const RawFrame& cur, const int32_t* deltas, int32_t baro_delta) const;
    void    write_header(uint32_t delta_ms, PktType type);
    void    write_t1_payload(const int32_t* deltas, int32_t baro_delta);
    void    write_t2_payload(const int32_t* deltas, int32_t baro_delta);
    void    write_t3_payload(const RawFrame& cur);

    RawFrame m_last;
    uint16_t m_frame_count;
    uint32_t m_last_ms;

    uint8_t  m_buf[18];
    uint8_t  m_last_size;
    uint8_t  m_last_type;

    static constexpr uint16_t ANCHOR_EVERY = 100;
};
