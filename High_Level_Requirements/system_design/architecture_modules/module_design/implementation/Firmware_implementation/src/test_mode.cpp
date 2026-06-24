/**
 * @file    test_mode.cpp
 * @brief   Sensor injection for automated testing.
 *
 * Always compiled — ADR-001 single-binary design.
 */

#include "test_mode.h"
#include "test_json.h"

#include <Arduino.h>

static float  g_test_pressure  = 101325.0f;   // sea-level baseline
static float  g_test_qw = 0.0f, g_test_qx = 0.0f;
static float  g_test_qy = 0.0f, g_test_qz = 1.0f;
static float  g_test_lax = 0.0f, g_test_lay = 0.0f, g_test_laz = 0.0f;
static bool   g_test_mode = false;

void test_mode_init()
{
    // Boot JSON emitted by main.cpp setup() — nothing to do here.
}

bool test_mode_active() { return g_test_mode; }

float test_get_pressure() { return g_test_pressure; }
float test_get_quat_w()   { return g_test_qw; }
float test_get_quat_x()   { return g_test_qx; }
float test_get_quat_y()   { return g_test_qy; }
float test_get_quat_z()   { return g_test_qz; }
float test_get_lax()      { return g_test_lax; }
float test_get_lay()      { return g_test_lay; }
float test_get_laz()      { return g_test_laz; }

/** Emit current injected values as JSON. */
static void json_print_values()
{
    json_begin();
    json_kv("ev", "echo");
    Serial.print(',');
    json_kv("p", g_test_pressure);
    Serial.print(',');
    json_arr4("q", g_test_qw, g_test_qx, g_test_qy, g_test_qz);
    Serial.print(',');
    json_arr3("la", g_test_lax, g_test_lay, g_test_laz);
    json_end();
}

bool test_mode_handle_serial(char c)
{
    switch (c) {
    case 'T':
        g_test_mode = !g_test_mode;
        json_begin();
        json_kv("ev", "cmd");
        Serial.print(',');
        json_kv("cmd", "T");
        Serial.print(',');
        json_kv_bool("tm", g_test_mode);
        Serial.print(',');
        json_kv("p", g_test_pressure);
        Serial.print(',');
        json_arr4("q", g_test_qw, g_test_qx, g_test_qy, g_test_qz);
        Serial.print(',');
        json_arr3("la", g_test_lax, g_test_lay, g_test_laz);
        json_end();
        return true;

    case 'B': {
        float pa = Serial.parseFloat();
        g_test_pressure = pa;
        json_begin();
        json_kv("ev", "cmd");
        Serial.print(',');
        json_kv("cmd", "B");
        Serial.print(',');
        json_kv("p", pa);
        json_end();
        return true;
    }
    case 'Q': {
        g_test_qw = Serial.parseFloat();
        g_test_qx = Serial.parseFloat();
        g_test_qy = Serial.parseFloat();
        g_test_qz = Serial.parseFloat();
        json_begin();
        json_kv("ev", "cmd");
        Serial.print(',');
        json_kv("cmd", "Q");
        Serial.print(',');
        json_arr4("q", g_test_qw, g_test_qx, g_test_qy, g_test_qz);
        json_end();
        return true;
    }
    case 'L': {
        g_test_lax = Serial.parseFloat();
        g_test_lay = Serial.parseFloat();
        g_test_laz = Serial.parseFloat();
        json_begin();
        json_kv("ev", "cmd");
        Serial.print(',');
        json_kv("cmd", "L");
        Serial.print(',');
        json_arr3("la", g_test_lax, g_test_lay, g_test_laz);
        json_end();
        return true;
    }
    case 'Z':
        json_print_values();
        return true;
    }
    return false; // not a test command
}
