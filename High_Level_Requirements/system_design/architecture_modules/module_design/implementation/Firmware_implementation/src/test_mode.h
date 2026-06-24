/**
 * @file    test_mode.h
 * @brief   Sensor injection for automated testing.
 *
 * Always compiled — ADR-001 single-binary design.
 * Test mode is OFF by default at boot. Requires serial 'T' command
 * to activate, which is harmless in production (USB inaccessible).
 *
 * Serial protocol:
 *   B <pa>            — set barometric pressure (Pa)
 *   Q <w> <x> <y> <z> — set quaternion (floats)
 *   L <x> <y> <z>     — set linear acceleration (mm/s²)
 *   T                  — toggle test mode on/off
 *   Z                  — print current injected values
 */

#pragma once

#include <stdint.h>

void test_mode_init();
bool test_mode_active();

float test_get_pressure();
float test_get_quat_w();
float test_get_quat_x();
float test_get_quat_y();
float test_get_quat_z();
float test_get_lax();
float test_get_lay();
float test_get_laz();

bool test_mode_handle_serial(char c);
