/**
 * @file    test_mode.h
 * @brief   Sensor injection for automated testing.
 *
 * Always compiled — ADR-001 single-binary design.
 * Test mode is OFF by default at boot. Requires serial 'T' command
 * to activate, which is harmless in production (USB inaccessible).
 *
 * V4.13: Pull model for stream mode. Firmware sends 0x3F request byte,
 * PC responds with one 16-byte RawFrame. Eliminates push-model clobbering
 * (USB chunk delivers 3-4 frames → only last survived).
 *
 * Serial protocol:
 *   B <pa>            — set barometric pressure (hPa)
 *   Q <w> <x> <y> <z> — set quaternion (floats)
 *   L <x> <y> <z>     — set linear acceleration (mm/s²)
 *   T                  — toggle test mode on/off
 *   S                  — enter stream mode (pull frames via request-response)
 *   Z                  — print current injected values
 */

#pragma once

#include <stdint.h>
#include "storage/ring_buffer.h"

void test_mode_init();
bool test_mode_active();

/* RawFrame getter/setter — used by feed_sensors() in test mode */
const RawFrame& test_get_frame();
void  test_set_frame(const RawFrame& rf);

/* Pull one frame from PC (request-response). Returns false on timeout.
   On success, updates g_test_frame and increments g_stream_frames.
   After first timeout, sets internal EOF flag — subsequent calls return
   immediately (no 0x3F, no block) so feed_sensors() runs at full speed. */
bool test_request_frame(uint32_t timeout_ms = 100);

/* True after first request timeout — PC has no more frames. */
bool test_stream_eof();

/* Reset stream state: clear EOF, had_data, frames counter, manual flag.
   Call before ARM-triggered stream start (equivalent to 'S' command init). */
void test_stream_reset();

/* Individual getters (derived from g_test_frame) */
float test_get_pressure();   /* hPa, from baro_pa_div2 */
float test_get_quat_w();     /* from q_w / 16384.0 */
float test_get_quat_x();
float test_get_quat_y();
float test_get_quat_z();
float test_get_lax();        /* mm/s² */
float test_get_lay();
float test_get_laz();

bool test_mode_handle_serial(char c);
