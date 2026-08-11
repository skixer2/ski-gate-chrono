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
bool     g_manual_frame = false; /* set by B/Q/L — suppress ARM→stream */
static bool     g_stream_eof = false;     /* stop requesting after PC stops responding */
static bool     g_stream_had_data = false; /* set after first successful frame */

/* ── Init ───────────────────────────────────────────────────── */
void test_mode_init() {}

bool test_mode_active() { return g_test_mode; }

/* ── RawFrame access ────────────────────────────────────────── */
const RawFrame& test_get_frame() { return g_test_frame; }

void test_set_frame(const RawFrame& rf) {
    memcpy(&g_test_frame, &rf, sizeof(RawFrame));
}

/* ── Individual getters (derived from g_test_frame) ─────────── */
/* Pressure: baro_pa_div2 is Pa/2 → ×2 → Pa (same unit as B command / status p) */
float test_get_pressure() {
    return (float)g_test_frame.baro_pa_div2 * 2.0f;
}
float test_get_quat_w() { return (float)g_test_frame.q_w / 16384.0f; }
float test_get_quat_x() { return (float)g_test_frame.q_x / 16384.0f; }
float test_get_quat_y() { return (float)g_test_frame.q_y / 16384.0f; }
float test_get_quat_z() { return (float)g_test_frame.q_z / 16384.0f; }
float test_get_lax()    { return (float)g_test_frame.la_x; }
float test_get_lay()    { return (float)g_test_frame.la_y; }
float test_get_laz()    { return (float)g_test_frame.la_z; }

/* ── Static helpers for B/Q/L commands (Write RawFrame field) ── */
/* B always takes Pascals (json_protocol + unit tests). Clamp to uint16 Pa/2. */
static void set_pressure_pa(float pa) {
    if (pa < 0.0f) pa = 0.0f;
    if (pa > 131070.0f) pa = 131070.0f;  /* max uint16 * 2 */
    g_test_frame.baro_pa_div2 = (uint16_t)(pa * 0.5f + 0.5f);
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

/* ── Default frame (desk-still, sea-level) — init on test mode enter.
   Sea-level Pa so unit tests that arm without B still have a valid P0.
   Stream path MUST invalidate baro before first pull (see test_stream_reset /
   test_request_frame) so start_det cannot lock P0 to 101325 while S03
   streams ~800 hPa GS profiles. */
static void init_default_frame() {
    set_quat(1.0f, 0.0f, 0.0f, 0.0f);
    set_la(0.0f, 0.0f, 0.0f);
    set_pressure_pa(101325.0f);
}

/* Invalidate pressure so start_det skips (baro_pa_div2==0) until a real
   stream frame or manual B arrives. Quat/LA left alone for unit inject. */
static void invalidate_pressure_for_stream() {
    g_test_frame.baro_pa_div2 = 0;
}

void test_set_pressure_quiet(float pa)
{
    /* Ambient IDLE refresh — never marks manual (would suppress stream ARM). */
    set_pressure_pa(pa);
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

/* ── Reset stream pull state (EOF/counters).
   Does NOT clear g_manual_frame — unit tests set B/Q/L once and must
   keep manual injection across POST_RUN → next ARM. Clearing manual
   here (v4.80) made the next ARM open stream; with no PC frame server
   the ring stayed at r=0 and tests saw instant ARMED→IDLE / not_armed.
   Stream ARM path sets g_manual_frame=false explicitly before pull.

   v4.83: also zero baro so a slow first 0x3F response cannot leave the
   tm-enter sea-level default in g_test_frame (start_det P0 poison). */
void test_stream_reset() {
    g_stream_eof = false;
    g_stream_had_data = false;
    g_stream_frames = 0;
    invalidate_pressure_for_stream();
}

/* ── Pull one frame from PC (request-response) ──────────────── */
bool test_request_frame(uint32_t timeout_ms)
{
    /* Once EOF is set, stop all serial traffic — PC is done.
       feed_sensors() runs at full 100Hz to feed end detector. */
    if (g_stream_eof)
        return false;
    
    Serial.write(0x3F);  /* '?' — request one frame */
    /* No flush() — USB CDC flush on nRF52 blocks waiting for host.
       The byte goes to the TX buffer and will be sent naturally. */
    
    uint32_t deadline = millis() + timeout_ms;
    uint8_t buf[16];
    uint8_t pos = 0;
    
    while (pos < 16) {
        if (Serial.available()) {
            buf[pos++] = Serial.read();
        } else {
            if ((int32_t)(millis() - deadline) > 0) {
                /* Only set EOF if we've received data before.
                   During initial setup (enter_stream_mode pause),
                   the PC hasn't started sending yet — keep polling. */
                if (g_stream_had_data)
                    g_stream_eof = true;
                else
                    /* Pre-first-frame timeout: do NOT keep sea-level default.
                       start_det skips baro_pa_div2==0 until a real frame. */
                    invalidate_pressure_for_stream();
                return false;
            }
        }
    }
    
    memcpy(&g_test_frame, buf, sizeof(RawFrame));
    g_stream_had_data = true;  /* PC is alive */
    g_stream_frames++;
    return true;
}

/* ── Serial command handler ─────────────────────────────────── */
bool test_mode_handle_serial(char c)
{
    /* 'T' always available (toggle). Injection/stream cmds only when tm ON.
       Critical: bare 'L' was always stolen as set_la — blocked S06 drain
       LOGGING command in main.cpp (v4.73). */
    if (c == 'T') {
        g_test_mode = !g_test_mode;
        g_manual_frame = false;
        if (g_test_mode) init_default_frame();
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "T");
        Serial.print(','); json_kv_bool("tm", g_test_mode);
        json_end();
        return true;
    }

    if (!g_test_mode)
        return false;

    switch (c) {
    case 'B': {
        g_manual_frame = true;  /* manual injection → no ARM→stream */
        float pa = Serial.parseFloat();  /* Pascals */
        set_pressure_pa(pa);
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "B");
        Serial.print(','); json_kv("p", pa);
        json_end();
        return true;
    }
    case 'Q': {
        g_manual_frame = true;  /* same as B — unit tests inject without stream PC */
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
        /* set lin-acc — only in test mode (tm=1). When tm=0, main 'L' =
           natural LOGGING drain path (S06). Marks manual so ARM does not
           open stream (unit tests inject LA without a frame server). */
        g_manual_frame = true;
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
        /* V4.14: Pull model. g_stream_had_data prevents premature
           EOF during the initial setup pause (enter_stream_mode).
           EOF only activates after PC responds AND then stops. */
        g_stream_active = true;
        g_stream_frames = 0;
        g_stream_eof = false;
        g_stream_had_data = false;
        g_manual_frame = false;  /* stream mode → auto frames, not manual */
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "S");
        Serial.print(','); json_kv_bool("strm", true);
        json_end();
        return true;
    }
    return false;
}
