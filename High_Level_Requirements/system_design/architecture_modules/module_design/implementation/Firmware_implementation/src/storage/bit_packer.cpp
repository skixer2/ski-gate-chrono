/**
 * @file    bit_packer.cpp
 * @brief   RawFrame → CompressedFrame encoding.
 */

#include "bit_packer.h"
#include <string.h>

void BitPacker::reset()
{
    m_has_last = false;
    memset(&m_last, 0, sizeof(m_last));
}

CompressedFrame BitPacker::encode(const RawFrame& cur, const RawFrame& prev, uint32_t ts_ms)
{
    CompressedFrame cf;
    memset(&cf, 0, sizeof(cf));

    cf.ts_ms = ts_ms;

    /* Quaternion delta from previous frame */
    cf.q_delta[0] = cur.q_w - prev.q_w;
    cf.q_delta[1] = cur.q_x - prev.q_x;
    cf.q_delta[2] = cur.q_y - prev.q_y;
    cf.q_delta[3] = cur.q_z - prev.q_z;

    /* Linear acceleration: pass-through */
    cf.la_x = cur.la_x;
    cf.la_y = cur.la_y;
    cf.la_z = cur.la_z;

    /* Baro: pass-through */
    cf.baro_pa_div4 = cur.baro_pa_div4;

    return cf;
}
