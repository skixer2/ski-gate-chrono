/**
 * @file    test_mode.cpp
 * @brief   Sensor injection for automated testing — single RawFrame.
 *
 * V4.13: Pull model for stream mode. test_request_frame() sends 0x3F
 * to request a frame from the PC, then blocks for a 16-byte response.
 * Eliminates the push-model clobbering bug (USB delivers 3-4 frames
 * per chunk, only the last survived in the single g_test_frame slot).
 */

#include "test_mode.h"
#include "test_json.h"

#include <Arduino.h>

extern bool     g_stream_active;   /* from main.cpp */
extern uint32_t g_stream_frames;   /* from main.cpp */

static RawFrame g_test_frame;       /* current test frame — real peripheral format */
static bool     g_test_mode = false;
static bool     g_stream_eof = false;  /* true after first request timeout — stop polling */

/* ── Init ───────────────────────────────────────────────────── */
void test_mode_init() {}

bool test_mode_active() { return g_test_mode; }

/* ── RawFrame access ────────────────────────────────────────── */
const RawFrame& test_get_frame() { return g_test_frame; }

void test_set_frame(const RawFrame& rf) {
    memcpy(&g_test_frame, &rf, sizeof(RawFrame));
}

/* ── Individual getters (derived from g_test_frame) ─────────── */
/* Pressure: baro_pa_div2 is Pa/2 → ×2 → Pa → /100 → hPa */
float test_get_pressure() {
    return (float)g_test_frame.baro_pa_div2 * 2.0f / 100.0f;
}
float test_get_quat_w() { return (float)g_test_frame.q_w / 16384.0f; }
float test_get_quat_x() { return (float)g_test_frame.q_x / 16384.0f; }
float test_get_quat_y() { return (float)g_test_frame.q_y / 16384.0f; }
float test_get_quat_z() { return (float)g_test_frame.q_z / 16384.0f; }
float test_get_lax()    { return (float)g_test_frame.la_x; }
float test_get_lay()    { return (float)g_test_frame.la_y; }
float test_get_laz()    { return (float)g_test_frame.la_z; }

/* ── Static helpers for B/Q/L commands (Write RawFrame field) ── */
static void set_pressure_hpa(float hpa) {
    /* hPa → Pa → Pa/2: hpa * 100 / 2 = hpa * 50 */
    g_test_frame.baro_pa_div2 = (uint16_t)(hpa * 50.0f);
}
static void set_quat(float w, float x, float y, float z) {
    g_test_frame.q_w = (int16_t)(w * 16384.0f);
    g_test_frame.q_x = (int16_t)(x * 16384.0f);
    g_test_frame.q_y = (int16_t)(y * 16384.0f);
    g_test_frame.q_z = (int16_t)(z * 16384.0f);
}
static void set_la(float x, float y, float z) {
    g_test_frame.la_x = (int16_t)x;
    g_test_frame.la_y = (int16_t)y;
    g_test_frame.la_z = (int16_t)z;
}

/* ── Default frame (desk-still, sea-level) — init on test mode enter */
static void init_default_frame() {
    set_quat(0.0f, 0.0f, 0.0f, 1.0f);
    set_la(0.0f, 0.0f, -9810.0f);
    set_pressure_hpa(1013.25f);
}

/* ── JSON echo ──────────────────────────────────────────────── */
static void json_print_values() {
    json_begin();
    json_kv("ev", "echo");
    Serial.print(','); json_kv("p", test_get_pressure());
    Serial.print(',');
    json_arr4("q", test_get_quat_w(), test_get_quat_x(),
                    test_get_quat_y(), test_get_quat_z());
    Serial.print(',');
    json_arr3("la", test_get_lax(), test_get_lay(), test_get_laz());
    json_end();
}

bool test_stream_eof() { return g_stream_eof; }

/* ── Pull one frame from PC (request-response) ──────────────── */
bool test_request_frame(uint32_t timeout_ms)
{
    /* After first timeout, stop requesting — PC has no more frames.
       Let feed_sensors() run at full 100Hz to feed end detector fast. */
    if (g_stream_eof)
        return false;
    
    Serial.write(0x3F);  /* '?' — request one frame */
    Serial.flush();
    
    uint32_t deadline = millis() + timeout_ms;
    uint8_t buf[16];
    uint8_t pos = 0;
    
    while (pos < 16) {
        if (Serial.available()) {
            buf[pos++] = Serial.read();
        } else {
            if ((int32_t)(millis() - deadline) > 0) {
                g_stream_eof = true;  /* PC stopped responding */
                return false;
            }
        }
    }
    
    memcpy(&g_test_frame, buf, sizeof(RawFrame));
    g_stream_frames++;
    return true;
}

/* ── Serial command handler ─────────────────────────────────── */
bool test_mode_handle_serial(char c)
{
    switch (c) {
    case 'T':
        g_test_mode = !g_test_mode;
        if (g_test_mode) init_default_frame();
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "T");
        Serial.print(','); json_kv_bool("tm", g_test_mode);
        json_end();
        return true;

    case 'B': {
        float pa = Serial.parseFloat();
        set_pressure_hpa(pa);
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "B");
        Serial.print(','); json_kv("p", pa);
        json_end();
        return true;
    }
    case 'Q': {
        float w = Serial.parseFloat(), x = Serial.parseFloat();
        float y = Serial.parseFloat(), z = Serial.parseFloat();
        set_quat(w, x, y, z);
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "Q");
        Serial.print(',');
        json_arr4("q", w, x, y, z);
        json_end();
        return true;
    }
    case 'L': {
        float x = Serial.parseFloat(), y = Serial.parseFloat(), z = Serial.parseFloat();
        set_la(x, y, z);
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "L");
        Serial.print(',');
        json_arr3("la", x, y, z);
        json_end();
        return true;
    }
    case 'Z':
        json_print_values();
        return true;
    case 'S':
        /* V4.13: Pull model — firmware requests frames via 0x3F,
           PC responds with one 16-byte RawFrame per request.
           g_stream_eof set false here; goes true on first timeout
           after PC stops sending → feed_sensors() runs at 100Hz. */
        g_stream_active = true;
        g_stream_frames = 0;
        g_stream_eof = false;
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "S");
        Serial.print(','); json_kv_bool("strm", true);
        json_end();
        return true;
    }
    return false;
}
