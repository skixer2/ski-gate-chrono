/**
 * Phase 9: Single-binary, always-JSON output (ADR-001, AD-009).
 *
 * JSON-lines is the only serial output format.
 * No #ifdef on output code. No -DTEST_MODE build flag.
 * Test commands (T,B,Q,L,Z) and sensor injection are always compiled in.
 * Production is physically sealed — USB inaccessible, test mode starts OFF.
 * Bench test = worst-case timing (serial TX blocks at 115200 baud).
 * Serial commands (?,a,l,p,s,i,f,R) work identically in every build.
 *
 * Flash layout unchanged from Phase 7.
 */

#include <ArduinoBLE.h>
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
BitPacker      g_packer;
StartDetector  g_start_det;
EndDetector    g_end_det;

/* ================================================================== */
static constexpr uint32_t RUN_FLASH_START = 5 * 4096;
static constexpr uint32_t INDEX_SECTOR    = 4 * 4096;

struct __attribute__((packed)) RunIndex {
    uint32_t magic;
    uint16_t run_count;
    uint16_t crc;
    uint32_t next_run_addr;
};

struct __attribute__((packed)) RunHeader {
    uint8_t  format_ver;
    uint8_t  arm_side;
    uint32_t ts_utc;
    int16_t  baro_temp;
    uint32_t data_size;
    uint8_t  cal_accuracy;
    uint8_t  _pad[3];
};

/* ================================================================== */
static uint32_t g_flash_addr = 0;
static uint32_t g_frame_count = 0;
uint32_t g_next_run_addr = RUN_FLASH_START;
uint16_t g_run_id = 0;
uint32_t g_oldest_run_age = 0;
static bool g_ring_drained = false;

/* ================================================================== */
static uint32_t g_last_sensor_ms  = 0;
static uint32_t g_last_battery_ms = 0;
static uint32_t g_last_status_ms  = 0;
static uint32_t g_last_qi_ms      = 0;
static uint32_t g_last_cal_ms     = 0;

static DeviceState g_prev_state = DeviceState::SLEEP;

/* ================================================================== */
void flash_test();
void apply_state_visuals(DeviceState s);
void handle_serial();
void feed_sensors();
void save_run_index();
void beep_on();  void beep_off();

/* ================================================================== */
void beep_on()  { analogWrite(1, 128); }
void beep_off() { analogWrite(1, 0); }

/* ================================================================== */
void load_run_index()
{
    RunIndex idx;
    g_flash.read_data(INDEX_SECTOR, (uint8_t*)&idx, sizeof(idx));
    if (idx.magic == 0x53474300) {
        g_run_id = idx.run_count;
        g_next_run_addr = idx.next_run_addr;
        if (g_next_run_addr < RUN_FLASH_START)
            g_next_run_addr = RUN_FLASH_START;
        json_begin();
        json_kv("ev", "index");
        Serial.print(','); json_kv("runs", (long)g_run_id);
        Serial.print(','); json_kv("next", (long)g_next_run_addr);
        json_end();
    } else {
        g_run_id = 0;
        g_next_run_addr = RUN_FLASH_START;
        save_run_index();
        json_begin();
        json_kv("ev", "index");
        Serial.print(','); json_kv("runs", 0L);
        json_end();
    }
}

