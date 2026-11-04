/**
 * @file    end_detector.h
 * @brief   End-of-run detection: stop logging when elevation drops
 *          less than 2 m over the last 5 s (finished descending).
 *
 *          Samples pressure at 0.5 Hz → ring of 10 values = 5 s window.
 *          Independent of FlashRing — no interference with drain logic.
 *          Per JP feedback 2026-06-30.
 */

#pragma once

#include <stdint.h>

class EndDetector
{
public:
    EndDetector();
    void reset();

    /**
     * Feed called at 100 Hz from sensor loop.
     * Samples internally at 0.5 Hz (every ~500 ms).
     * Returns true when end is detected.
     */
    bool feed(float pressure_pa);

    bool detected() const { return m_detected; }

private:
    float    m_ring[10];          /* 10 pressure samples = 5 s at 0.5 Hz */
    uint8_t  m_idx;
    uint8_t  m_count;
    bool     m_ring_full;
    bool     m_detected;
    uint32_t m_last_sample_ms;

    static constexpr float    THRESHOLD_M         = 2.0f;
    static constexpr float    PA_PER_M_SEA        = 12.0f;
    static constexpr float    SEA_LEVEL_PA        = 101325.0f;
    static constexpr uint32_t SAMPLE_INTERVAL_MS  = 500;
    static constexpr uint8_t  WINDOW_SAMPLES      = 10;    /* 5 s at 0.5 Hz */
};
