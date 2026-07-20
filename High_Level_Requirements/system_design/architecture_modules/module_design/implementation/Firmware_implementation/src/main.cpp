/**
 * Phase 12: LittleFS storage — LittleFSStorage replaces FlashManager.
 *
 * JSON-lines is the only serial output format (ADR-001, AD-009).
 * Test commands always compiled in, test mode starts OFF.
 *
 * Flash layout:
 *   Sectors 0-3:    Flash ring buffer (flash_ring.cpp)
 *   Sectors 4-511:  LittleFS run storage (littlefs_storage.cpp)
 */

#include <ArduinoBLE.h>
#include "nrf_power.h"
#include "Arduino_BHY2.h"
#include <Nicla_System.h>
#include <math.h>

extern volatile uint8_t g_bhy2_accuracy[256];
extern volatile uint8_t g_meta_event_count;
void bhy2_cal_hook_init();
#include "ble/sgc_service.h"
#include "ble/file_transfer.h"
#include "test_mode.h"
#include "test_json.h"
#include "led/led.h"
#include "sensors/ldc1612.h"
#include "config.h"
#include "state_machine/state_machine.h"
#include "state_machine/start_detector.h"
#include "state_machine/end_detector.h"
#include "storage/spi_flash.h"
#include "storage/flash_ring.h"
#include "storage/littlefs_storage.h"
#include "storage/ring_buffer.h"
#include "storage/bit_packer.h"

/* ================================================================== */
SensorQuaternion rotation(SENSOR_ID_RV);
SensorXYZ        lin_acc(SENSOR_ID_LACC);
Sensor           pressure(SENSOR_ID_BARO);
Sensor           temperature(SENSOR_ID_TEMP);

/* ================================================================== */
LED            g_led(0, 0);
LDC1612        g_ldc;
StateMachine   g_sm;
SPIFlash       g_flash;
FlashRing      g_ring(g_flash);
LittleFSStorage g_fs;
BitPacker      g_packer;
StartDetector  g_start_det;
EndDetector    g_end_det;

/* ================================================================== */
/* ── Run state during LOGGING ─────────────────────────────────── */
static uint32_t g_frame_count    = 0;   /* frames written this run */
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

static DeviceState g_prev_state = DeviceState::SLEEP;
bool g_stream_active = false;  /* 'S' command — binary frame ingestion */
uint32_t g_stream_frames = 0;   /* frames received in stream mode */

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
    g_fs.append_data(g_page_buf, g_page_cursor);
    g_page_cursor = 0;
}

/* ================================================================== */
void apply_state_visuals(DeviceState s)
{
    switch (s) {
    case DeviceState::SLEEP:
        g_led.set_pattern(LedPattern::OFF); break;
    case DeviceState::IDLE:
        g_led.set_pattern(LedPattern::BLUE_SLOW_FLOW); break;
    case DeviceState::ARMED:
        g_led.set_pattern(LedPattern::GREEN_CHASE); beep_on(); break;
    case DeviceState::LOGGING:
        g_led.set_pattern(LedPattern::RED_CHASE); beep_off(); break;
    case DeviceState::POST_RUN:
        g_led.set_pattern(LedPattern::BLUE_SLOW_FLOW_POST); break;
    }
    sgc_ble_update_state(s);
}

