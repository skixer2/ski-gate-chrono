/**
 * @file    end_detector.cpp
 */

#include "end_detector.h"
#include <Arduino.h>

EndDetector::EndDetector() : m_quiet_count(0), m_detected(false) {}
void EndDetector::reset() { m_quiet_count = 0; m_detected = false; }

bool EndDetector::feed(int16_t la_x, int16_t la_y, int16_t la_z)
{
    if (m_detected) return true;
    int32_t m2 = (int32_t)la_x*la_x + (int32_t)la_y*la_y + (int32_t)la_z*la_z;
    if (m2 < QUIET_MAG2) {
        m_quiet_count++;
        if (m_quiet_count >= QUIET_FRAMES) {
            m_detected = true;
            Serial.println("END (10s stillness)");
            return true;
        }
    } else { m_quiet_count = 0; }
    return false;
}
