/**
 * @file    start_detector.cpp
 * @brief   Start detection (Phase 9 — always-JSON output).
 */

#include "start_detector.h"
#include "test_json.h"
#include <Arduino.h>

StartDetector::StartDetector()
    : m_mode(DetectMode::Drop), m_prev_baro(0), m_base_baro(0),
      m_descent_count(0), m_seeded(false), m_detected(false)
{
}

void StartDetector::reset()
{
    m_prev_baro = m_base_baro = 0;
    m_descent_count = 0;
    m_seeded = false;
    m_detected = false;
}

bool StartDetector::feed(uint16_t baro_pa_div4)
{
    if (m_detected) return true;
    if (baro_pa_div4 == 0) return false;

    if (m_mode == DetectMode::Speed) {
        if (m_prev_baro == 0) { m_prev_baro = baro_pa_div4; return false; }
        int16_t delta = (int16_t)(baro_pa_div4 - m_prev_baro);
        m_prev_baro = baro_pa_div4;
        if (delta > SPEED_DELTA) {
            m_descent_count++;
            if (m_descent_count >= SPEED_CONFIRM) {
                m_detected = true;
                json_begin();
                json_kv("ev", "start");
                Serial.print(','); json_kv("mode", "speed");
                Serial.print(','); json_kv("delta", (long)delta);
                json_end();
                return true;
            }
        } else { m_descent_count = 0; }
    } else {
        if (!m_seeded) {
            m_base_baro = baro_pa_div4;
            m_seeded = true;
            return false;
        }
        if ((int16_t)(baro_pa_div4 - m_base_baro) < 0) {
            m_base_baro = baro_pa_div4;
        }
        int16_t drop = (int16_t)(baro_pa_div4 - m_base_baro);
        if (drop > DROP_TOTAL) {
            m_detected = true;
            float drop_pa = drop * 0.25f;
            json_begin();
            json_kv("ev", "start");
            Serial.print(','); json_kv("mode", "drop");
            Serial.print(','); json_kv("pa", drop_pa, 1);
            json_end();
            return true;
        }
    }
    return false;
}
