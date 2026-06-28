/**
 * @file    end_detector.cpp
 * @brief   End-of-run detection — 10s barometric flatline + IMU stillness.
 */

#include "end_detector.h"
#include "test_json.h"
#include <Arduino.h>
#include <math.h>

EndDetector::EndDetector()
    : m_prev_pressure(0), m_quiet_count(0),
      m_detected(false), m_seeded(false)
{}

void EndDetector::reset()
{
    m_prev_pressure = 0;
    m_quiet_count = 0;
    m_detected = false;
    m_seeded = false;
}

bool EndDetector::feed(float pressure_pa, int16_t la_x, int16_t la_y, int16_t la_z)
{
    if (m_detected) return true;

    if (!m_seeded) {
        m_prev_pressure = pressure_pa;
        m_seeded = true;
        return false;
    }

    /* ── Barometric flatline: |vertical_speed| < 0.3 m/s ── */
    float dp = pressure_pa - m_prev_pressure;
    m_prev_pressure = pressure_pa;
    /* At 100 Hz, Δt = 0.01 s. vertical_speed (m/s, positive = ascending) */
    float vert_speed = dp / (0.01f * PA_PER_M);  /* negative = descending */
    bool baro_flat = (fabsf(vert_speed) < FLATLINE_MPS);

    /* ── IMU stillness: |accel_mag - 1g| < 0.05g ── */
    float ax = (float)la_x / MM_S2_PER_G;  /* convert mm/s² → g */
    float ay = (float)la_y / MM_S2_PER_G;
    float az = (float)la_z / MM_S2_PER_G;
    float accel_mag = sqrtf(ax*ax + ay*ay + az*az);
    bool imu_still = (fabsf(accel_mag - 1.0f) < STILLNESS_G);

    if (baro_flat && imu_still) {
        m_quiet_count++;
        if (m_quiet_count >= QUIET_FRAMES) {
            m_detected = true;
            json_begin();
            json_kv("ev", "end");
            Serial.print(','); json_kv("reason", "stillness");
            json_end();
            return true;
        }
    } else {
        m_quiet_count = 0;
    }

    return false;
}
