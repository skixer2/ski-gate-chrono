/**
 * @file    end_detector.h
 * @brief   10s ACC stillness end detection.
 */

#pragma once

#include <stdint.h>

class EndDetector
{
public:
    EndDetector();
    bool feed(int16_t la_x, int16_t la_y, int16_t la_z);
    void reset();

private:
    uint16_t m_quiet_count;
    bool     m_detected;
    static constexpr uint16_t QUIET_FRAMES = 1000;
    static constexpr int32_t  QUIET_MAG2   = 500000;  /* squared threshold */
};
