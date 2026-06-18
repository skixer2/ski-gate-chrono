/**
 * @file    start_detector.cpp
 * @brief   Start detection from baro_pa_div4 values.
 *          Drop mode: tracks minimum pressure (peak altitude), triggers on descent.
 *          Speed mode: sustained per-sample delta.
 */

#include "start_detector.h"
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
                Serial.print("START (speed) delta="); Serial.println(delta);
                return true;
            }
        } else { m_descent_count = 0; }
    } else {
        /* Drop mode: track minimum pressure (= peak altitude).
         * When athlete raises arms, pressure drops → update min.
         * When they drop, pressure rises from min → detect descent. */
        if (!m_seeded) {
            m_base_baro = baro_pa_div4;  /* min pressure = peak altitude */
            m_seeded = true;
            return false;
        }
        if ((int16_t)(baro_pa_div4 - m_base_baro) < 0) {
            m_base_baro = baro_pa_div4;  /* going higher → update peak */
        }
        int16_t drop = (int16_t)(baro_pa_div4 - m_base_baro);
        if (drop > DROP_TOTAL) {
            m_detected = true;
            Serial.print("START (drop) ");
            float pa = drop * 0.25f;
            Serial.print(pa, 1); Serial.print(" Pa (~");
            Serial.print(pa / 12.0f, 1); Serial.println(" m)");
            return true;
        }
    }
    return false;
}
