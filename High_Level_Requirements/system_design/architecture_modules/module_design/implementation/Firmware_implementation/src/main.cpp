/**
 * Opt-A production storage — RawRunStore (v4.63+), not LittleFS payloads.
 *
 * JSON-lines is the only serial output format (ADR-001, AD-009).
 * Test commands always compiled in, test mode starts OFF.
 *
 * Flash layout (MX25R 2 MB, v4.77):
 *   0x0000–0x13FFF    Linear pre-roll 4000 slots (3000 ARM + 1000 drain)
 *   0x14000–0x1FBFFF  8 × raw run slots (RawRunStore)
 *   0x1FC000          Config
 *   0x1FD000          Index (RRS1)
 * prepare_preroll on enter IDLE; LOGGING drain = pop2 + push1 live.
 *
 * prepare_next_run(): full-slot erase at POST_RUN cooldown + boot (not ARM).
 * LOGGING: program-only into prepared slot; run_saved.store == "raw".
 */

#include <ArduinoBLE.h>
#include "nrf_power.h"
#include "Arduino_BHY2.h"
#include <Nicla_System.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "test_json.h"  /* needed for enter_system_off() json_begin/end */

/* T6/T7/T8: nRF52 GPIO SENSE and LATCH configuration for wake pins */
#include "nrf.h"
#if defined(ARDUINO_ARCH_NRF52)
#include "nrf_gpio.h"
#endif

/* ================================================================== */

/**
 * T7: Wake source detection — identifies which GPIO pin woke the device from System Off.
 * Read NRF_GPIO->LATCH at boot (P0.02 = LDC INTB, P0.14 = BHI260 INT).
 * Store in g_wake_source after verifying System Off wake via RESETREAS OFF bit.
 */
enum WakeSource { WAKE_UNKNOWN, WAKE_BUTTON, WAKE_BHI260 };
/* V5.34: LDC1612 fully removed from boot path — lazy-init only for diagnostics.
   P0.02 is now exclusively the piezo button pin. */
static bool g_ldc_inited = false;  /* true only after on-demand g_ldc.begin() */
WakeSource g_wake_source = WAKE_UNKNOWN;

/* V5.27: raw GPIO state captured at boot before any reconfig —
   for System Off wake debugging. */
uint32_t g_sys_off_latch  = 0;
uint32_t g_sys_off_pin_in = 0;

/* T6/T7/T8: Enter System Off — defined in main.cpp, called from state_machine.cpp */
void enter_system_off();

/* V5.28: g_ldc is defined later in this file, but enter_system_off()
   (defined below) needs it for clear_drdy() before System Off. */
#include "sensors/ldc1612.h"
extern LDC1612 g_ldc;

/* ================================================================== */

/**
 * T6: Enter System Off mode with GPIO SENSE configured for wake.
 * Wake sources: P0.02 (piezo button) and P0.14 (BHI260 INT)
 * Both pins are active-low, edge-triggered, configured with GPIO SENSE_LOW.
 * This is a one-way operation — device only wakes via full reset.
 */
