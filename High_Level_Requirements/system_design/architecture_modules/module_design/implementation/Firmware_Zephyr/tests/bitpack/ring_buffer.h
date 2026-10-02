#pragma once
#include <stdint.h>
struct __attribute__((packed)) RawFrame {
    int16_t  q_w, q_x, q_y, q_z;
    int16_t  la_x, la_y, la_z;
    uint16_t baro_pa_div2;
};
