#include "bit_packer.h"
#include "bp_ref_glue.h"
#include <string.h>

static BitPacker s_ref;

extern "C" void bp_ref_reset(void)
{
    s_ref.reset();
}

extern "C" uint8_t bp_ref_encode(int32_t qw, int32_t qx, int32_t qy, int32_t qz,
                                 int32_t lax, int32_t lay, int32_t laz,
                                 uint32_t baro_div2, uint32_t ts_ms,
                                 uint8_t *buf18, uint8_t *ptype)
{
    RawFrame f;
    f.q_w = (int16_t)qw; f.q_x = (int16_t)qx;
    f.q_y = (int16_t)qy; f.q_z = (int16_t)qz;
    f.la_x = (int16_t)lax; f.la_y = (int16_t)lay; f.la_z = (int16_t)laz;
    f.baro_pa_div2 = (uint16_t)baro_div2;
    uint8_t sz = s_ref.encode(f, ts_ms);
    memcpy(buf18, s_ref.buffer(), 18);
    *ptype = s_ref.last_type();
    return sz;
}