void enter_system_off()
{
    // Stop BLE advertising (clean teardown)
    BLE.stopAdvertise();
    BLE.end();
    
    // T9: Configure BHI260AP any-motion wake-up sensor before System Off
    BHY2.configureSensor(BHY2_SENSOR_ID_ANY_MOTION_WU, 1.0f, 1000);
    delay(100);
    json_begin();
    json_kv("ev", "bhy2_wake_cfg");
    Serial.print(",");
    json_kv("sensor", (long)BHY2_SENSOR_ID_ANY_MOTION_WU);
    Serial.print(",");
    json_kv_bool("ok", true);
    json_end();
    
    // T6: Configure GPIO SENSE — LAST thing before System Off.
    // Use direct register access to avoid any mbed framework override.
    // P0.02 (piezo button): input, pull-up, sense low (active-low wake)
    NRF_GPIO->PIN_CNF[2] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                          (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                          (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
                          (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
    // P0.14 (BHI260 INT): input, pull-up, sense low
    NRF_GPIO->PIN_CNF[14] = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                           (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                           (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
                           (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
    
    // Clear LATCH before System Off
    NRF_GPIO->LATCH = 0xFFFFFFFF;
    
    // Debug: log pin states + PIN_CNF readback right before System Off
    json_begin();
    json_kv("ev", "sys_off_prep");
    Serial.print(",");
    json_kv("pin2", (long)((NRF_GPIO->IN >> 2) & 1));
    Serial.print(",");
    json_kv("pin14", (long)((NRF_GPIO->IN >> 14) & 1));
    Serial.print(",");
    json_kv("cnf2", (long)NRF_GPIO->PIN_CNF[2]);
    Serial.print(",");
    json_kv("cnf14", (long)NRF_GPIO->PIN_CNF[14]);
    Serial.print(",");
    json_kv("latch", (long)NRF_GPIO->LATCH);
    json_end();
    Serial.flush();
    
    /* V5.32: Piezo button on P0.02 — no DRDY timing race.
       Button is idle HIGH (pull-up), only goes LOW on physical press.
       No need to clear anything before System Off — just enter. */
    nrf_power_system_off();
}

/* Always emit exactly 2 lowercase hex digits (0-padded). */
static void print_hex2(uint8_t b)
{
    static const char* H = "0123456789abcdef";
    Serial.write(H[(b >> 4) & 0xF]);
    Serial.write(H[b & 0xF]);
}


extern volatile uint8_t g_bhy2_accuracy[256];
extern volatile uint8_t g_meta_event_count;
void bhy2_cal_hook_init();
#include "ble/sgc_service.h"
#include "ble/file_transfer.h"
#include "test_mode.h"
#include "led/led.h"
#include "sensors/ldc1612.h"
#include "sensors/piezo_button.h"
#include "config.h"
#include "state_machine/state_machine.h"
#include "state_machine/start_detector.h"
#include "state_machine/end_detector.h"
#include "storage/spi_flash.h"
#include "storage/flash_ring.h"
#include "storage/raw_run_store.h"
#include "storage/ring_buffer.h"
#include "storage/bit_packer.h"

/* ================================================================== */
SensorQuaternion rotation(SENSOR_ID_RV);
SensorXYZ        lin_acc(SENSOR_ID_LACC);
Sensor           pressure(SENSOR_ID_BARO);
Sensor           temperature(SENSOR_ID_TEMP);

/* ================================================================== */
/* Strip bench: count>0 enables SK6812 path; pin=0 → timing-only (no HW). */
#if LED_STRIP_COUNT > 0
LED            g_led(LED_STRIP_PIN, LED_STRIP_COUNT);
#else
LED            g_led(0, 0);
#endif
LDC1612        g_ldc;
StateMachine   g_sm;
SPIFlash       g_flash;
/* FlashRing is the ARMED pre-roll only (on MX25R). Do NOT add a large RAM
   ring — BSS growth → Cordio HCI stack alloc fails (0x80FF0144). Documented
   MEMORY.md: "RAM ring buffer abandoned". S04 force-l bypasses the ring. */
FlashRing      g_ring(g_flash);
RawRunStore    g_runs;      /* Opt-A production: pre-erased raw slots */
BitPacker      g_packer;
StartDetector  g_start_det;
EndDetector    g_end_det;

/* ================================================================== */
/* ── Run state during LOGGING ─────────────────────────────────── */
static uint32_t g_frame_count    = 0;   /* frames written this run */
static uint32_t g_logging_start_ms = 0; /* millis() at LOGGING entry */
static bool     g_run_created    = false;

/* ── Page buffer for compressed frames ────────────────────────── */
static constexpr size_t PAGE_BUF_SIZE = 256;
static uint8_t  g_page_buf[PAGE_BUF_SIZE];
static size_t   g_page_cursor = 0;

/* ================================================================== */
static uint32_t g_last_sensor_ms  = 0;
static uint32_t g_last_baro_ms   = 0;
static uint32_t g_last_battery_ms = 0;
static uint32_t g_last_qi_ms      = 0;
static uint32_t g_last_cal_ms     = 0;
static uint32_t g_last_ambient_ms = 0;  /* IDLE BHY2 ambient sample */
static float    g_ambient_pa      = 0;  /* last good ambient Pa; 0 = none */
static constexpr uint32_t AMBIENT_IDLE_MS = 5000;

/* ── Current frame (V4.41: file-scope so start detector reads same data as ring) ── */
static RawFrame g_cur_frame;

bool g_stream_active = false;  /* 'S' command — pull model frame ingestion */
uint32_t g_stream_frames = 0;   /* frames received in stream mode (total pull) */
static uint32_t g_stream_frames_pre_log = 0; /* pulls while ARMED before LOGGING */
/* Force-LOGGING via serial 'l' (bench/S04): skip end detector so a stationary
   device is not auto-closed after ~5s flat pressure. Production path enters
   LOGGING via start detector with this flag false. Cleared on POST_RUN. */
static bool g_force_logging = false;
/* Serial 'L' bench drain path: natural ring drain + live, but skip end det on
   desk (flat pressure). Production start-det entry leaves both flags false. */
static bool g_bench_drain = false;
extern bool g_manual_frame;      /* from test_mode.cpp: set by B/Q/L, suppress ARM→stream */

/* V5.04: one-shot hard BLE radio restart (BLE.end/begin) requested by serial
   'i', stream-end, or stream escape. Consumed once at the bottom of loop(). */
static bool        g_ble_radio_restart_pending = false;
static const char* g_ble_radio_restart_why     = "stream_end";

void request_ble_radio_restart(const char* why)
{
    g_ble_radio_restart_pending = true;
    g_ble_radio_restart_why     = why;
}


/* Stream mode: pull model — firmware requests frames via 0x3F,
   PC responds with one 16-byte RawFrame. No parser state needed. */

/* ================================================================== */
void flash_test();
void apply_state_visuals(DeviceState s);
void handle_serial();
void feed_sensors();
void beep_on();  void beep_off();
void flush_page_buffer();

/* ================================================================== */
void beep_on()  { analogWrite(1, 128); }
void beep_off() { analogWrite(1, 0); }

/* ================================================================== */
void flush_page_buffer()
{
    if (g_page_cursor == 0) return;
    if (!g_runs.append_data(g_page_buf, g_page_cursor)) {
        /* Nicla RGBled.h #define once — do not use that name. */
        static uint8_t raw_err_reported;
        if (!raw_err_reported) {
            raw_err_reported = 1;
            json_begin(); json_kv("ev", "raw_prog_err");
            Serial.print(','); json_kv("we", (long)g_runs.write_err());
            Serial.print(','); json_kv("sz", (long)g_runs.run_bytes());
            json_end();
        }
    }
    g_page_cursor = 0;
}

/* Encode one frame straight into the page buffer → LittleFS (no ring hop). */
static void encode_to_storage(const RawFrame& fr, uint32_t ts_ms)
{
    g_packer.encode(fr, ts_ms);
    const uint8_t cf_size = g_packer.last_size();
    const uint8_t* cf_buf = g_packer.buffer();

    /* Rare telemetry only — Serial.print mid-run costs ms and was not free. */
    if (g_frame_count == 0 || (g_frame_count % 500) == 0) {
        json_begin(); json_kv("ev", "enc_baro");
        Serial.print(','); json_kv("fn", (long)g_frame_count);
        Serial.print(','); json_kv("p", (long)fr.baro_pa_div2);
        Serial.print(','); json_kv("typ", (long)g_packer.last_type());
        json_end();
    }

    if (g_page_cursor + cf_size > PAGE_BUF_SIZE)
        flush_page_buffer();
    memcpy(g_page_buf + g_page_cursor, cf_buf, cf_size);
    g_page_cursor += cf_size;
    g_frame_count++;
}

/* ================================================================== */
void apply_state_visuals(DeviceState s)
{
    switch (s) {
    case DeviceState::SLEEP:
        g_led.set_pattern(LedPattern::BLUE_SLOW_FLOW); break;
    case DeviceState::ARMED:
        g_led.set_pattern(LedPattern::GREEN_CHASE); beep_on(); break;
    case DeviceState::LOGGING:
        /* Red blink (RED_CHASE). Real BHY2 LOGGING also calls g_led.update()
           in loop (v4.66) — same visual as stream/test path. */
        g_led.set_pattern(LedPattern::RED_CHASE);
        beep_off();
        break;
    case DeviceState::POST_RUN:
        g_led.set_pattern(LedPattern::BLUE_SLOW_FLOW_POST); break;
    }
    sgc_ble_update_state(s);
}

/* ================================================================== */
/* Read rest of current command line into buf (excluding the already-consumed
   first character). Waits up to wait_ms for the terminating newline.
   Returns length written. Returns (size_t)-1 if the newline never arrived
   within wait_ms (line timeout). Returns 0 if the line was empty (just \n). */
static size_t read_rest_of_line(char* buf, size_t cap, uint32_t wait_ms)
{
    size_t n = 0;
    uint32_t t0 = millis();
    while ((int32_t)(millis() - t0) < (int32_t)wait_ms) {
        if (!Serial.available()) { delay(1); continue; }
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') { if (n < cap) buf[n] = '\0'; return n; }
        if (n + 1 < cap) buf[n++] = c;
    }
    if (n < cap) buf[n] = '\0';
    return (size_t)-1;  /* timeout — newline never arrived */
}

void handle_serial()
{
    if (!Serial.available()) return;
    char c = Serial.read();

    /* V4.13: Pull model — no binary parser needed. Frames are pulled
       by feed_sensors() → test_request_frame() via request-response.
       In stream mode, handle_serial() skips the binary path entirely. */

    if (test_mode_handle_serial(c)) return;

    /* V4.45/v5.19: for 'h', buffer the full line first so arg parsing does
       not race USB CDC (partial packet with only 'h'). Wait for newline
       with 2 s safety timeout — this is now line-oriented like the 'L' command.
       read_rest_of_line returns (size_t)-1 on timeout (no newline in 2 s). */
    char h_args[40];
    h_args[0] = '\0';
    size_t h_len = 0;
    bool h_line_timeout = false;
    if (c == 'h') {
        h_len = read_rest_of_line(h_args, sizeof(h_args), 2000);
        h_line_timeout = (h_len == (size_t)-1);
        if (h_line_timeout) h_args[0] = '\0';  /* ensure NUL for safety */
    }

    switch (c) {
    case 'i':
        g_sm.force_state(DeviceState::SLEEP);
        /* V5.03: always recover BLE on manual wake — clears zombie link /
           sticky hold and re-ADVs even if already IDLE. */
        sgc_ble_force_recover("serial_i");
        /* V5.32: clear any pending button press from the wake */
        g_button.clear_press();
        g_button.clear_factory_trigger();
        /* V5.18: only hard-restart radio if BLE is actually wedged (phantom
           link: g_central_connected but BLE.connected() false). Prevents
           T-008c NVIC_SystemReset reboot when 'i' is used to wake from
           SLEEP (test script wakeup, normal use). The zombie check in
           main loop also gates on !BLE.connected() since 5.17. */
        if (sgc_ble_central_connected() && !BLE.connected()) {
            request_ble_radio_restart("serial_i");
        }
        break;
    case 'a':
        if (g_sm.state() == DeviceState::SLEEP) {
            /* Minimal test fork: enable serial frame source. Everything
               else (start detector, ring, LOGGING) is the real path.
               Manual B/Q/L keeps g_manual_frame → no stream (unit inject).
               S03: tm on, no manual → open pull stream. */
            if (test_mode_active() && !g_manual_frame) {
                g_manual_frame = false; /* explicit: stream owns the frame */
                /* Clears pull flags AND invalidates baro (v4.83) so start_det
                   cannot lock P0 to the tm-enter 101325 default before the
                   first S03 0x3F response (~797 hPa GS profile). */
                test_stream_reset();
                test_stream_drain_rx(); /* align first 16B pull */
                g_stream_active = true;
            } else {
                /* Manual inject or production: never leave a stale stream on. */
                g_stream_active = false;
                if (!test_mode_active()) {
                    float qx = rotation.x(), qy = rotation.y();
                    float qz = rotation.z(), qw = rotation.w();
                    float mag = sqrtf(qw*qw + qx*qx + qy*qy + qz*qz);
                    if (mag < 0.8f || mag > 1.2f) {
                        json_begin();
                        json_kv("ev", "arm_refused");
                        Serial.print(','); json_kv("reason", "quat_magnitude");
                        Serial.print(','); json_kv("mag", mag);
                        json_end();
                        break;
                    }
                }
            }
            g_sm.force_state(DeviceState::ARMED);  /* handler resets start_det + g_last_baro_ms */
        }
        break;
    case 'l':
        /* Bench force-LOGGING: skip ring drain + skip end det (S04 rate). */
        g_force_logging = true;
        g_bench_drain = false;
        g_end_det.reset();
        g_sm.force_state(DeviceState::LOGGING);
        break;
    case 'L':
        /* Bench drain LOGGING (S06): drain pre-roll then live encode.
           g_force_logging=false → ring drain path; g_bench_drain=true → no
           end det on flat desk (close with 'p'). tm must be 0. */
        g_force_logging = false;
        g_bench_drain = true;
        g_end_det.reset();
        {
            bool from_armed = (g_sm.state() == DeviceState::ARMED);
            g_sm.force_state(DeviceState::LOGGING);
            json_begin(); json_kv("ev", "cmd");
            Serial.print(','); json_kv("cmd", "L");
            Serial.print(','); json_kv("mode", "drain");
            Serial.print(','); json_kv_bool("armed", from_armed);
            Serial.print(','); json_kv("st", g_sm.state_name());
            json_end();
        }
        break;
    case 'p': g_sm.force_state(DeviceState::POST_RUN); break;
    case 's':
        /* V5.04: SLEEP entry already force_recovers (stop ADV + drop link) via
           sgc_ble_update_state; next 'i' hard-restarts the radio to come back. */
        g_sm.force_state(DeviceState::SLEEP);
        break;
    case 'O': {
        /* Toggle Nicla onboard RGB (I2C). Default OFF when strip path is built-in.
           Use during bench if you want visible ARMED/LOGGING without a strip. */
        bool en = !g_led.onboard_enabled();
        /* Optional arg: O 0 / O 1 */
        if (Serial.available()) {
            int v = Serial.parseInt();
            if (v == 0 || v == 1) en = (v != 0);
        }
        g_led.set_onboard_enabled(en);
        json_begin(); json_kv("ev", "cmd");
        Serial.print(','); json_kv("cmd", "O");
        Serial.print(','); json_kv_bool("onboard", en);
        json_end();
        return;
    }
    case 'f': if (g_sm.state() != DeviceState::ARMED && g_sm.state() != DeviceState::LOGGING) flash_test(); return;
    case 'z': {
        json_begin();
        if (!g_ldc_inited) g_ldc_inited = g_ldc.begin();
        json_kv("ev", "ldc_diag");
        Serial.print(','); json_kv_bool("ok", g_ldc.is_connected());
        Serial.print(','); json_kv("dev_id", (long)g_ldc.read_device_id());
        Serial.print(','); json_kv("manuf", (long)g_ldc.read_manufacturer_id());
        Serial.print(','); json_kv("raw", (long)g_ldc.data());
        Serial.print(','); json_kv("status", (long)(int)g_ldc.status());
        Serial.print(','); json_kv("prox_ms", (long)g_ldc.proximity_ms());
        json_end();
        return;
    }
    case 'd': g_runs.list_files(); return;
    case 'h': {
        /* Hex/baro dump for a specific run.
           Uses same read_run_data() as BLE file transfer — fork at output.
           h <run_id>              → anchors every 100th frame (T3)
           h <run_id> raw          → full file hex (same bytes BLE→phone)
           h <run_id> <from> <to>  → baro for every frame in range
           h <id> <from> <to> r    → raw hex for range

           V4.45: args come from h_args (full line buffered above),
           not live Serial.parseInt — that raced USB CDC and returned
           no chunks during the integrity check. */
        const char* p = h_args;
        while (*p == ' ' || *p == '\t') p++;
        if (h_line_timeout) {
            json_begin(); json_kv("ev","hex_err"); Serial.print(','); json_kv("reason","line_timeout"); json_end(); return;
        }
        if (*p == '\0') {
            json_begin(); json_kv("ev","hex_err"); Serial.print(','); json_kv("reason","no_args"); json_end(); return;
        }
        char* endp = nullptr;
        long rid = strtol(p, &endp, 10);
        if (endp == p || rid < 0 || rid > 65535) {
            json_begin(); json_kv("ev","hex_err"); Serial.print(','); json_kv("reason","bad_id"); json_end(); return;
        }
        p = endp;
        while (*p == ' ' || *p == '\t') p++;

        RunHeader hdr;
        if (!g_runs.read_run_header((uint16_t)rid, hdr)) {
            json_begin(); json_kv("ev","hex_err"); Serial.print(','); json_kv("reason","no_run");
            Serial.print(','); json_kv("id",rid); json_end(); return;
        }
        uint32_t data_sz = hdr.data_size;
        /* V4.46: append-only header keeps data_size=0 on disk. Prefer RAM
           index size (authoritative after close_run). */
        if (data_sz == 0) {
            const RunEntry* e = g_runs.get_entry_by_id((uint16_t)rid);
            if (e) data_sz = e->compressed_size;
        }
        if (data_sz == 0 || data_sz > 200000) {
            json_begin(); json_kv("ev","hex_err"); Serial.print(','); json_kv("reason","bad_size");
            Serial.print(','); json_kv("sz",(long)data_sz); json_end(); return;
        }

        Serial.print("{\"ev\":\"hex_dump\",\"id\":"); Serial.print(rid);
        Serial.print(",\"sz\":"); Serial.print((long)data_sz);
        long from_frame = -1, to_frame = -1;
        bool raw_mode = false, has_range = false, raw_file_mode = false;
        if (*p != '\0') {
            if (*p >= '0' && *p <= '9') {
                from_frame = strtol(p, &endp, 10); p = endp;
                while (*p == ' ' || *p == '\t') p++;
                to_frame = strtol(p, &endp, 10); p = endp;
                has_range = (from_frame >= 0 && to_frame >= from_frame && to_frame < 100000);
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 'r' || *p == 'R') raw_mode = true;
            } else {
                /* "raw" or any non-numeric token → full file hex dump */
                raw_file_mode = true;
            }
        }
        if (raw_file_mode) {
            /* Dump entire file as hex, split into short JSON lines.
               V4.47: throttle TX — blasting ~250 lines without pause
               overflows the host USB CDC RX and drops early chunks
               (seen as garbage header bytes / incomplete dump). */
            Serial.print(",\"chunks\":");
            size_t file_total = sizeof(RunHeader) + data_sz + CRC32_TRAILER_SIZE;
            Serial.print((file_total + 127) / 128);  /* chunk count */
            Serial.println("}");
            Serial.flush();

            /* V4.48: first emit a tiny header peek so we can tell flash
               corruption apart from serial/hex encoding bugs. */
            {
                uint8_t peek[16];
                if (g_runs.read_run_data((uint16_t)rid, 0, peek, 16)) {
                    json_begin(); json_kv("ev","hdr_peek");
                    Serial.print(','); json_kv("id",rid);
                    Serial.print(','); json_kv("ver",(long)peek[0]);
                    Serial.print(','); json_kv("side",(long)peek[1]);
                    Serial.print(",\"hex\":\"");
                    for (int i = 0; i < 16; i++) print_hex2(peek[i]);
                    Serial.print('"');
                    json_end();
                    Serial.flush();
                }
            }

            uint8_t rbuf[128];
            uint8_t pace = 0;
            for (uint32_t off = 0; off < file_total; off += sizeof(rbuf)) {
                size_t chunk = (file_total - off > sizeof(rbuf)) ? sizeof(rbuf) : (file_total - off);
                if (!g_runs.read_run_data((uint16_t)rid, off, rbuf, chunk)) {
                    json_begin(); json_kv("ev","hex_err"); Serial.print(',');
                    json_kv("reason","read_fail"); Serial.print(',');
                    json_kv("off",(long)off); json_end();
                    break;
                }
                /* Build hex into a stack buffer, then one Serial.write —
                   far fewer USB transactions than per-nibble prints. */
                char hexbuf[128 * 2 + 1];
                for (size_t i = 0; i < chunk; i++) {
                    static const char* H = "0123456789abcdef";
                    hexbuf[i*2]     = H[(rbuf[i] >> 4) & 0xF];
                    hexbuf[i*2 + 1] = H[rbuf[i] & 0xF];
                }
                hexbuf[chunk * 2] = '\0';

                json_begin();
                json_kv("ev","raw");
                Serial.print(','); json_kv("id",rid);
                Serial.print(','); json_kv("off",(long)off);
                Serial.print(",\"hex\":\"");
                Serial.write((const uint8_t*)hexbuf, chunk * 2);
                Serial.print('"');
                json_end();
                /* Every chunk: flush. At 115200, one ~300B JSON line is
                   fine; host drop was from back-to-back bursts. */
                Serial.flush();
                if ((++pace & 1) == 0) delay(1);
                /* V5.39: Feed WDT inside hex dump — this loop can take
                   ~8s for a 40 KB run, but WDT timeout is 5s. Without
                   this, the device reboots mid-dump (RESETREAS=DOG). */
                NRF_WDT->RR[0] = WDT_RR_RR_Reload;
            }
            json_begin(); json_kv("ev","hex_done");
            Serial.print(','); json_kv("id",rid);
            Serial.print(','); json_kv("sz",(long)file_total);
            json_end();
            return;
        }

        if (has_range) { Serial.print(",\"range\":["); Serial.print(from_frame); Serial.print(','); Serial.print(to_frame); Serial.print(']'); if (raw_mode) Serial.print(",\"raw\":1"); }
        else { Serial.print(",\"anchors\":["); }
        Serial.print(raw_mode ? ",\"hex\":[" : (has_range ? ",\"baro\":[" : ""));

        /* ── Bulk-read via read_run_data() (SAME path as BLE file xfer) ── */
        static constexpr size_t CHUNK = 128;  /* match BLE chunk size */
        uint8_t buf[CHUNK + 18];  /* +max frame size for partial frame at boundary */
        size_t  buf_avail = 0;    /* valid bytes in buf */
        size_t  buf_pos   = 0;    /* parse cursor (always 0 after chunk top-up) */
        uint32_t offset   = sizeof(RunHeader);
        uint32_t end      = offset + data_sz;
        bool     first    = true;
        int32_t  baro_recon = 0;
        uint16_t frame_idx  = 0;

        while (offset < end || buf_pos < buf_avail) {
            /* Need more data? */
            if (buf_pos + 2 > buf_avail && offset < end) {
                /* Preserve unparsed tail at buf[0..tail-1] */
                size_t tail = (buf_avail > buf_pos) ? (buf_avail - buf_pos) : 0;
                if (tail > 0) memmove(buf, buf + buf_pos, tail);
                size_t to_read = (end - offset > CHUNK) ? CHUNK : (end - offset);
                if (!g_runs.read_run_data((uint16_t)rid, offset, buf + tail, to_read)) {
                    break;
                }
                offset += to_read;
                buf_avail = tail + to_read;
                buf_pos = 0;
            }
            if (buf_pos + 2 > buf_avail) break;  /* no more data, done */

            /* Decode one frame from buf[buf_pos..] */
            uint16_t h = (uint16_t)buf[buf_pos] | ((uint16_t)buf[buf_pos+1] << 8);
            uint8_t pkt_type = (h >> 14) & 0x03;
            uint8_t pkt_size = (pkt_type == 0) ? 6 : ((pkt_type == 1) ? 10 : 18);
            if (pkt_size < 2 || buf_pos + pkt_size > buf_avail) {
                if (offset >= end) break;  /* incomplete frame at EOF */
                continue;  /* need more data from flash */
            }

            /* Decode baro */
            if (pkt_type == 2) {
                baro_recon = ((uint16_t)buf[buf_pos+16] | ((uint16_t)buf[buf_pos+17] << 8));
            } else if (pkt_type == 1) {
                baro_recon += (int32_t)(int8_t)buf[buf_pos+9];
            } else {
                baro_recon += (int32_t)(int8_t)((buf[buf_pos+5] & 0x0F) << 4) / 16;
            }

            if (has_range) {
                if (frame_idx >= (uint16_t)from_frame && frame_idx <= (uint16_t)to_frame) {
                    if (!first) Serial.print(',');
                    Serial.print('['); Serial.print(frame_idx);
                    if (raw_mode) {
                        Serial.print(",\"");
                        for (uint8_t i = 0; i < pkt_size; i++) {
                            if (buf[buf_pos+i] < 16) Serial.print('0');
                            Serial.print(buf[buf_pos+i], HEX);
                        }
                        Serial.print('"'); Serial.print(','); Serial.print(pkt_type);
                    }
                    Serial.print(','); Serial.print((long)baro_recon); Serial.print(']');
                    first = false;
                }
                if (frame_idx > (uint16_t)to_frame) break;
            } else {
                if (frame_idx % 100 == 0) {
                    if (!first) Serial.print(',');
                    Serial.print('['); Serial.print(frame_idx); Serial.print(',');
                    Serial.print((long)baro_recon); Serial.print(']');
                    first = false;
                }
            }
            frame_idx++;
            buf_pos += pkt_size;
        }

        Serial.print(']');
        Serial.print(",\"frames\":"); Serial.print(frame_idx); Serial.println("}");
        return;
    }
    case 'y': {
        Wire.begin();
        Serial.print("Wire scan: ");
        bool found = false;
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                if (found) Serial.print(", ");
                Serial.print("0x"); if (addr < 16) Serial.print("0");
                Serial.print(addr, HEX);
                found = true;
            }
        }
        if (!found) Serial.print("none");
        Serial.println();
        return;
    }
    case 'w': {
        /* V5.37: Fix pin mapping — was 22/23 (out of range).
           SDA1 = Arduino pin 4 (P0.22), SCL1 = Arduino pin 3 (P0.23). */
        pinMode(4, OUTPUT); pinMode(3, OUTPUT);
        Serial.println("Toggling (disconnect Seeed first!) — any key to stop");
        while (!Serial.available()) {
            digitalWrite(4, HIGH); digitalWrite(3, LOW);  delay(500);
            digitalWrite(4, LOW);  digitalWrite(3, HIGH); delay(500);
        }
        while (Serial.available()) Serial.read();
        pinMode(4, INPUT_PULLUP); pinMode(3, INPUT_PULLUP);
        Serial.println("Stopped.");
        return;
    }
    case 'c':
        if (!g_ldc_inited) g_ldc_inited = g_ldc.begin();
        if (g_ldc_inited) g_ldc.force_recalibrate();
        json_begin(); json_kv("ev", "ldc_recal"); json_end();
        return;
    case '!':
        json_begin(); json_kv("ev", "reboot"); json_end();
        Serial.flush();
        /* V2.93: Force LittleFS superblock commit, then DP to protect
           flash through reset. On Nicla dev board (no CS pull-up),
           NVIC_SystemReset still floats CS → possible corruption.
           On custom PCB with CS pull-up, this guarantees persistence. */
        g_runs.metadata_sync();
        g_flash.enter_deep_powerdown();
        delay(50);
        NVIC_SystemReset();
        return;
    case 'V':
        json_begin();
        json_kv("ev", "version");
        Serial.print(','); json_kv("ver", FW_VERSION);
        json_end();
        return;
    case 'R':
        json_begin(); json_kv("ev", "factory_reset"); json_end();
        Serial.flush();
        g_runs.erase_all();
        json_begin(); json_kv("ev", "reboot"); json_end();
        g_runs.metadata_sync();
        g_flash.enter_deep_powerdown();
        delay(50);
        NVIC_SystemReset();
        return;
    case '?': {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        json_begin();
        json_kv("ev", "status");
        /* Opt-A: do NOT metadata_sync()/persist_index() here.
           persist_index() erases+rewrites a full 4 KB sector — fatal on the
           LOGGING hot path. S04 v4.69 heartbeats called '?' every 5 s and
           dropped 99.4 → ~97 fps. Index is persisted on close_run/boot only.
           RAM cache is authoritative during a run (same as V4.03 scan rule). */
        Serial.print(','); json_kv("st", g_sm.state_name());
        Serial.print(','); json_kv("r", (long)g_ring.count());
        Serial.print(','); json_kv("rm", (long)g_ring.max_count());
        Serial.print(','); json_kv("rh", (long)g_ring.head());
        Serial.print(','); json_kv("p", (long)(pressure.value() * 100));   /* hPa→Pa for display */
        Serial.print(','); json_kv("bat", (long)(batt >= 0 ? batt : 0));
        Serial.print(','); json_kv("evc", (long)g_meta_event_count);
        /* V5.37: No Qi hardware on Nicla or custom PCB (dropped v4.2).
           Was digitalRead(10) = P0.02 = button pin, not Qi. */
        Serial.print(','); json_kv_bool("qi", false);
        Serial.print(','); json_kv("runs", (long)g_runs.run_count());
        Serial.print(','); json_kv("total_runs", (long)g_runs.total_run_count());
        Serial.print(','); json_kv("oldest_age", (long)g_runs.oldest_run_age());
        Serial.print(','); json_kv_bool("ldc", g_ldc_inited && g_ldc.is_connected());
        Serial.print(','); json_kv("ldc_raw", (long)(g_ldc_inited ? g_ldc.data() : 0));
        Serial.print(','); json_kv("flash_pct", (long)g_runs.flash_used_pct());
        Serial.print(','); json_kv("ver", FW_VERSION);
        /* Non-destructive test-mode read — S04 must not toggle 'T' to query. */
        Serial.print(','); json_kv_bool("tm", test_mode_active());
        Serial.print(','); json_kv("strip_n", (long)g_led.strip_count());
        Serial.print(','); json_kv_bool("strip_to", g_led.timing_only());
        Serial.print(','); json_kv_bool("onboard", g_led.onboard_enabled());
        Serial.print(','); json_kv("show_us", (long)g_led.last_show_us());
        Serial.print(','); json_kv("shows", (long)g_led.show_count());
        json_end();
        return;
    }
    }
}

/* ================================================================== */
void flash_test()
{
    /* Use reserved sector — never pre-roll (0x0000) or run slots.
       Layout: 0x1FE000–0x1FFFFF reserved (raw_run_store.h). */
    static constexpr uint32_t TEST_ADDR = 0x1FE000u;
    /* Ack immediately so harness knows 'f' was received (erase can take s).
       v4.90: commas between json_kv fields — prior lines were invalid JSON
       ({"ev":"flash""phase":"start"}) so harness dropped every flash event
       → U12 silent FAIL while SPI test still ran. */
    json_begin(); json_kv("ev", "flash");
    Serial.print(','); json_kv("phase", "start"); json_end();
    Serial.flush();
    if (!g_flash.erase_block(TEST_ADDR)) {
        json_begin(); json_kv("ev", "flash");
        Serial.print(','); json_kv_bool("ok", false);
        Serial.print(','); json_kv("err", "erase1"); json_end();
        Serial.flush(); return;
    }
    uint8_t wr[256]; for (int i = 0; i < 256; i++) wr[i] = (uint8_t)(i * 3 + 7);
    if (!g_flash.write_page(TEST_ADDR, wr, 256)) {
        json_begin(); json_kv("ev", "flash");
        Serial.print(','); json_kv_bool("ok", false);
        Serial.print(','); json_kv("err", "prog"); json_end();
        Serial.flush(); return;
    }
    uint8_t rd[256];
    if (!g_flash.read_data(TEST_ADDR, rd, 256)) {
        json_begin(); json_kv("ev", "flash");
        Serial.print(','); json_kv_bool("ok", false);
        Serial.print(','); json_kv("err", "read"); json_end();
        Serial.flush(); return;
    }
    for (int i = 0; i < 256; i++) if (rd[i] != wr[i]) {
        json_begin(); json_kv("ev", "flash");
        Serial.print(','); json_kv_bool("ok", false);
        Serial.print(','); json_kv("err_at", (long)i); json_end();
        Serial.flush(); return;
    }
    /* Leave sector erased so reserved region stays clean. */
    g_flash.erase_block(TEST_ADDR);
    json_begin(); json_kv("ev", "flash");
    Serial.print(','); json_kv_bool("ok", true); json_end();
    Serial.flush();  /* ensure harness sees result after SPI quiet period */
}

/* ================================================================== */
/* ── Synchronous state transition handler (V4.41).
   Called immediately by StateMachine::enter_state() — no lazy check,
   no one-iteration race between force_state and feed_sensors. ── */
static void on_state_transition(DeviceState from, DeviceState to)
{
    uint32_t now = millis();

    json_state_evt(g_sm.state_name_for(from), g_sm.state_name());

    if (to == DeviceState::SLEEP) {
        /* Sample ambient ASAP on enter IDLE (then every AMBIENT_IDLE_MS). */
        g_last_ambient_ms = 0;
        /* Drop stream on any path into IDLE (cancel/timeout/cooldown).
           POST_RUN already clears stream; this covers ARMED→IDLE without
           LOGGING so unit 'i' / ARM_TIMEOUT cannot leave g_stream_active. */
        if (g_stream_active) {
            g_stream_active = false;
            json_begin(); json_kv("ev", "stream_end");
            Serial.print(','); json_kv("frames", (long)g_stream_frames);
            Serial.print(','); json_kv("reason", "idle");
            json_end();
            g_stream_frames = 0;
            test_stream_reset(); /* pull flags only — keeps g_manual_frame */
            request_ble_radio_restart("stream_end");
        }
        /* JP: prepare_preroll on enter IDLE from ARMED (timeout/cancel) or
           POST_RUN (after run). Erase 3000-slot buffer off the fill path so
           next ARMED is program-only for up to 30 s. */
        if (from == DeviceState::ARMED || from == DeviceState::POST_RUN) {
            /* Skip if already prepared (e.g. soft clear left prepared=true). */
            if (!g_ring.prepared()) {
                /* Flush st/stream_end BEFORE sector erases. SPI erase can
                   mask IRQs long enough that an unflushed USB CDC st line
                   is dropped — harness then only sees preroll_prep. */
                Serial.flush();
                g_ring.prepare_preroll();
                json_begin(); json_kv("ev", "preroll_prep");
                Serial.print(','); json_kv("arm_cap", (long)ARM_FILL_CAP);
                Serial.print(','); json_kv("total", (long)TOTAL_SLOTS);
                Serial.print(','); json_kv("keep", (long)PREROLL_KEEP);
                json_end();
            }
        }
    }
    if (to == DeviceState::ARMED) {
        g_packer.reset();
        g_page_cursor = 0;
        g_run_created = false;
        g_last_baro_ms = now;
        /* Buffer already erased on enter IDLE — do not erase here.
           Soft clear only if somehow dirty (should be prepared). */
        if (!g_ring.prepared())
            g_ring.clear();
        /* P₀ auto-inits from first valid frame written to the ring.
           Stream ARM already called test_stream_reset() (baro=0). Soft-clear
           ring count so no leftover slots from a prior aborted fill. */
        g_ring.clear();
        g_start_det.reset(0.0f);
    }
    /* V5.34: no LDC recal on SLEEP — LDC not in boot path anymore. */
    if (to == DeviceState::LOGGING) {
        BLE.stopAdvertise();
        g_end_det.reset();
        g_packer.reset();
        g_page_cursor = 0;
        g_frame_count = 0;
        int16_t baro_temp = (int16_t)(temperature.value() * 10.0f);
        uint8_t cal = 0;

        if (g_force_logging) {
            /* S04: no pre-roll needed */
            g_ring.clear();
        } else {
            /* Keep only newest ~10 s for encode/phone (may have up to 30 s). */
            g_ring.trim_to_newest(PREROLL_KEEP);
        }

        /* Both production and force-l use raw store (Opt A).
           create_run() uses slot prepared in POST_RUN; if none (first boot
           / S04 -R), it prepares now (full or partial erase). */
        g_run_created = g_runs.create_run(0, baro_temp, cal, sgc_ble_epoch_now());
        /* Rate clock after create — prefer POST_RUN-paid erase so this is
           header program only (~ms). */
        g_logging_start_ms = millis();
        g_stream_frames_pre_log = g_stream_frames;
        json_begin();
        json_kv("ev", "run_created");
        Serial.print(','); json_kv_bool("ok", g_run_created);
        Serial.print(','); json_kv("id", (long)g_runs.total_run_count());
        Serial.print(','); json_kv("store", "raw");
        json_end();
    }
    if (to == DeviceState::POST_RUN) {
        /* V5.03: ADV handled by sgc_ble_update_state (force_recover path) —
           no bare advertise() here. */
        if (g_stream_active) {
            g_stream_active = false;
            json_begin(); json_kv("ev", "stream_end");
            Serial.print(','); json_kv("frames", (long)g_stream_frames);
            Serial.print(','); json_kv("armed", (long)g_stream_frames_pre_log);
            {
                long logf = (long)g_stream_frames - (long)g_stream_frames_pre_log;
                if (logf < 0) logf = 0;
                Serial.print(','); json_kv("logging", logf);
            }
            json_end();
            request_ble_radio_restart("stream_end");
        }
        flush_page_buffer();

        uint32_t compressed_sz = g_runs.run_bytes();
        uint16_t run_id = g_runs.close_run(g_frame_count);
        bool ok = (run_id != 0xFFFF) && (g_runs.write_err() == 0);
        sgc_ble_set_run_count(g_runs.run_count());
        sgc_ble_set_flash_used(g_runs.flash_used_pct());

        g_force_logging = false;
        g_bench_drain = false;

        {
            uint32_t dur_ms = 0;
            if (g_logging_start_ms != 0) {
                dur_ms = millis() - g_logging_start_ms;
            }
            json_begin();
            json_kv("ev", "run_saved");
            Serial.print(','); json_kv("id", (long)run_id);
            Serial.print(','); json_kv("fr", (long)g_frame_count);
            Serial.print(','); json_kv("sz", (long)compressed_sz);
            Serial.print(','); json_kv("dur_ms", (long)dur_ms);
            if (dur_ms > 0) {
                long fps10 = ((long)g_frame_count * 10000L) / (long)dur_ms;
                Serial.print(','); json_kv("fps10", fps10);
            }
            Serial.print(','); json_kv_bool("ok", ok);
            Serial.print(','); json_kv("store", "raw");
            Serial.print(','); json_kv("we", (long)g_runs.write_err());
            Serial.print(','); json_kv("runs", (long)g_runs.run_count());
            Serial.print(','); json_kv("total", (long)g_runs.total_run_count());
            json_end();
            g_logging_start_ms = 0;
        }
        g_ring.clear(); g_packer.reset();
        g_start_det.reset(0.0f);
        g_frame_count = 0;
        g_run_created = false;
        g_page_cursor = 0;
        if (g_stream_frames) { g_stream_frames = 0; }
        /* Clear pull-stream flags only. Keep g_manual_frame so the next
           unit-test ARM still uses injected B/Q/L (not empty stream). */
        test_stream_reset();

        /* Opt A: pre-erase NEXT run slot during POST_RUN cooldown.
           Pre-roll erase happens on enter IDLE (after cooldown → IDLE). */
        g_runs.ensure_space_for_new_run();
        g_runs.prepare_next_run();
    }
    apply_state_visuals(to);
}

/* ================================================================== */
void feed_sensors()
{
    RawFrame f;
    DeviceState st = g_sm.state();  /* captured once for state checks below */

    /* ── Test mode: pull request-response only in ARMED/LOGGING.
       In IDLE, test_request_frame() would consume serial bytes
       intended for command parsing (e.g. 'a' ARM command). ── */
    if (test_mode_active()) {
        if (g_stream_active && (st == DeviceState::ARMED || st == DeviceState::LOGGING)) {
            test_request_frame();  /* updates g_test_frame on success;
                                      leaves it unchanged on timeout */
        }
        f = test_get_frame();
    } else {
        f.q_w = (int16_t)(rotation.w() * 16384.0f);
        f.q_x = (int16_t)(rotation.x() * 16384.0f);
        f.q_y = (int16_t)(rotation.y() * 16384.0f);
        f.q_z = (int16_t)(rotation.z() * 16384.0f);
        f.la_x = (int16_t)lin_acc.x();
        f.la_y = (int16_t)lin_acc.y();
        f.la_z = (int16_t)lin_acc.z();
        f.baro_pa_div2 = (uint16_t)(pressure.value() * 50.0f);  /* hPa→Pa/2 */
    }

    /* Store for start detector (same data as ring) */
    g_cur_frame = f;

    if (st == DeviceState::SLEEP) return;

    if (st == DeviceState::ARMED) {
        /* V4.98: stream ARM must NOT ring/start_det on ambient leftovers.
           IDLE ambient (test_set_pressure_quiet) can leave room baro (~941 hPa)
           in g_test_frame. After 'a', g_stream_active is true but the first
           successful 0x3F pull may land on a later tick — feeding ambient as
           P0 poisons S03 (GS profile ~797 hPa) and integrity fails.
           Skip until stream has real data (or non-stream manual/prod path). */
        if (g_stream_active && test_mode_active() && !test_stream_has_data()) {
            /* Do not touch g_test_frame here — invalidate already done on ARM.
               Just skip ring + start_det until first good pull. */
            return;
        }

        /* Linear pre-roll: program only up to ARM_FILL_CAP (3000). */
        bool was_full = g_ring.is_full();
        g_ring.write(f, true);
        if (!was_full && g_ring.is_full()) {
            json_begin();
            json_kv("ev", "ring_full");
            Serial.print(','); json_kv("r", (long)g_ring.max_count());
            Serial.print(','); json_kv("cap_s", 30L);
            json_end();
        }
        /* v4.86: feed start_det from THIS frame (same data as ring).
           Do not rely on a separate 100 ms poll of g_cur_frame — that raced
           stream pull timeouts which zeroed baro between samples (S03 flake). */
        if (f.baro_pa_div2 > 0) {
            float pa = (float)f.baro_pa_div2 * 2.0f;  /* Pa/2 → Pa */
            if (g_start_det.feed(pa))
                g_sm.force_state(DeviceState::LOGGING);
        }
        return;
    }

    if (st == DeviceState::LOGGING) {
        /* force-'l' (S04): live encode only, no ring.
           Natural / bench L: pop-2 + push-1 while backlog remains after pop.
           CRITICAL: if count was 1, pop-1 then push-1 would deadlock at r=1
           forever (S06 v4.77 saw ~16 s stuck). So only push live when the
           ring is STILL non-empty after pops; otherwise encode live direct. */
        if (g_force_logging) {
            encode_to_storage(f, millis());
        } else if (!g_ring.is_empty()) {
            uint8_t pop_n = g_ring.count() >= 2 ? 2 : (uint8_t)g_ring.count();
            for (uint8_t i = 0; i < pop_n; i++) {
                RawFrame oldest = g_ring.read();
                encode_to_storage(oldest, g_ring.last_read_ts());
            }
            if (!g_ring.is_empty()) {
                /* Still draining — park live in headroom (net −1 if pop2). */
                g_ring.write(f, false);
            } else {
                /* Just emptied — take this tick's live sample now. */
                encode_to_storage(f, millis());
            }
        } else {
            encode_to_storage(f, millis());
        }

        /* End detector on live sample — skipped for force-'l' (S04) and
           bench drain 'L' (S06). Production start-det entry uses end det. */
        if (!g_force_logging && !g_bench_drain) {
            float pa_raw = (float)f.baro_pa_div2 * 2.0f;  /* Pa/2 → Pa */
            if (g_end_det.feed(pa_raw))
                g_sm.force_state(DeviceState::POST_RUN);
        }
        return;
    }
}

/* ================================================================== */
void setup()
{
    nicla::begin();
    Serial.begin(115200);
    delay(500);   /* let pio device monitor open the port before we print */
    /* V5.27: USB CDC re-enumeration after hard reset takes 1-2 s.
       delay(500) was too short — boot JSON (including version) was lost.
       V5.26 tried `while (!Serial)` but on mbed, Serial is always truthy
       (CDC object exists regardless of host connection). Use unconditional delay. */
    delay(1500);

    /* ── T7: Wake source detection (LATCH) + RESETREAS FIRST ──
       Read LATCH immediately (before any GPIO reconfig), then clear it.
       Only trust g_wake_source if we woke from System Off (RESETREAS_OFF).
       V5.24: Also verify the pin is actually LOW (active interrupt) —
       LATCH can retain stale bits from previous wake cycles. */
    {
        uint32_t latched = NRF_GPIO->LATCH;
        uint32_t pin_in = NRF_GPIO->IN;
        if ((latched & (1u << 2)) && !(pin_in & (1u << 2))) {
            g_wake_source = WAKE_BUTTON;       // V5.32: P0.02 = piezo button now (was LDC INTB)
        } else if ((latched & (1u << 14)) && !(pin_in & (1u << 14))) {
            g_wake_source = WAKE_BHI260;    // P0.14 = BHI260 INT
        }
        // V5.27: log raw LATCH + pin states for System Off wake debugging
        // (before any GPIO reconfig can clobber them)
        g_sys_off_latch = latched;
        g_sys_off_pin_in = pin_in;
        // Clear LATCH for next wake
        NRF_GPIO->LATCH = 0xFFFFFFFF;
    }

    /* ── Version FIRST — no delay, no preamble ── */
    uint32_t rr = NRF_POWER->RESETREAS;
    // T7: If we woke from System Off, keep g_wake_source; otherwise UNKNOWN
    if ((rr & POWER_RESETREAS_OFF_Msk) == 0) {
        g_wake_source = WAKE_UNKNOWN;
    }
    NRF_POWER->RESETREAS = 0xFFFFFFFF;  /* clear for next boot */
    Serial.print("{\"ev\":\"boot\",\"ver\":\"");
    Serial.print(FW_VERSION);
    Serial.print("\",\"rr\":");
    Serial.print(rr);
    // T7: wake source report in boot JSON
    Serial.print(",\"wrs\":");
    if (g_wake_source == WAKE_BUTTON) {
        Serial.print("\"LDC\"");
    } else if (g_wake_source == WAKE_BHI260) {
        Serial.print("\"BHI260\"");
    } else {
        Serial.print("\"UNKNOWN\"");
    }
    Serial.println("}");
    // V5.27: if woke from System Off, log raw GPIO state for wake debugging
    if (rr & POWER_RESETREAS_OFF_Msk) {
        json_begin();
        json_kv("ev", "sys_off_wake_dbg");
        Serial.print(','); json_kv("latch", (long)g_sys_off_latch);
        Serial.print(','); json_kv("pin2", (long)((g_sys_off_pin_in >> 2) & 1));
        Serial.print(','); json_kv("pin14", (long)((g_sys_off_pin_in >> 14) & 1));
        Serial.print(','); json_kv("lat2", (long)((g_sys_off_latch >> 2) & 1));
        Serial.print(','); json_kv("lat14", (long)((g_sys_off_latch >> 14) & 1));
        json_end();
    }
    Serial.flush();
    delay(50);

    /* V5.08: Hardware watchdog timer — independent of CPU. If loop() blocks
       >5s (e.g. writeValue stuck inside Cordio HCI), WDT reboots the device.
       This is the ONLY recovery path when the main loop is stuck inside a
       blocking call. Software watchdogs (FT stall, zombie) can't fire because
       they run inside loop(). WDT runs in hardware, independent of CPU.
       
       WDT notes:
       - 5s timeout: safe for flash erase (~2-3s/sector) and BLE radio_restart (~50ms)
       - PAUSE on HALT (debugger) — doesn't interfere with platformio debugging
       - RUN in SLEEP — WDT stays active during WFE/WFI (important: device sleeps in IDLE)
       - Fed every loop() iteration (~100 Hz normally, ~10 Hz during FT)
       - If writeValue() blocks: WDT fires → device reboots → BLE re-inits → scannable again
       - Reboot reason will show in boot JSON: "rr" field (RESETREAS bit 8 = WDT)
    */
    /* V5.09: WDT start moved to AFTER all init completes (was before init
       in 5.08, causing a boot loop — BHY2.begin + preroll erase take >5s). */

    /* ── LED (onboard and/or SK6812 bench/production strip) ── */
    g_led.begin();
    json_begin();
    json_kv("ev", "led");
    Serial.print(','); json_kv("strip", (long)g_led.strip_count());
    Serial.print(','); json_kv_bool("timing_only", g_led.timing_only());
    Serial.print(','); json_kv("pin", (long)LED_STRIP_PIN);
    json_end();

    /* ── SPI Flash (MX25R1635F on SPI0, CS_FLASH=p26) ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "flash");
    bool flash_ok = g_flash.begin();
    Serial.print(','); json_kv_bool("ok", flash_ok);
    json_end();
    if (!flash_ok) { while(1) { g_led.set_pattern(LedPattern::OFF); delay(1000); } }

    /* ── Linear pre-roll: full erase at boot (same as enter IDLE) ── */
    g_ring.prepare_preroll();
    json_begin(); json_kv("ev", "preroll_prep");
    Serial.print(','); json_kv("arm_cap", (long)ARM_FILL_CAP);
    Serial.print(','); json_kv("total", (long)TOTAL_SLOTS);
    Serial.print(','); json_kv("keep", (long)PREROLL_KEEP);
    Serial.print(','); json_kv("why", "boot");
    json_end();

    /* V5.34: LDC1612 NOT initialized at boot — but chip powers up in
       active mode by default (DRDY fires, INTB pulls P0.02 LOW).
       Quiesce it: CONFIG=0 (sleep) + clear DRDY → INTB releases HIGH.
       Must happen BEFORE piezo button init to free P0.02. */
    g_ldc.quiesce();
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "ldc_quiesce");
    Serial.print(','); json_kv_bool("ok", g_ldc.is_connected());
    json_end();

    /* V5.32: Piezo button init — replaces LDC for arming/wake. */
    g_button.begin();
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "piezo_btn");
    Serial.print(','); json_kv("pin", (long)PiezoButton::ARDUINO_PIN);
    json_end();

    /* V5.15 T-008a: warm reset preserves Cordio statics — tear down before begin.
       RESETREAS bit 0 = power-on. Soft/WDT/flash resets leave bit 0 clear. */
    bool is_power_on = (rr & 0x01u) != 0;
    if (!is_power_on) {
        json_begin();
        json_kv("ev", "ble_warm_deinit");
        Serial.print(','); json_kv("rr", (long)rr);
        json_end();
        BLE.end();
        delay(100);
    }

    /* ── BLE first — needs heap for thread before BHY2 exhausts it ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "ble");
    bool ble_ok = BLE.begin();
    Serial.print(','); json_kv_bool("ok", ble_ok);
    json_end();
    if (!ble_ok) { while(1) delay(1000); }
    sgc_ble_init();
    sgc_ble_transfer_init();

    /* V5.32: LDC recalibration after BLE init removed — LDC no longer used for arming.
       Button uses GPIO edge interrupt, no calibration needed. */

    /* ── Raw run store (Opt A) — after BD init, before BHY2 ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "raw_store");
    json_end();
    Serial.flush();
    bool fs_ok = g_runs.begin(g_flash);
    Serial.print("{\"ev\":\"init\",\"sub\":\"raw_store_res\",\"ok\":");
    Serial.print(fs_ok ? "1" : "0");
    Serial.println("}");
    if (!fs_ok) {
        while (1) { g_led.set_pattern(LedPattern::RED_FLASH_3); delay(1000); }
    }
    /* First-run slot ready before any ARM (S04 -R / cold boot). */
    g_runs.ensure_space_for_new_run();
    g_runs.prepare_next_run();

    /* ── BHY2 init (standalone — only sensor hub, no BLE/I2C/DFU handlers) ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "bhy2");
    bool bhy2_ok = BHY2.begin(NICLA_STANDALONE);
    Serial.print(','); json_kv_bool("ok", bhy2_ok);
    json_end();
    if (bhy2_ok) {
        /* CRITICAL (v4.59): SensorClass::begin() defaults to rate=1000 Hz,
           latency=1 ms. That floods the BHI260 FIFO; BHY2.update() then
           dominates the main loop and feed_sensors collapses to ~40 Hz
           (S04 measured 41.9 fps @ v4.58). Request the rates we actually
           consume at 10 ms feed ticks. */
        /* Rates only — no setRange(). LACC virtual sensor → int16 mm/s²
           (Arduino_BHY2 scale 1.0). Wire ceiling ±32767 mm/s² ≈ ±3.34 g.
           Physical BHI260 accel ~±16 g / gyro ~±2000 dps stay on hub defaults;
           gyro is fusion-internal only (we log RV + LACC, not raw gyro). */
        rotation.begin(100.0f, 0);     /* RV @ 100 Hz → store as Q14 */
        lin_acc.begin(100.0f, 0);     /* LACC @ 100 Hz → mm/s² int16 */
        pressure.begin(100.0f, 0);    /* baro virtual @ 100 Hz */
        temperature.begin(1.0f, 0);   /* header only — not per-frame */
    }

    test_mode_init();
    bhy2_cal_hook_init();

    g_sm.on_transition(on_state_transition);  /* V4.41: synchronous handlers */
    g_sm.force_state(DeviceState::SLEEP);

    // T8: If woke from System Off via LDC tap, auto-arm after init
    if (g_wake_source == WAKE_BUTTON) {
        json_begin();
        json_kv("ev", "auto_arm");
        Serial.print(','); json_kv("reason", "button_wake");
        json_end();
        g_sm.force_state(DeviceState::ARMED);
    }

    // T8: If BHI260 wake, stay in SLEEP (wait for LDC tap to ARM)

    /* V4.03: Reset timestamps to current millis(). On nRF52 warm resets
       (NVIC_SystemReset), static variables retain their values. If
       g_last_baro_ms was e.g. 45000 from before reset and millis()
       restarts at ~0, "now - g_last_baro_ms" underflows unsigned,
       firing start-detection immediately with stale sensor data. */
    g_last_baro_ms   = millis();
    g_last_sensor_ms = millis();
    g_last_battery_ms = millis();
    apply_state_visuals(g_sm.state());

    int8_t batt = nicla::getBatteryVoltagePercentage();
    sgc_ble_set_battery(batt >= 0 ? (uint8_t)batt : 0);
    sgc_ble_set_run_count(g_runs.run_count());
    sgc_ble_set_flash_used(g_runs.flash_used_pct());

    json_begin();
    json_kv("ev", "ready");
    Serial.print(','); json_kv("st", g_sm.state_name());
    Serial.print(','); json_kv("runs", (long)g_runs.run_count());
    Serial.print(','); json_kv("ver", FW_VERSION);
    Serial.print(','); json_kv("used_pct", (long)g_runs.flash_used_pct());
    json_end();

    /* V5.09: Start hardware watchdog AFTER all init completes.
       5.08 started it before init → BHY2.begin + preroll erase took >5s
       → WDT fired during boot → infinite boot loop (rr:2). */
    NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) |
                      (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
    NRF_WDT->CRV = (WDT_TIMEOUT_MS * 32768) / 1000;  // ms → WDT clock ticks (32.768 kHz)
    NRF_WDT->RREN |= WDT_RREN_RR0_Msk;  // enable reload register 0
    NRF_WDT->TASKS_START = 1;
}

