/**
 * @file    bit_packer.h
 * @brief   Encodes 16-byte RawFrame → 22-byte CompressedFrame.
 *          Quaternion delta encoding, acceleration pass-through.
 */

#pragma once

#include "ring_buffer.h"

class BitPacker
{
public:
    void reset();

    /* Encode raw to compressed using previous frame for delta reference */
    CompressedFrame encode(const RawFrame& cur, const RawFrame& prev, uint32_t ts_ms);

private:
    RawFrame m_last;
    bool     m_has_last;
};
