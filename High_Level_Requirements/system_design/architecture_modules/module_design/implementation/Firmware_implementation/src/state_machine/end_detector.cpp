/**
 * @file    end_detector.cpp
 * @brief   End-of-run detection — samples pressure at 0.5 Hz,
 *          checks descent < 2 m over last 10 samples (5 s).
 */

#include "end_detector.h"
#include "test_json.h"
#include <Arduino.h>

EndDetector::EndDetector()
    : m_idx(0), m_count(0),
      m_ring_full(false), m_detected(false),
      m_last_sample_ms(0)
{
    for (uint8_t i = 0; i < WINDOW_SAMPLES; i++) m_ring[i] = 0;
}

void EndDetector::reset()
{
    m_idx = 0;
    m_count = 0;
    m_ring_full = false;
    m_detected = false;
    m_last_sample_ms = 0;
}

bool EndDetector::feed(float pressure_pa)
{
    if (m_detected) return true;

    uint32_t now = millis();
    if (now - m_last_sample_ms < SAMPLE_INTERVAL_MS) return false;
    m_last_sample_ms = now;

    /* ── Store sample in small ring ── */
    m_ring[m_idx] = pressure_pa;
    m_idx++;
    m_count++;

    if (m_idx >= WINDOW_SAMPLES) {
        m_idx = 0;
        m_ring_full = true;
    }

    if (!m_ring_full) return false;

    /* oldest is at m_idx (next write position = oldest after wrap) */
    float oldest_pa = m_ring[m_idx];

    /* dp > 0  → descended (lower altitude = higher pressure)
     * dp == 0 → flat
     * dp < 0  → ascended (ignore)
     *
     * PA_PER_M scales with local air density: ~12 at sea level,
     * ~10.5 at 1100 m. Use oldest_pa as altitude reference. */
    float dp = pressure_pa - oldest_pa;
    float pa_per_m = PA_PER_M_SEA * (oldest_pa / SEA_LEVEL_PA);

    if (dp >= 0 && dp < THRESHOLD_M * pa_per_m) {
        m_detected = true;
        float descent_m = dp / pa_per_m;
        json_begin();
        json_kv("ev", "end");
        Serial.print(',');
        json_kv("reason", "descent_slow");
        Serial.print(',');
        json_kv("de_5s_m", descent_m);
        json_end();
        return true;
    }

    return false;
}
