/**
 * @file    start_detector.h
 * @brief   Start detection: cumulative vertical descent from arming P₀ > 2.5 m.
 *          Fed at 10 Hz with pressure in Pa.
 */

#pragma once

#include <stdint.h>

class StartDetector
{
public:
    StartDetector();
    bool feed(float pressure_pa);
    void reset(float p0_pa);
    void set_p0(float p0_pa) { m_p0 = p0_pa; }
    bool detected() const { return m_drop_triggered; }

private:
    float m_p0;
    float m_last_reported_pa;  /* suppress duplicate sd events */
    bool  m_drop_triggered;

    static constexpr float DROP_THRESHOLD_M      = 2.0f;
    static constexpr float PA_PER_M_SEA          = 12.0f;  /* Pa/m at sea level */
    static constexpr float SEA_LEVEL_PA          = 101325.0f;
    static constexpr float SD_REPORT_DELTA_PA    = 1.0f;   /* noise gate */
};
