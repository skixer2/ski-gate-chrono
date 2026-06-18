/**
 * @file    start_detector.h
 * @brief   Dual-mode: speed (baro descent) or drop (total pressure increase).
 */

#pragma once

#include <stdint.h>

class StartDetector
{
public:
    enum class DetectMode : uint8_t { Speed = 0, Drop = 1 };

    StartDetector();
    bool feed(uint16_t baro_pa_div4);   /* pressure/4 value from RawFrame */
    void reset();
    void set_mode(DetectMode m) { m_mode = m; }

private:
    DetectMode m_mode;
    uint16_t m_prev_baro;
    uint16_t m_base_baro;
    uint16_t m_descent_count;
    bool     m_seeded;
    bool     m_detected;

    /* Thresholds in pressure/4 units (BMP390 reads hPa×100 → Pa, then /4)
     * 1 Pa = 4 units. 2m drop ≈ 24 Pa ≈ 96 units at sea level.
     * At 1000m: ~22 Pa ≈ 88 units. Use 60 for reliable detection. */
    static constexpr int16_t  SPEED_DELTA  = 4;    /* 1 Pa/sample */
    static constexpr uint16_t SPEED_CONFIRM = 10;
    static constexpr int16_t  DROP_TOTAL   = 5;   /* 1.25 Pa ≈ 10 cm */
};