/* ================================================================== */
void handle_serial()
{
    if (!Serial.available()) return;
    char c = Serial.read();

    /* Stream mode: read 18-byte RawFrame frames (2B sync + 16B payload).
       Identical format to real peripheral — feed_sensors copies directly. */
    if (g_stream_active) {
        static uint8_t  sbuf[18];
        static uint8_t  spos = 0;
        for (;;) {
            if (spos == 0) {
                if (c == 0xAA) spos = 1;
            } else if (spos == 1) {
                if (c == 0xAA) { /* re-sync */ }
                else if (c == 0x55) { sbuf[0]=0xAA; sbuf[1]=0x55; spos=2; }
                else spos = 0;
            } else {
                sbuf[spos++] = c;
                if (spos >= 18) {
                    spos = 0;
                    RawFrame rf;
                    memcpy(&rf, sbuf + 2, sizeof(RawFrame));
                    /* All-zeros = sentinel (quat magnitude 0 is physically
                       impossible).  Emit stream_end for the test harness. */
                    if (rf.q_w == 0 && rf.q_x == 0 && rf.q_y == 0 && rf.q_z == 0
                     && rf.la_x == 0 && rf.la_y == 0 && rf.la_z == 0
                     && rf.baro_pa_div2 == 0) {
                        g_stream_active = false;
                        json_begin(); json_kv("ev", "stream_end");
                        Serial.print(','); json_kv("frames", (long)g_stream_frames);
                        json_end();
                        g_stream_frames = 0;
                        return;
                    }
                    test_set_frame(rf);
                    g_stream_frames++;
                }
            }
            if (!Serial.available()) return;
            c = Serial.read();
        }
    }

    if (test_mode_handle_serial(c)) return;

    switch (c) {
    case 'i': g_sm.force_state(DeviceState::IDLE); break;
    case 'a':
        if (g_sm.state() == DeviceState::IDLE) {
            float qx = rotation.x(), qy = rotation.y();
            float qz = rotation.z(), qw = rotation.w();
            float mag = sqrtf(qw*qw + qx*qx + qy*qy + qz*qz);
            if (mag < 0.8f || mag > 1.2f) {
                json_begin();
                json_kv("ev", "arm_refused");
                Serial.print(','); json_kv("reason", "quat_magnitude");
                Serial.print(','); json_kv("mag", mag);
                json_end();
            } else {
                /* ── Pressure units: hPa→Pa (×100.0f) ── */
                float pa = test_mode_active()
                    ? (float)test_get_frame().baro_pa_div2 * 2.0f   /* Pa/2→Pa */
                    : pressure.value() * 100.0f;                     /* hPa→Pa */
                if (test_mode_active()) { /* use injected pressure as-is */ }
                else if (pa > 50000.0f && pa < 110000.0f) { /* plausible real pressure */ }
                else { pa = 101325.0f; }
                g_start_det.reset(pa);
                g_sm.force_state(DeviceState::ARMED);
            }
        }
        break;
    case 'l':
        g_end_det.reset();
        g_sm.force_state(DeviceState::LOGGING);
        break;
    case 'p': g_sm.force_state(DeviceState::POST_RUN); break;
    case 's': g_sm.force_state(DeviceState::SLEEP); break;
    case 'f': if (g_sm.state() != DeviceState::ARMED && g_sm.state() != DeviceState::LOGGING) flash_test(); return;
    case 'z': {
        json_begin();
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
    case 'd': g_fs.list_files(); return;
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
        pinMode(22, OUTPUT); pinMode(23, OUTPUT);
        Serial.println("Toggling (disconnect Seeed first!) — any key to stop");
        while (!Serial.available()) {
            digitalWrite(22, HIGH); digitalWrite(23, LOW);  delay(500);
            digitalWrite(22, LOW);  digitalWrite(23, HIGH); delay(500);
        }
        while (Serial.available()) Serial.read();
        pinMode(22, INPUT_PULLUP); pinMode(23, INPUT_PULLUP);
        Serial.println("Stopped.");
        return;
    }
    case '!':
        json_begin(); json_kv("ev", "reboot"); json_end();
        Serial.flush();
        /* V2.89: Deep power-down protects flash through reset.
           Flash ignores all SPI commands in DP — CS glitch is harmless.
           Released at boot in SPIFlash::begin() via 0xAB. */
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
        g_fs.erase_all();
        json_begin(); json_kv("ev", "reboot"); json_end();
        g_flash.enter_deep_powerdown();
        delay(50);
        NVIC_SystemReset();
        return;
    case '?': {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        json_begin();
        json_kv("ev", "status");
        Serial.print(','); json_kv("st", g_sm.state_name());
        Serial.print(','); json_kv("r", (long)g_ring.count());
        Serial.print(','); json_kv("rm", (long)RING_SIZE);
        Serial.print(','); json_kv("p", (long)(pressure.value() * 100));   /* hPa→Pa for display */
        Serial.print(','); json_kv("bat", (long)(batt >= 0 ? batt : 0));
        Serial.print(','); json_kv("evc", (long)g_meta_event_count);
        Serial.print(','); json_kv_bool("qi", !digitalRead(10));
        Serial.print(','); json_kv("runs", (long)g_fs.run_count());
        Serial.print(','); json_kv("total_runs", (long)g_fs.total_run_count());
        Serial.print(','); json_kv("oldest_age", (long)g_fs.oldest_run_age());
        Serial.print(','); json_kv_bool("ldc", g_ldc.is_connected());
        Serial.print(','); json_kv("ldc_raw", (long)g_ldc.data());
        Serial.print(','); json_kv("flash_pct", (long)g_fs.flash_used_pct());
        Serial.print(','); json_kv("ver", FW_VERSION);
        json_end();
        return;
    }
    }
}

