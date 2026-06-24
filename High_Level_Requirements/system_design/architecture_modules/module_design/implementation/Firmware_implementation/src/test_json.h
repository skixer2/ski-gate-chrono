/**
 * @file    test_json.h
 * @brief   Compact JSON-lines output helpers for TEST_MODE.
 *
 * When compiled with -DTEST_MODE, all serial output switches to
 * JSON-lines (one JSON object per line, no pretty-printing).
 * This enables the Python test harness to parse responses
 * reliably without fragile string matching.
 *
 * Key names are deliberately short to save flash/RAM on the Nicla.
 *
 * Protocol spec: see unit_tests/json_protocol.md
 */

#pragma once

#include <Arduino.h>

/** Start a JSON object. */
inline void json_begin() { Serial.print('{'); }

/** End a JSON object with newline. */
inline void json_end() { Serial.println('}'); }

/** Write a string key:value pair. Caller handles separators. */
inline void json_kv(const char* key, const char* val) {
    Serial.print('"'); Serial.print(key); Serial.print('"');
    Serial.print(':');
    Serial.print('"'); Serial.print(val); Serial.print('"');
}

/** Write a string key:integer pair. */
inline void json_kv(const char* key, long val) {
    Serial.print('"'); Serial.print(key); Serial.print('"');
    Serial.print(':');
    Serial.print(val);
}

/** Write a string key:float pair with specified decimal places. */
inline void json_kv(const char* key, float val, int decimals = 2) {
    Serial.print('"'); Serial.print(key); Serial.print('"');
    Serial.print(':');
    Serial.print(val, decimals);
}

/** Write a string key:bool pair (0 or 1). */
inline void json_kv_bool(const char* key, bool val) {
    Serial.print('"'); Serial.print(key); Serial.print('"');
    Serial.print(':');
    Serial.print(val ? '1' : '0');
}

/** Write a float array [v0,v1,v2,v3]. */
inline void json_arr4(const char* key, float v0, float v1, float v2, float v3) {
    Serial.print('"'); Serial.print(key); Serial.print('"');
    Serial.print(":[");
    Serial.print(v0, 2); Serial.print(',');
    Serial.print(v1, 2); Serial.print(',');
    Serial.print(v2, 2); Serial.print(',');
    Serial.print(v3, 2);
    Serial.print(']');
}

/** Write a float array [v0,v1,v2] for linear acceleration. */
inline void json_arr3(const char* key, float v0, float v1, float v2) {
    Serial.print('"'); Serial.print(key); Serial.print('"');
    Serial.print(":[");
    Serial.print(v0, 2); Serial.print(',');
    Serial.print(v1, 2); Serial.print(',');
    Serial.print(v2, 2);
    Serial.print(']');
}

/** Emit a simple event object: {"ev":"<name>",...} */
inline void json_event_begin(const char* name) {
    json_begin();
    json_kv("ev", name);
}

/** Emit a command acknowledgement. */
inline void json_cmd_ack(const char* cmd) {
    json_begin();
    json_kv("ev", "cmd");
    Serial.print(',');
    json_kv("cmd", cmd);
}

/** Emit a state transition event. */
inline void json_state_evt(const char* from, const char* to) {
    json_begin();
    json_kv("ev", "st");
    Serial.print(',');
    json_kv("from", from);
    Serial.print(',');
    json_kv("to", to);
    json_end();
}