void save_run_index()
{
    g_flash.erase_block(INDEX_SECTOR);
    RunIndex idx;
    memset(&idx, 0, sizeof(idx));
    idx.magic = 0x53474300;
    idx.run_count = g_run_id;
    idx.next_run_addr = g_next_run_addr;
    g_flash.write_page(INDEX_SECTOR, (const uint8_t*)&idx, sizeof(idx));
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
                g_sm.force_state(DeviceState::ARMED);
            }
        }
        break;
    case 'l': g_sm.force_state(DeviceState::LOGGING); break;
    case 'p': g_sm.force_state(DeviceState::POST_RUN); break;
    case 's': g_sm.force_state(DeviceState::SLEEP); break;
    case 'f': flash_test(); return;
    case 'z': {
        /* Hardware Wire LDC1612 test — minimal, safe */
        Serial.print("Wire ping 0x2B: ");
        Wire.begin();
        Wire.setClock(100000);
        Wire.beginTransmission(0x2B);
        uint8_t err = Wire.endTransmission();
        Serial.println(err == 0 ? "ACK" : "NACK");
        if (err == 0) {
            /* Read DEVICE_ID */
            Wire.beginTransmission(0x2B);
            Wire.write(0x7F);
            if (Wire.endTransmission(false) == 0) {
                uint8_t n = Wire.requestFrom(0x2B, (uint8_t)2);
                if (n >= 2) {
                    uint16_t hi = Wire.read(); uint16_t lo = Wire.read();
                    uint16_t dev = (hi << 8) | lo;
                    Serial.print("DEVICE_ID: 0x"); Serial.println(dev, HEX);
                }
            }
        }
        return;
    }
    case 'y': {
        /* Hardware Wire scanner — only works if bus has active pull-ups */
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
        /* GPIO drive test — disconnect Seeed board first!
         * P0.22(SDA) + P0.23(SCL) alternate HIGH/LOW at ~1 Hz.
         * With Seeed board connected, active level translator overrides GPIO. */
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
    case 'R':
        json_begin(); json_kv("ev", "factory_reset"); json_end();
        g_flash.erase_block(INDEX_SECTOR);
        g_run_id = 0;
        g_next_run_addr = RUN_FLASH_START;
        save_run_index();
        json_begin(); json_kv("ev", "reboot"); json_end();
        NVIC_SystemReset();
        return;
    case '?': {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        json_begin();
        json_kv("ev", "status");
        Serial.print(','); json_kv("st", g_sm.state_name());
        Serial.print(','); json_kv("r", (long)g_ring.count());
        Serial.print(','); json_kv("rm", (long)RING_SIZE);
        Serial.print(','); json_kv("p", (long)(pressure.value() * 100));
        Serial.print(','); json_kv("bat", (long)(batt >= 0 ? batt : 0));
        Serial.print(','); json_kv("evc", (long)g_meta_event_count);
        Serial.print(','); json_kv_bool("qi", !digitalRead(10));
        Serial.print(','); json_kv("runs", (long)g_run_id);
        Serial.print(','); json_kv_bool("ldc", g_ldc.is_connected());
        Serial.print(','); json_kv("ldc_raw", (long)g_ldc.data());
        json_end();
        return;
    }
    }
}

/* ================================================================== */
void flash_test()
{
    uint8_t wr[256]; for (int i=0;i<256;i++) wr[i]=(uint8_t)(i*3+7);
    g_flash.erase_block(0);
    g_flash.write_page(0,wr,256);
    uint8_t rd[256]; g_flash.read_data(0,rd,256);
    for (int i=0;i<256;i++) if (rd[i]!=wr[i]) {
        json_begin();
        json_kv("ev", "flash");
        Serial.print(','); json_kv_bool("ok", false);
        Serial.print(','); json_kv("err_at", (long)i);
        json_end();
        return;
    }
    json_begin();
    json_kv("ev", "flash");
    Serial.print(','); json_kv_bool("ok", true);
    json_end();
}