/* ================================================================== */
void flash_test()
{
    static constexpr uint32_t TEST_ADDR = 0;
    if (!g_flash.erase_block(TEST_ADDR)) { json_begin(); json_kv("ev","flash"); json_kv_bool("ok",false); json_end(); return; }
    uint8_t wr[256]; for(int i=0;i<256;i++) wr[i]=(uint8_t)(i*3+7);
    if (!g_flash.write_page(TEST_ADDR,wr,256)) { json_begin(); json_kv("ev","flash"); json_kv_bool("ok",false); json_end(); return; }
    uint8_t rd[256];
    if (!g_flash.read_data(TEST_ADDR,rd,256)) { json_begin(); json_kv("ev","flash"); json_kv_bool("ok",false); json_end(); return; }
    for (int i=0;i<256;i++) if (rd[i]!=wr[i]) {
        json_begin(); json_kv("ev","flash"); json_kv_bool("ok",false);
        json_kv("err_at",(long)i); json_end(); return;
    }
    json_begin(); json_kv("ev","flash"); json_kv_bool("ok",true); json_end();
}

/* ================================================================== */
void feed_sensors()
{
    RawFrame f;

    /* ── Test mode: copy injected frame directly — same 16B format
       as the real peripheral output.  Stream parser updates it via
       test_set_frame().  B/Q/L commands set individual fields. ── */
    if (test_mode_active()) {
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

    DeviceState st = g_sm.state();
    if (st == DeviceState::SLEEP) return;

    if (st == DeviceState::ARMED) {
        if (!g_ring.is_full()) {
            g_ring.write(f);
            if (g_ring.is_full()) {
                json_begin();
                json_kv("ev", "ring_full");
                Serial.print(','); json_kv("r", (long)RING_SIZE);
                json_end();
            }
        } else {
            g_ring.read();
            g_ring.write(f);
        }
        return;
    }

    if (st == DeviceState::LOGGING) {
        uint8_t count = g_ring.count();
        uint8_t pop_n = count >= 2 ? 2 : count;

        for (uint8_t i = 0; i < pop_n; i++) {
            RawFrame oldest = g_ring.read();
            g_packer.encode(oldest, millis());
            uint8_t cf_size = g_packer.last_size();
            const uint8_t* cf_buf = g_packer.buffer();

            if (g_page_cursor + cf_size > PAGE_BUF_SIZE) {
                flush_page_buffer();
            }
            memcpy(g_page_buf + g_page_cursor, cf_buf, cf_size);
            g_page_cursor += cf_size;
            g_frame_count++;
        }

        g_ring.write(f);

        /* ── End detector (LOGGING→POST_RUN) — Pa ── */
        float pa_raw = test_mode_active()
            ? (float)test_get_frame().baro_pa_div2 * 2.0f   /* Pa/2→Pa */
            : pressure.value() * 100.0f;                     /* hPa→Pa */
        if (g_end_det.feed(pa_raw))
            g_sm.force_state(DeviceState::POST_RUN);
        return;
    }
}

/* ================================================================== */
void setup()
{
    nicla::begin();
    Serial.begin(115200);
    delay(500);   /* let pio device monitor open the port before we print */

    /* ── Version FIRST — no delay, no preamble ── */
    uint32_t rr = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFF;  /* clear for next boot */
    Serial.print("{\"ev\":\"boot\",\"ver\":\"");
    Serial.print(FW_VERSION);
    Serial.print("\",\"rr\":");
    Serial.print(rr);
    Serial.println("}");
    Serial.flush();
    delay(50);

    /* ── LED ── */
    g_led.begin();

    /* ── SPI Flash (MX25R1635F on SPI0, CS_FLASH=p26) ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "flash");
    bool flash_ok = g_flash.begin();
    Serial.print(','); json_kv_bool("ok", flash_ok);
    json_end();
    if (!flash_ok) { while(1) { g_led.set_pattern(LedPattern::OFF); delay(1000); } }

    /* ── Flash ring buffer ── */
    g_ring.reset();

    /* ── LDC1612 proximity ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "ldc1612");
    bool ldc_ok = g_ldc.begin();
    Serial.print(','); json_kv_bool("ok", ldc_ok);
    Serial.print(','); json_kv("dev_id", (long)g_ldc.read_device_id());
    Serial.print(','); json_kv("manuf", (long)g_ldc.read_manufacturer_id());
    Serial.print(','); json_kv("baseline", (long)g_ldc.baseline());
    Serial.print(','); json_kv_bool("bl_ok", g_ldc.baseline_valid());
    json_end();
    g_ldc.enable_interrupt();

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

    /* ── LittleFS (after BD init, before BHY2 for heap) ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "littlefs");
    json_end();
    Serial.flush();   /* ensure init line is complete before begin() prints */
    bool fs_ok = g_fs.begin();
    Serial.print("{\"ev\":\"init\",\"sub\":\"littlefs_res\",\"ok\":");
    Serial.print(fs_ok ? "1" : "0");
    Serial.println("}");

    /* fs_ok false → storage offline (usually CS glitch during reset).
       Device continues without storage — BLE, sensors, state machine
       all work. Factory reset ('R') will reformat and remount. */
    if (!fs_ok) {
        json_begin(); json_kv("ev","warn"); Serial.print(',');
        json_kv("msg","storage_offline"); json_end();
    }

    /* ── BHY2 init (standalone — only sensor hub, no BLE/I2C/DFU handlers) ── */
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "bhy2");
    bool bhy2_ok = BHY2.begin(NICLA_STANDALONE);
    Serial.print(','); json_kv_bool("ok", bhy2_ok);
    json_end();
    if (bhy2_ok) {
        rotation.begin(); lin_acc.begin();
        pressure.begin(); temperature.begin();
    }

    test_mode_init();
    bhy2_cal_hook_init();

    g_sm.force_state(DeviceState::IDLE);
    g_prev_state = g_sm.state();
    apply_state_visuals(g_sm.state());

    int8_t batt = nicla::getBatteryVoltagePercentage();
    sgc_ble_set_battery(batt >= 0 ? (uint8_t)batt : 0);
    sgc_ble_set_run_count(g_fs.run_count());
    sgc_ble_set_flash_used(g_fs.flash_used_pct());

    json_begin();
    json_kv("ev", "ready");
    Serial.print(','); json_kv("st", g_sm.state_name());
    Serial.print(','); json_kv("runs", (long)g_fs.run_count());
    Serial.print(','); json_kv("ver", FW_VERSION);
    Serial.print(','); json_kv("used_pct", (long)g_fs.flash_used_pct());
    json_end();
}

