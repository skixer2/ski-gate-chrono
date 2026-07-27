/**
 * Phase 12: LittleFS storage — LittleFSStorage replaces FlashManager.
 *
 * JSON-lines is the only serial output format (ADR-001, AD-009).
 * Test commands always compiled in, test mode starts OFF.
 *
 * Flash layout:
 *   Sectors 0-5:    Flash ring buffer (flash_ring.cpp) — 20-byte RingEntry
 *   Sectors 6-507:  LittleFS run storage (littlefs_storage.cpp)
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
bool g_stream_active = false;  /* 'S' command — pull model frame ingestion */
uint32_t g_stream_frames = 0;   /* frames received in stream mode */
static bool g_ring_drained = false;  /* V4.16: true after ring drain in pull-model LOGGING */

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

    /* V4.13: Pull model — no binary parser needed. Frames are pulled
       by feed_sensors() → test_request_frame() via request-response.
       In stream mode, handle_serial() skips the binary path entirely. */

    if (test_mode_handle_serial(c)) return;

    switch (c) {
    case 'i': g_sm.force_state(DeviceState::IDLE); break;
    case 'a':
        if (g_sm.state() == DeviceState::IDLE) {
            float qx = rotation.x(), qy = rotation.y();
            float qz = rotation.z(), qw = rotation.w();
            float mag = sqrtf(qw*qw + qx*qx + qy*qy + qz*qz);
            /* V2.95: Skip quaternion check in test mode — BHY2 may not
               be stable yet after factory reset, but test frames have
               known-good synthetic data. */
            if (!test_mode_active() && (mag < 0.8f || mag > 1.2f)) {
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
    case 'h': {
        /* Hex/baro dump for a specific run.
           Uses same read_run_data() as BLE file transfer — fork at output.
           h <run_id>              → anchors every 100th frame (T3)
           h <run_id> <from> <to>  → baro for every frame in range
           Add trailing 'r' for raw hex: h <id> <from> <to> r */
        long rid = Serial.parseInt();
        if (rid < 0 || rid > 65535) { json_begin(); json_kv("ev","hex_err"); json_kv("reason","bad_id"); json_end(); return; }

        RunHeader hdr;
        if (!g_fs.read_run_header((uint16_t)rid, hdr)) {
            json_begin(); json_kv("ev","hex_err"); json_kv("reason","no_run"); json_kv("id",rid); json_end(); return;
        }
        uint32_t data_sz = hdr.data_size;
        if (data_sz == 0 || data_sz > 200000) { json_begin(); json_kv("ev","hex_err"); json_kv("reason","bad_size"); json_kv("sz",(long)data_sz); json_end(); return; }

        Serial.print("{\"ev\":\"hex_dump\",\"id\":"); Serial.print(rid);
        Serial.print(",\"sz\":"); Serial.print((long)data_sz);
        /* Argument parsing:
           h <id>            → anchors every 100th frame
           h <id> raw        → hex string of entire file (same bytes as BLE→phone)
           h <id> <from> <to>  → baro for range
           h <id> <from> <to> r → raw hex for range */
        long from_frame = -1, to_frame = -1;
        bool raw_mode = false, has_range = false, raw_file_mode = false;
        while (Serial.available() && (Serial.peek() == ' ' || Serial.peek() == '\t')) Serial.read();
        if (Serial.available() && Serial.peek() != '\n' && Serial.peek() != '\r') {
            if (Serial.peek() >= '0' && Serial.peek() <= '9') {
                from_frame = Serial.parseInt(); to_frame = Serial.parseInt();
                has_range = (from_frame >= 0 && to_frame >= from_frame && to_frame < 100000);
                while (Serial.available() && (Serial.peek() == ' ' || Serial.peek() == '\t')) Serial.read();
                if (Serial.available() && (Serial.peek() == 'r' || Serial.peek() == 'R')) { Serial.read(); raw_mode = true; }
            } else {
                raw_file_mode = true;
                while (Serial.available() && Serial.peek() != '\n' && Serial.peek() != '\r') Serial.read();
            }
        }
        if (raw_file_mode) {
            /* Dump entire file as hex, split into short JSON lines
               (each line fits within 1s readline timeout at 115200 baud).
               Format:
                 {"ev":"hex_dump","id":0,"sz":30420}
                 {"ev":"raw","off":0,"hex":"A1B2..."}
                 {"ev":"raw","off":128,"hex":"C3D4..."}
                 ...
               Test script concatenates raw[].hex fields, decompresses. */
            Serial.print(",\"chunks\":");
            size_t file_total = sizeof(RunHeader) + data_sz + CRC32_TRAILER_SIZE;
            Serial.print((file_total + 127) / 128);  /* chunk count */
            Serial.println("}");

            uint8_t rbuf[128];
            for (uint32_t off = 0; off < file_total; off += sizeof(rbuf)) {
                size_t chunk = (file_total - off > sizeof(rbuf)) ? sizeof(rbuf) : (file_total - off);
                if (!g_fs.read_run_data((uint16_t)rid, off, rbuf, chunk)) break;
                json_begin();
                json_kv("ev","raw");
                Serial.print(','); json_kv("id",rid);
                Serial.print(','); json_kv("off",(long)off);
                Serial.print(",\"hex\":\"");
                for (size_t i = 0; i < chunk; i++) {
                    if (rbuf[i] < 16) Serial.print('0');
                    Serial.print(rbuf[i], HEX);
                }
                Serial.print('"');
                json_end();
            }
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
                if (!g_fs.read_run_data((uint16_t)rid, offset, buf + tail, to_read)) {
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
    case 'c':
        g_ldc.force_recalibrate();
        json_begin(); json_kv("ev", "ldc_recal"); json_end();
        return;
    case '!':
        json_begin(); json_kv("ev", "reboot"); json_end();
        Serial.flush();
        /* V2.93: Force LittleFS superblock commit, then DP to protect
           flash through reset. On Nicla dev board (no CS pull-up),
           NVIC_SystemReset still floats CS → possible corruption.
           On custom PCB with CS pull-up, this guarantees persistence. */
        g_fs.metadata_sync();
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
        g_fs.metadata_sync();
        g_flash.enter_deep_powerdown();
        delay(50);
        NVIC_SystemReset();
        return;
    case '?': {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        json_begin();
        json_kv("ev", "status");
        g_fs.metadata_sync();  /* flush pending directory commits */
        /* V4.03: Do NOT call scan_runs() here — it wipes the RAM cache
           that close_run() just populated. The RAM cache is authoritative
           during operation (close_run adds entries, delete_oldest_run
           removes them). scan_runs() is for boot-time initialization only. */
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

    /* ── Test mode: copy injected frame. In stream mode, pull one
       frame from PC via request-response (like polling BHY2).
       In manual mode (B/Q/L commands), use the static test frame. ── */
    if (test_mode_active()) {
        if (g_stream_active) {
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

        /* ── V4.16: Pull model — encode frame directly (no ring). ── */
        if (test_mode_active() && g_stream_active) {
            if (!g_ring_drained) {
                /* First LOGGING call: drain all ARM-pre-buffered ring frames */
                uint8_t count = g_ring.count();
                for (uint8_t i = 0; i < count; i++) {
                    RawFrame oldest = g_ring.read();
                    if (i == 0) {
                        /* Dump raw bytes of first ring frame */
                        uint8_t* raw = (uint8_t*)&oldest;
                        json_begin(); json_kv("ev","ring_dbg");
                        Serial.print(",\"bytes\":\"");
                        for (int b = 0; b < 16; b++) {
                            if (raw[b] < 16) Serial.print('0');
                            Serial.print(raw[b], HEX);
                        }
                        Serial.print('"');
                        json_end();
                    }
                    g_packer.encode(oldest, g_ring.last_read_ts());
                    uint8_t cf_size = g_packer.last_size();
                    uint8_t cf_type = g_packer.last_type();
                    if (i == 0) {
                        json_begin(); json_kv("ev","enc_dbg");
                        Serial.print(','); json_kv("src","ring");
                        Serial.print(','); json_kv("type", (long)cf_type);
                        Serial.print(','); json_kv("sz", (long)cf_size);
                        Serial.print(','); json_kv("qw", (long)oldest.q_w);
                        Serial.print(','); json_kv("baro", (long)oldest.baro_pa_div2);
                        json_end();
                    }
                    if (g_page_cursor + cf_size > PAGE_BUF_SIZE) {
                        flush_page_buffer();
                    }
                    memcpy(g_page_buf + g_page_cursor, g_packer.buffer(), cf_size);
                    g_page_cursor += cf_size;
                    g_frame_count++;
                }
                g_ring_drained = true;
            }

            /* Encode the pulled frame directly — no flash ring ops.
               Timestamp: use millis() directly (no ring to buffer). */
            g_packer.encode(f, millis());
            uint8_t cf_size = g_packer.last_size();
            uint8_t cf_type = g_packer.last_type();
            if (g_frame_count == 0) {
                json_begin(); json_kv("ev","enc_dbg");
                Serial.print(','); json_kv("src","direct");
                Serial.print(','); json_kv("type", (long)cf_type);
                Serial.print(','); json_kv("sz", (long)cf_size);
                Serial.print(','); json_kv("qw", (long)f.q_w);
                Serial.print(','); json_kv("baro", (long)f.baro_pa_div2);
                json_end();
            }
            if (g_page_cursor + cf_size > PAGE_BUF_SIZE) {
                flush_page_buffer();
            }
            memcpy(g_page_buf + g_page_cursor, g_packer.buffer(), cf_size);
            g_page_cursor += cf_size;
            g_frame_count++;

            /* End detector */
            float pa_raw = (float)f.baro_pa_div2 * 2.0f;  /* Pa/2→Pa */
            if (g_end_det.feed(pa_raw))
                g_sm.force_state(DeviceState::POST_RUN);
            return;
        }

        /* ── Real-sensor path: ring-based (original) ── */
        uint8_t count = g_ring.count();
        uint8_t pop_n = count >= 2 ? 2 : count;

        for (uint8_t i = 0; i < pop_n; i++) {
            RawFrame oldest = g_ring.read();
            g_packer.encode(oldest, g_ring.last_read_ts());
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
    g_ldc.force_recalibrate();  /* auto-calibrate baseline to current reading on every boot */

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

    /* fs_ok false means catastrophic failure (heap, flash dead).
       Halt — device cannot operate without storage. */
    if (!fs_ok) {
        while (1) { g_led.set_pattern(LedPattern::RED_FLASH_3); delay(1000); }
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

    /* ── Stream mode: pull model — feed_sensors() does
       request-response with PC.  Skip heavy ops (BHY2, BLE, LDC)
       for maximum loop rate.  Keep all state transitions. ── */
    if (g_stream_active) {
        handle_serial();  /* normal commands (non-blocking) */
        /* Skip g_led.update() during streaming — I2C LED writes slow
           the loop. Pattern is set on state transitions. */
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
                g_ring_drained = false;
                g_last_baro_ms = now;
            }
            if (cur == DeviceState::LOGGING) {
                BLE.stopAdvertise();
                g_end_det.reset();
                g_packer.reset();  /* V4.27: fresh packer at every run start */
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
                    /* V4.13: Pull model — no stray binary bytes. PC waits for
                       0x3F; firmware stops requesting → PC stops sending. */
                }
                flush_page_buffer();
                uint32_t compressed_sz = g_fs.run_bytes(); /* capture before close_run zeroes it */
                uint16_t run_id = g_fs.close_run(g_frame_count);
                sgc_ble_set_run_count(g_fs.run_count());
                sgc_ble_set_flash_used(g_fs.flash_used_pct());
                json_begin(); json_kv("ev", "run_saved");
                Serial.print(','); json_kv("id", (long)run_id);
                Serial.print(','); json_kv("fr", (long)g_frame_count);
                Serial.print(','); json_kv("sz", (long)compressed_sz);
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
        g_fs.metadata_sync();
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
            g_ring_drained = false;
        }
        if (cur == DeviceState::SLEEP) {
            /* Recalibrate LDC baseline on sleep entry — device is
               unattended, definitely no athlete nearby. */
            g_ldc.force_recalibrate();
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

            uint32_t compressed_sz = g_fs.run_bytes(); /* capture before close_run zeroes it */
            uint16_t run_id = g_fs.close_run(g_frame_count);

            sgc_ble_set_run_count(g_fs.run_count());
            sgc_ble_set_flash_used(g_fs.flash_used_pct());

            json_begin();
            json_kv("ev", "run_saved");
            Serial.print(','); json_kv("id", (long)run_id);
            Serial.print(','); json_kv("fr", (long)g_frame_count);
            Serial.print(','); json_kv("sz", (long)compressed_sz);
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
