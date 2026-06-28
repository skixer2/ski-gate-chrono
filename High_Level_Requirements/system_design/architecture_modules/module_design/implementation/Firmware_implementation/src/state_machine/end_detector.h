/**
 * @file    end_detector.h
 * @brief   End-of-run detection: 10s barometric flatline (±0.3 m/s)
 *          combined with IMU stillness (< 0.05g from 1g).
 *
 *          Per F06 and sgc_architecture_devices.md §9.
 */

#pragma once

#include <stdint.h>

class EndDetector
{
public:
    EndDetector();
    void reset();

    /**
     * Feed a sensor frame at 100 Hz.
     * pressure_pa: current barometric pressure (Pa)
     * la_x, la_y, la_z: linear acceleration (mm/s² raw)
     * Returns true when end is detected (10s of flatline + stillness).
     */
    bool feed(float pressure_pa, int16_t la_x, int16_t la_y, int16_t la_z);

    bool detected() const { return m_detected; }

private:
    float    m_prev_pressure;   /* previous pressure for vertical speed */
    uint16_t m_quiet_count;     /* consecutive quiet samples */
    bool     m_detected;
    bool     m_seeded;          /* first sample received */

    static constexpr uint16_t QUIET_FRAMES     = 1000;   /* 10 s at 100 Hz */
    static constexpr float    FLATLINE_MPS     = 0.3f;   /* m/s vertical speed threshold */
    static constexpr float    STILLNESS_G      = 0.05f;  /* g threshold from 1.0g */
    static constexpr float    PA_PER_M         = 12.0f;  /* Pa per meter at sea level */
    static constexpr float    MM_S2_PER_G      = 9806.65f; /* mm/s² per g */
};