/* ================================================================== */
void loop()
{
    uint32_t now = millis();

    static bool     g_factory_confirming    = false;
    static uint32_t g_factory_confirm_start = 0;
    static constexpr uint32_t FACTORY_CONFIRM_MS = 3000;
    static constexpr uint32_t FACTORY_LED_BLINK_MS = 250;

    /* ── Stream mode: maximize parser throughput. Skip heavy ops
       (BHY2, BLE, LDC) for higher loop rate.  Keep all state
       transitions including LOGGING (create_run) and POST_RUN
       (close_run). ── */
    if (g_stream_active) {
        for (int i = 0; i < 40; i++) handle_serial();
        g_led.update();
        g_sm.tick();

        if (now - g_last_baro_ms >= 100 && g_sm.state() == DeviceState::ARMED) {
            float pa = test_mode_active()
                ? (float)test_get_frame().baro_pa_div2 * 2.0f
                : pressure.value() * 100.0f;
            if (g_start_det.feed(pa))
                g_sm.force_state(DeviceState::LOGGING);
            g_last_baro_ms = now;
        }

        DeviceState cur = g_sm.state();
        if (cur != g_prev_state) {
            json_state_evt(g_sm.state_name_for(g_prev_state), g_sm.state_name());
            if (cur == DeviceState::ARMED) {
                g_ring.reset(); g_packer.reset();
                g_page_cursor = 0;
                g_run_created = false;
                g_last_baro_ms = now;
            }
            if (cur == DeviceState::LOGGING) {
                BLE.stopAdvertise();
                g_end_det.reset();
                int16_t baro_temp = (int16_t)(temperature.value() * 10.0f);
                uint8_t cal = 0;
                g_run_created = g_fs.create_run(0, baro_temp, cal);
                g_frame_count = 0;
                json_begin(); json_kv("ev", "run_created");
                Serial.print(','); json_kv_bool("ok", g_run_created);
                Serial.print(','); json_kv("id", (long)g_fs.total_run_count());
                json_end();
            }
            if (cur == DeviceState::POST_RUN) {
                BLE.advertise();
                /* Natural end-detector transition = stream complete */
                if (g_stream_active) {
                    g_stream_active = false;
                    json_begin(); json_kv("ev", "stream_end");
                    Serial.print(','); json_kv("frames", (long)g_stream_frames);
                    json_end();
                    g_stream_frames = 0;
                }
                flush_page_buffer();
                uint16_t run_id = g_fs.close_run(g_frame_count);
                sgc_ble_set_run_count(g_fs.run_count());
                sgc_ble_set_flash_used(g_fs.flash_used_pct());
                json_begin(); json_kv("ev", "run_saved");
                Serial.print(','); json_kv("id", (long)run_id);
                Serial.print(','); json_kv("fr", (long)g_frame_count);
                Serial.print(','); json_kv_bool("ok", run_id != 0xFFFF);
                Serial.print(','); json_kv("runs", (long)g_fs.run_count());
                json_end();
            }
            apply_state_visuals(cur);
            g_prev_state = cur;
        }

        if (now - g_last_sensor_ms >= 10) {
            feed_sensors();
            g_last_sensor_ms = now;
        }
        return;
    }

    BHY2.update(); sgc_ble_poll(); sgc_ble_transfer_poll();
    g_led.update(); g_sm.tick(); g_ldc.tick();
    if (!g_stream_active) handle_serial();  /* normal command parsing */

    /* ── LDC1612 wake from SLEEP (F13) ── */
    if (g_ldc.is_proximity() && g_sm.state() == DeviceState::SLEEP) {
        json_begin();
        json_kv("ev", "wake");
        Serial.print(','); json_kv("prox_ms", (long)g_ldc.proximity_ms());
        json_end();
        g_sm.force_state(DeviceState::IDLE);
    }

    /* ── LDC1612 proximity arming (F03) ── */
    if (g_ldc.is_armed() && g_sm.state() == DeviceState::IDLE) {
        if (g_sm.can_arm()) {
            json_begin();
            json_kv("ev", "prox_arm");
            Serial.print(','); json_kv("prox_ms", (long)g_ldc.proximity_ms());
            json_end();
            /* ── Pressure units: hPa→Pa (×100.0f) — see block comment at line ~140 ── */
            float pa = test_mode_active()
                ? (float)test_get_frame().baro_pa_div2 * 2.0f   /* Pa/2→Pa */
                : pressure.value() * 100.0f;                     /* hPa→Pa */
            g_start_det.reset(pa);
            g_sm.force_state(DeviceState::ARMED);
        }
    }
    /* ── Factory reset with confirmation ── */
    if (g_ldc.is_factory_hold() && g_sm.state() == DeviceState::IDLE) {
        if (!g_factory_confirming) {
            g_factory_confirming    = true;
            g_factory_confirm_start = now;
            json_begin();
            json_kv("ev", "factory_warn");
            Serial.print(','); json_kv("hold_ms", (long)g_ldc.proximity_ms());
            json_end();
        }

        uint32_t elapsed = now - g_factory_confirm_start;
        if (elapsed < FACTORY_CONFIRM_MS) {
            if (!g_ldc.is_proximity()) {
                json_begin();
                json_kv("ev", "factory_cancelled");
                json_end();
                g_factory_confirming = false;
                nicla::leds.setColor(0, 0, 0);
                return;
            }
            bool led_on = ((elapsed / FACTORY_LED_BLINK_MS) % 2) == 0;
            nicla::leds.setColor(led_on ? 80 : 0, 0, 0);
            return;
        }

        nicla::leds.setColor(0, 0, 0);
        g_factory_confirming = false;
        json_begin();
        json_kv("ev", "factory_reset");
        Serial.print(','); json_kv("hold_ms", (long)g_ldc.proximity_ms());
        json_end();
        g_fs.erase_all();
        json_begin(); json_kv("ev", "reboot"); json_end();
        g_flash.enter_deep_powerdown();
        delay(50);
        NVIC_SystemReset();
        return;
    }

    if (!g_ldc.is_proximity() && g_factory_confirming) {
        g_factory_confirming = false;
        nicla::leds.setColor(0, 0, 0);
    }

    if (now - g_last_sensor_ms >= 10) {
        feed_sensors();
        g_last_sensor_ms = now;
    }

    /* ── Start detector feed at 10 Hz (ARMED→LOGGING) — Pa ── */
    if (now - g_last_baro_ms >= 100 && g_sm.state() == DeviceState::ARMED) {
        float pa = test_mode_active()
            ? (float)test_get_frame().baro_pa_div2 * 2.0f   /* Pa/2→Pa */
            : pressure.value() * 100.0f;                     /* hPa→Pa */
        if (g_start_det.feed(pa))
            g_sm.force_state(DeviceState::LOGGING);
        g_last_baro_ms = now;
    }

    DeviceState cur = g_sm.state();
    if (cur != g_prev_state) {
        json_state_evt(g_sm.state_name_for(g_prev_state), g_sm.state_name());
        if (cur == DeviceState::ARMED) {
            g_ring.reset(); g_packer.reset();
            g_page_cursor = 0;
            g_run_created = false;
        }
        if (cur == DeviceState::LOGGING) {
            /* V2.80 (H4 fix): Suspend BLE advertising during logging.
               BLE TX current peaks (5-12mA) combined with flash page
               program current (15mA) can sag battery voltage below
               MX25R minimum. Stopping advertising eliminates the
               highest-current BLE activity while keeping connections. */
            BLE.stopAdvertise();
            g_end_det.reset();

            int16_t baro_temp = (int16_t)(temperature.value() * 10.0f);
            uint8_t cal = 0;

            g_run_created = g_fs.create_run(0, baro_temp, cal);
            g_frame_count = 0;

            json_begin();
            json_kv("ev", "run_created");
            Serial.print(','); json_kv_bool("ok", g_run_created);
            Serial.print(','); json_kv("id", (long)g_fs.total_run_count());
            json_end();
        }
        if (cur == DeviceState::POST_RUN) {
            /* Resume BLE advertising after logging completes */
            BLE.advertise();
            flush_page_buffer();

            uint16_t run_id = g_fs.close_run(g_frame_count);

            sgc_ble_set_run_count(g_fs.run_count());
            sgc_ble_set_flash_used(g_fs.flash_used_pct());

            json_begin();
            json_kv("ev", "run_saved");
            Serial.print(','); json_kv("id", (long)run_id);
            Serial.print(','); json_kv("fr", (long)g_frame_count);
            Serial.print(','); json_kv_bool("ok", run_id != 0xFFFF);
            long diag = run_id != 0xFFFF ? 0
                      : (!g_run_created ? 1 : 2);
            Serial.print(','); json_kv("wh", diag);
            Serial.print(','); json_kv("runs", (long)g_fs.run_count());
            Serial.print(','); json_kv("total", (long)g_fs.total_run_count());
            Serial.print(','); json_kv("ec", (long)g_fs.run_count());
            Serial.print(','); json_kv("tc", (long)g_fs.total_run_count());
            json_end();
        }
        apply_state_visuals(cur);
        g_prev_state = cur;
    }

    if (now - g_last_battery_ms >= 30000) {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        if (batt >= 0) sgc_ble_set_battery((uint8_t)batt);
        if (batt > 0 && batt < 15 && cur == DeviceState::LOGGING) {
            json_begin();
            json_kv("ev", "battery_low");
            Serial.print(','); json_kv("bat", (long)batt);
            json_end();
            g_sm.force_state(DeviceState::POST_RUN);  /* route through close_run */
        }
        g_last_battery_ms = now;
    }

    if (now - g_last_qi_ms >= 1000) { g_last_qi_ms = now; }
    if (now - g_last_cal_ms >= 2000) {
        sgc_ble_set_cal(0);
        g_last_cal_ms = now;
    }
}
