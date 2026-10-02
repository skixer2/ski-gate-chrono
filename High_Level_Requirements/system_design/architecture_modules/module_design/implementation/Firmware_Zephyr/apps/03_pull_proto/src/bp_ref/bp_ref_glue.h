#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void bp_ref_reset(void);
uint8_t bp_ref_encode(int32_t qw, int32_t qx, int32_t qy, int32_t qz,
                      int32_t lax, int32_t lay, int32_t laz,
                      uint32_t baro_div2, uint32_t ts_ms,
                      uint8_t *buf18, uint8_t *ptype);
#ifdef __cplusplus
}
#endif