/* ================================================================== */
void feed_sensors()
{
    RawFrame f;

    if (test_mode_active()) {
        f.q_w = (int16_t)(test_get_quat_w() * 16384.0f);
        f.q_x = (int16_t)(test_get_quat_x() * 16384.0f);
        f.q_y = (int16_t)(test_get_quat_y() * 16384.0f);
        f.q_z = (int16_t)(test_get_quat_z() * 16384.0f);
        f.la_x = (int16_t)test_get_lax();
        f.la_y = (int16_t)test_get_lay();
        f.la_z = (int16_t)test_get_laz();
        f.baro_pa_div4 = (uint16_t)(test_get_pressure() * 25.0f);
    } else {
        f.q_w = (int16_t)(rotation.x() * 16384.0f);
        f.q_x = (int16_t)(rotation.y() * 16384.0f);
        f.q_y = (int16_t)(rotation.z() * 16384.0f);
        f.q_z = (int16_t)(rotation.w() * 16384.0f);
        f.la_x = (int16_t)lin_acc.x();
        f.la_y = (int16_t)lin_acc.y();
        f.la_z = (int16_t)lin_acc.z();
        f.baro_pa_div4 = (uint16_t)(pressure.value() * 25.0f);
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
        }
        if (g_start_det.feed(f.baro_pa_div4))
            g_sm.force_state(DeviceState::LOGGING);
        return;
    }

    if (st == DeviceState::LOGGING) {
        if (!g_ring_drained) {
            RunHeader hdr;
            memset(&hdr, 0, sizeof(hdr));
            hdr.format_ver = 2;
            hdr.arm_side = 0;
            hdr.baro_temp = (int16_t)(temperature.value() * 10.0f);
            hdr.cal_accuracy = 0;

            g_flash.erase_block(g_next_run_addr);
            g_flash.write_page(g_next_run_addr, (const uint8_t*)&hdr, sizeof(hdr));
            g_flash_addr = g_next_run_addr + sizeof(hdr);
            g_frame_count = 0;

            uint16_t pre_count = g_ring.count();
            for (uint16_t i = 0; i < pre_count; i++) {
                RawFrame rf = g_ring.read();
                uint8_t sz = g_packer.encode(rf, millis());
                g_flash.write_page(g_flash_addr, g_packer.buffer(), sz);
                g_flash_addr += sz; g_frame_count++;
            }
            g_ring_drained = true;
            json_begin();
            json_kv("ev", "log_start");
            Serial.print(','); json_kv("run", (long)(g_run_id + 1));
            Serial.print(','); json_kv("pre", (long)pre_count);
            json_end();
        } else {
            uint8_t sz = g_packer.encode(f, millis());
            if ((g_flash_addr % g_flash.block_size()) == 0)
                g_flash.erase_block(g_flash_addr);
            g_flash.write_page(g_flash_addr, g_packer.buffer(), sz);
            g_flash_addr += sz; g_frame_count++;
        }

        if (g_end_det.feed(f.la_x, f.la_y, f.la_z)) {
            json_begin();
            json_kv("ev", "end_detected");
            Serial.print(','); json_kv("fr", (long)g_frame_count);
            json_end();
            g_sm.force_state(DeviceState::POST_RUN);
        }
        return;
    }
}

/* ================================================================== */
void setup()
{
    Serial.begin(115200); delay(300);

    json_begin();
    json_kv("ev", "boot");
    Serial.print(','); json_kv("ver", "2.3");
    json_end();

    nicla::begin();
    g_led.begin();

    pinMode(9, OUTPUT); digitalWrite(9, LOW);
    pinMode(0, INPUT_PULLUP);

    bool flash_ok = g_flash.begin();
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "flash");
    Serial.print(','); json_kv_bool("ok", flash_ok);
    json_end();

    bool ldc_ok = g_ldc.begin();
    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "ldc1612");
    Serial.print(','); json_kv_bool("ok", ldc_ok);
    if (ldc_ok) {
        Serial.print(','); json_kv("dev_id", (long)g_ldc.read_device_id());
        Serial.print(','); json_kv("manuf", (long)g_ldc.read_manufacturer_id());
        Serial.print(','); json_kv("baseline", (long)g_ldc.data());
        g_ldc.enable_interrupt();
    }
    json_end();
    load_run_index();

    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "bhy2");
    bool bhy2_ok = BHY2.begin();
    Serial.print(','); json_kv_bool("ok", bhy2_ok);
    json_end();
    if (!bhy2_ok) { while(1) delay(1000); }
    rotation.begin(); lin_acc.begin(); pressure.begin(); temperature.begin();

    json_begin();
    json_kv("ev", "init");
    Serial.print(','); json_kv("sub", "ble");
    bool ble_ok = BLE.begin();
    Serial.print(','); json_kv_bool("ok", ble_ok);
    json_end();
    if (!ble_ok) { while(1) delay(1000); }
    sgc_ble_init();
    sgc_ble_transfer_init();

    test_mode_init();
    bhy2_cal_hook_init();

    g_sm.force_state(DeviceState::IDLE);
    g_prev_state = g_sm.state();
    apply_state_visuals(g_sm.state());

    int8_t batt = nicla::getBatteryVoltagePercentage();
    sgc_ble_set_battery(batt >= 0 ? (uint8_t)batt : 0);
    sgc_ble_set_run_count(g_run_id);

    json_begin();
    json_kv("ev", "ready");
    Serial.print(','); json_kv("st", g_sm.state_name());
    Serial.print(','); json_kv("runs", (long)g_run_id);
    json_end();
}

