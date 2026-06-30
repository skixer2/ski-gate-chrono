/**
 * @file    start_detector.cpp
 * @brief   Start detection: cumulative vertical descent from arming P₀ > 2.5 m.
 *          Fed at 10 Hz with pressure in Pa.
 */

#include "start_detector.h"
#include "test_json.h"
#include <Arduino.h>

StartDetector::StartDetector()
    : m_p0(0), m_last_reported_pa(0), m_drop_triggered(false)
{}

void StartDetector::reset(float p0_pa)
{
    m_p0 = p0_pa;
    m_drop_triggered = false;
}

bool StartDetector::feed(float pressure_pa)
{
    if (m_drop_triggered) return true;
    if (pressure_pa <= 0 || m_p0 <= 0) return false;

    /* Descent = pressure INCREASES (lower altitude = higher pressure).
     * PA_PER_M scales with local air density: ~12 at sea level, ~10.5 at 1100 m. */
    float pa_per_m = PA_PER_M_SEA * (m_p0 / SEA_LEVEL_PA);
    float drop_m = (pressure_pa - m_p0) / pa_per_m;

    /* ── Diagnostic: only emit when pressure changed meaningfully ── */
    if (fabsf(pressure_pa - m_last_reported_pa) >= SD_REPORT_DELTA_PA) {
        m_last_reported_pa = pressure_pa;
        json_begin();
        json_kv("ev", "sd");
        Serial.print(','); json_kv("p0", m_p0, 1);
        Serial.print(','); json_kv("pa", pressure_pa, 1);
        Serial.print(','); json_kv("drp", drop_m, 2);
        json_end();
    }

    if (drop_m >= DROP_THRESHOLD_M) {
        m_drop_triggered = true;
        json_begin();
        json_kv("ev", "start");
        Serial.print(','); json_kv("mode", "drop");
        Serial.print(','); json_kv("m", drop_m, 1);
        json_end();
        return true;
    }

    return false;
}