/* ================================================================== */
void loop()
{
    uint32_t now = millis();

    /* V5.32: factory_confirming removed — piezo button handles factory reset
       internally (5 presses in 3s via edge interrupt). */

    /* V5.08: feed hardware watchdog as the FIRST LINE. If loop() blocks
       anywhere (e.g. writeValue stuck inside Cordio HCI), the WDT isn't
       fed, and after 5s the device reboots. */
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    /* ── Path split by mode/state for max LOGGING sample rate ──
       Stream: skip BHY2/BLE/LDC (USB pull path).
       LOGGING: BHY2 + LED + serial only — advertise already off,
                LDC arming irrelevant mid-run, BLE transfer idle.
       Else: full peripheral service. */
    DeviceState loop_st = g_sm.state();
    if (g_stream_active) {
        /* v4.85 + v5.05: do NOT touch Serial while streaming — no handle_serial,
           no escape peek. Pull frames are raw 16B; payload bytes look like cmds
           ('i','s','p', 0x3F inside LE quats, etc.). Stealing even one byte
           desyncs the pull parser → garbage baro → start_det never locks P0 →
           ARM_TIMEOUT (S03 flake on 4.82–4.84, regression on 5.04). Stream exits
           only via end det / ARM_TIMEOUT / POST_RUN / state transitions. */
        g_led.update();  /* athlete must see ARMED/LOGGING */
    } else if (loop_st == DeviceState::LOGGING) {
        /* Real BHY2 LOGGING: keep LED blink (RED_CHASE). show_onboard_blink
           only hits I2C on on/off edges (~few Hz), not every sample.
           Still skip BLE poll + LDC (advertise already off mid-run).
           S04 @ v4.65 was 99.1 fps without LED; v4.66 re-enables blink. */
        BHY2.update();
        g_led.update();
        handle_serial();  /* 'p' / status still needed */
    } else {
        /* V4.96: while streaming a run over BLE, suspend BHY2.update().
           BHI260AP (CS p31) and MX25R flash (CS p26) share SPI bus p3/p4/p5
           via SEPARATE mbed::SPI objects → no shared lock. Alternating
           BHY2.update() (every loop) with read_run_data() (FT chunk) raced the
           bus and wedged the SPI peripheral after a few seconds → loop() hung
           → device unresponsive → LINK_SUPERVISION_TIMEOUT. Sensor data is not
           needed during a download (device is in IDLE). */
        if (!sgc_ble_ft_active()) BHY2.update();
        sgc_ble_poll(); sgc_ble_transfer_poll();
        /* V5.03: desync heal — connect flag set but link gone means the
           disconnect event was missed; force-recover so we don't hold IDLE
           forever / stop being scannable. */
        if (sgc_ble_central_connected() && !BLE.connected())
            sgc_ble_force_recover("desync");
        /* V5.17: zombie BLE link detection — only fire on phantom links (phone
           gone, BLE.connected() false). Do NOT fire on live-but-idle connections
           (athlete navigating the app takes >30s). The desync check above
           handles the first soft-recover attempt; zombie escalates to hard
           radio restart only if the link is truly dead. */
        if (sgc_ble_central_connected() && !BLE.connected() && !sgc_ble_ft_active()) {
            uint32_t idle = millis() - sgc_ble_last_activity_ms();
            if (idle > BLE_ZOMBIE_TIMEOUT_MS) {
                request_ble_radio_restart("zombie");
            }
        }
        /* V5.00: refresh hold from live link + FT (covers event miss / stall abort). */
        g_sm.set_hold_sleep(sgc_ble_central_connected() || BLE.connected() || sgc_ble_ft_active());
        g_led.update();
        handle_serial();
    }

    /* ── Unified state machine (runs regardless of stream mode) ── */
    g_sm.tick();

    /* V5.04: consume a one-shot hard BLE radio restart (serial 'i' / stream
       end / escape). Runs here, outside BLE.poll and transition callbacks, so
       BLE.end() tears down the radio in a safe context. */
    if (g_ble_radio_restart_pending) {
        g_ble_radio_restart_pending = false;
        sgc_ble_radio_restart(g_ble_radio_restart_why);
    }

    /* ── Piezo button arming/factory — real-world, non-stream, non-LOGGING ── */
    if (!g_stream_active && loop_st != DeviceState::LOGGING) {
        g_button.tick();
        
        /* Single press in SLEEP → ARMED */
        if (g_button.is_pressed() && g_sm.state() == DeviceState::SLEEP) {
            g_button.clear_press();
            if (g_sm.can_arm()) {
                json_begin();
                json_kv("ev", "btn_arm");
                json_end();
                g_sm.force_state(DeviceState::ARMED);
            }
        }
        
        /* Factory reset: 5 presses in 3s */
        if (g_button.is_factory_trigger() && g_sm.state() == DeviceState::SLEEP) {
            g_button.clear_factory_trigger();
            json_begin();
            json_kv("ev", "factory_reset");
            Serial.print(','); json_kv("presses", (long)PiezoButton::FACTORY_PRESS_COUNT);
            json_end();
            g_runs.erase_all();
            g_runs.metadata_sync();
            json_begin(); json_kv("ev", "reboot"); json_end();
            g_flash.enter_deep_powerdown();
            delay(50);
            NVIC_SystemReset();
            return;
        }
    }

    /* ── Feed sensors (10 ms). Start det is fed inside feed_sensors() ARMED
       path from the same frame written to the ring (v4.86). No second poll.
       V4.96+: skip entirely while BLE FT is active — production path reads
       BHY2 sensor objects (same SPI bus as flash). ── */
    if (!sgc_ble_ft_active() && now - g_last_sensor_ms >= 10) {
        feed_sensors();
        g_last_sensor_ms += 10;
        if ((int32_t)(now - g_last_sensor_ms) > 50)
            g_last_sensor_ms = now;  /* resync after long stall */
    }

    /* ── IDLE ambient pressure (BHY2) every ~5 s ──
       Production ARMED already feeds live BHY2 into start_det via feed_sensors.
       This keeps a fresh ambient cache for status/diagnostics and refreshes the
       test-mode default frame when no manual B and no stream (so a desk arm
       without inject uses room pressure, not hard-coded 101325).
       NEVER push ambient into g_test_frame during stream — S03 owns P0.
       NEVER during BLE FT (SPI race with flash). */
    if (!g_stream_active
        && !sgc_ble_ft_active()
        && (g_sm.state() == DeviceState::SLEEP || g_sm.state() == DeviceState::SLEEP)
        && (g_last_ambient_ms == 0
            || (now - g_last_ambient_ms) >= AMBIENT_IDLE_MS)) {
        float hpa = pressure.value();  /* BHY2 SENSOR_ID_BARO, hPa */
        if (hpa > 500.0f && hpa < 1100.0f) {
            g_ambient_pa = hpa * 100.0f;  /* hPa → Pa */
            if (test_mode_active() && !g_manual_frame) {
                test_set_pressure_quiet(g_ambient_pa);
            }
        }
        g_last_ambient_ms = now;
    }

    if (now - g_last_battery_ms >= 30000) {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        if (batt >= 0) sgc_ble_set_battery((uint8_t)batt);
        if (batt > 0 && batt < 15 && g_sm.state() == DeviceState::LOGGING) {
            json_begin();
            json_kv("ev", "battery_low");
            Serial.print(','); json_kv("bat", (long)batt);
            json_end();
            g_sm.force_state(DeviceState::POST_RUN);
        }
        g_last_battery_ms = now;
    }

    if (now - g_last_qi_ms >= 1000) { g_last_qi_ms = now; }
    if (now - g_last_cal_ms >= 2000) {
        sgc_ble_set_cal(0);
        g_last_cal_ms = now;
    }
}