/* ================================================================== */
void loop()
{
    uint32_t now = millis();
    BHY2.update(); sgc_ble_poll(); sgc_ble_transfer_poll();
    g_led.update(); g_sm.tick(); g_ldc.tick(); handle_serial();

    /* ── LDC1612 proximity arming (F03) ── */
    if (g_ldc.is_armed() && g_sm.state() == DeviceState::IDLE) {
        if (g_sm.can_arm()) {
            json_begin();
            json_kv("ev", "prox_arm");
            Serial.print(','); json_kv("prox_ms", (long)g_ldc.proximity_ms());
            json_end();
            g_sm.force_state(DeviceState::ARMED);
        }
    }
    if (g_ldc.is_factory_hold() && g_sm.state() == DeviceState::IDLE) {
        json_begin();
        json_kv("ev", "factory_reset");
        Serial.print(','); json_kv("hold_ms", (long)g_ldc.proximity_ms());
        json_end();
        g_flash.erase_block(INDEX_SECTOR);
        g_run_id = 0;
        g_next_run_addr = RUN_FLASH_START;
        save_run_index();
        json_begin(); json_kv("ev", "reboot"); json_end();
        NVIC_SystemReset();
        return;
    }

    if (now - g_last_sensor_ms >= 10) {
        feed_sensors();
        g_last_sensor_ms = now;
    }

    DeviceState cur = g_sm.state();
    if (cur != g_prev_state) {
        json_state_evt(g_sm.state_name_for(g_prev_state), g_sm.state_name());
        if (cur == DeviceState::ARMED) {
            g_ring.reset(); g_packer.reset(); g_start_det.reset();
            g_ring_drained = false;
        }
        if (cur == DeviceState::LOGGING) {
            g_end_det.reset();
        }
        if (cur == DeviceState::POST_RUN) {
            RunHeader hdr;
            uint32_t hdr_addr = g_next_run_addr;
            g_flash.read_data(hdr_addr, (uint8_t*)&hdr, sizeof(hdr));
            hdr.data_size = g_flash_addr - g_next_run_addr - sizeof(hdr);
            g_flash.write_page(hdr_addr, (const uint8_t*)&hdr, sizeof(hdr));

            g_next_run_addr = ((g_flash_addr + 4095) / 4096) * 4096;
            g_run_id++;
            save_run_index();
            sgc_ble_set_run_count(g_run_id);

            json_begin();
            json_kv("ev", "run_saved");
            Serial.print(','); json_kv("id", (long)g_run_id);
            Serial.print(','); json_kv("fr", (long)g_frame_count);
            Serial.print(','); json_kv("sz", (long)hdr.data_size);
            Serial.print(','); json_kv("cal", (long)hdr.cal_accuracy);
            json_end();
        }
        apply_state_visuals(cur);
        g_prev_state = cur;
    }

    // Periodic status — always silent (ADR-001: no periodic output)

    if (now - g_last_battery_ms >= 30000) {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        if (batt >= 0) sgc_ble_set_battery((uint8_t)batt);
        if (batt > 0 && batt < 15 && cur == DeviceState::LOGGING) {
            json_begin();
            json_kv("ev", "battery_low");
            Serial.print(','); json_kv("bat", (long)batt);
            json_end();
            g_sm.force_state(DeviceState::SLEEP);
        }
        g_last_battery_ms = now;
    }

    if (now - g_last_qi_ms >= 1000) { g_last_qi_ms = now; }
    if (now - g_last_cal_ms >= 2000) {
        sgc_ble_set_cal(0);
        g_last_cal_ms = now;
    }
}
