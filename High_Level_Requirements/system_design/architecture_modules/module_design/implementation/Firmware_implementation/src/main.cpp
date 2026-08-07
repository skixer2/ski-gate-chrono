/**
 * Opt-A production storage — RawRunStore (v4.63+), not LittleFS payloads.
 *
 * JSON-lines is the only serial output format (ADR-001, AD-009).
 * Test commands always compiled in, test mode starts OFF.
 *
 * Flash layout (MX25R 2 MB, v4.73):
 *   0x0000–0x8FFF     FlashRing 3×500 pre-roll (live MAX_COUNT=1000 ≈10 s)
 *   0x9000–0x1FBFFF   8 × raw run slots (RawRunStore)
 *   0x1FC000          Config (BLE name etc.)
 *   0x1FD000          Run index (RRS1)
 *   0x1FE000–0x1FFFFF reserved
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
#include "test_json.h"
#include "led/led.h"
#include "sensors/ldc1612.h"
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

/* ── Current frame (V4.41: file-scope so start detector reads same data as ring) ── */
static RawFrame g_cur_frame;

bool g_stream_active = false;  /* 'S' command — pull model frame ingestion */
uint32_t g_stream_frames = 0;   /* frames received in stream mode (total pull) */
static uint32_t g_stream_frames_pre_log = 0; /* pulls while ARMED before LOGGING */
/* Force-LOGGING via serial 'l' (bench/S04): skip end detector so a stationary
   device is not auto-closed after ~5s flat pressure. Production path enters
   LOGGING via start detector with this flag false. Cleared on POST_RUN. */
static bool g_force_logging = false;
extern bool g_manual_frame;      /* from test_mode.cpp: set by B/Q/L, suppress ARM→stream */


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
        g_led.set_pattern(LedPattern::OFF); break;
    case DeviceState::IDLE:
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
   Returns length written (0 if timeout / empty). */
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
    return n;
}

void handle_serial()
{
    if (!Serial.available()) return;
    char c = Serial.read();

    /* V4.13: Pull model — no binary parser needed. Frames are pulled
       by feed_sensors() → test_request_frame() via request-response.
       In stream mode, handle_serial() skips the binary path entirely. */

    if (test_mode_handle_serial(c)) return;

    /* V4.45: for 'h', buffer the full line first so arg parsing does not
       race USB CDC delivery (was: parseInt saw empty → no dump). */
    char h_args[40];
    h_args[0] = '\0';
    if (c == 'h') {
        read_rest_of_line(h_args, sizeof(h_args), 50);
    }

    switch (c) {
    case 'i': g_sm.force_state(DeviceState::IDLE); break;
    case 'a':
        if (g_sm.state() == DeviceState::IDLE) {
            /* Minimal test fork: enable serial frame source. Everything
               else (start detector, ring, LOGGING) is the real path. */
            if (test_mode_active() && !g_manual_frame) {
                test_stream_reset();
                g_stream_active = true;
            } else if (!test_mode_active()) {
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
            g_sm.force_state(DeviceState::ARMED);  /* handler resets start_det + g_last_baro_ms */
        }
        break;
    case 'l':
        /* Bench force-LOGGING: skip ring drain + skip end det (S04 rate). */
        g_force_logging = true;
        g_end_det.reset();
        g_sm.force_state(DeviceState::LOGGING);
        break;
    case 'L':
        /* Natural LOGGING path for bench (S06): drain FlashRing then live.
           Does NOT set g_force_logging — end det still active (use 'p' on desk).
           Only reaches here when tm=0 (test_mode 'L'=set_la when tm=1). */
        g_force_logging = false;
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
    case 's': g_sm.force_state(DeviceState::SLEEP); break;
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
        Serial.print(','); json_kv_bool("qi", !digitalRead(10));
        Serial.print(','); json_kv("runs", (long)g_runs.run_count());
        Serial.print(','); json_kv("total_runs", (long)g_runs.total_run_count());
        Serial.print(','); json_kv("oldest_age", (long)g_runs.oldest_run_age());
        Serial.print(','); json_kv_bool("ldc", g_ldc.is_connected());
        Serial.print(','); json_kv("ldc_raw", (long)g_ldc.data());
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
/* ── Synchronous state transition handler (V4.41).
   Called immediately by StateMachine::enter_state() — no lazy check,
   no one-iteration race between force_state and feed_sensors. ── */
static void on_state_transition(DeviceState from, DeviceState to)
{
    uint32_t now = millis();

    json_state_evt(g_sm.state_name_for(from), g_sm.state_name());

    if (to == DeviceState::ARMED) {
        g_packer.reset();
        g_page_cursor = 0;
        g_run_created = false;
        g_last_baro_ms = now;
        g_ring.clear();  /* soft clear — no 6-sector erase */
        /* Pre-erase is done in POST_RUN (10 s cooldown) or boot — not here.
           ARM must stay fast (S04 arm window, athlete at gate). */
        /* P₀ auto-inits from first valid frame written to the ring. */
        g_start_det.reset(0.0f);
    }
    if (to == DeviceState::SLEEP) {
        if (!g_stream_active) g_ldc.force_recalibrate();
    }
    if (to == DeviceState::LOGGING) {
        BLE.stopAdvertise();
        g_end_det.reset();
        g_packer.reset();
        g_page_cursor = 0;
        g_frame_count = 0;
        int16_t baro_temp = (int16_t)(temperature.value() * 10.0f);
        uint8_t cal = 0;

        if (g_force_logging) {
            /* S04: no pre-roll needed; slot may already be prepared at ARM. */
            g_ring.clear();
        }

        /* Both production and force-l use raw store (Opt A).
           create_run() uses slot prepared in POST_RUN; if none (first boot
           / S04 -R), it prepares now (full or partial erase). */
        g_run_created = g_runs.create_run(0, baro_temp, cal);
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
        BLE.advertise();
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
        }
        flush_page_buffer();

        uint32_t compressed_sz = g_runs.run_bytes();
        uint16_t run_id = g_runs.close_run(g_frame_count);
        bool ok = (run_id != 0xFFFF) && (g_runs.write_err() == 0);
        sgc_ble_set_run_count(g_runs.run_count());
        sgc_ble_set_flash_used(g_runs.flash_used_pct());

        g_force_logging = false;

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
        test_stream_reset();

        /* Opt A (JP): pre-erase NEXT run during POST_RUN cooldown (10 s).
           Athlete is stopped; 1 s of sector erase is free here and keeps
           ARM + LOGGING free of bulk erase. */
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
        /* Flash pre-roll (MX25R). SPI cost is OK while waiting at the gate;
           LOGGING path avoids ring write (see below). */
        bool was_full = g_ring.is_full();
        g_ring.write(f);
        if (!was_full && g_ring.is_full()) {
            json_begin();
            json_kv("ev", "ring_full");
            Serial.print(','); json_kv("r", (long)g_ring.max_count());
            json_end();
        }
        return;
    }

    if (st == DeviceState::LOGGING) {
        /* V4.61 rate path:
           - force-'l' (S04): NEVER touch FlashRing — encode live only.
             Measures pure BHY2→packer→LFS without SPI ring hop.
           - natural start-det: drain ARMED pre-roll with READ-only pops
             (no ring.write of live samples while draining — that was
             SPI program+possible erase every sample and capped ~42 Hz).
             After ring empty, encode live direct. */
        if (g_force_logging) {
            encode_to_storage(f, millis());
        } else if (!g_ring.is_empty()) {
            /* Drain only — do not push live into flash ring mid-LOGGING.
               pop min(2, count): original 2× design (was briefly 4). */
            uint8_t pop_n = g_ring.count() >= 2 ? 2 : (uint8_t)g_ring.count();
            for (uint8_t i = 0; i < pop_n; i++) {
                RawFrame oldest = g_ring.read();
                encode_to_storage(oldest, g_ring.last_read_ts());
            }
            /* Live sample of this tick is not stored while draining.
               Pre-roll already covers the start window; after empty we
               take live every tick. Accept tiny gap at transition. */
            (void)f;
        } else {
            encode_to_storage(f, millis());
        }

        /* End detector on live sample — skipped for force-'l' bench runs
           (S04 BHY2 rate) so stationary pressure does not end at ~5s. */
        if (!g_force_logging) {
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

    /* ── Flash ring buffer (full erase at boot only) ── */
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
        rotation.begin(100.0f, 0);     /* RV @ 100 Hz */
        lin_acc.begin(100.0f, 0);     /* linear accel @ 100 Hz */
        pressure.begin(100.0f, 0);    /* baro virtual @ 100 Hz */
        temperature.begin(1.0f, 0);   /* header only — not per-frame */
    }

    test_mode_init();
    bhy2_cal_hook_init();

    g_sm.on_transition(on_state_transition);  /* V4.41: synchronous handlers */
    g_sm.force_state(DeviceState::IDLE);
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
}

/* ================================================================== */
void loop()
{
    uint32_t now = millis();

    static bool     g_factory_confirming    = false;
    static uint32_t g_factory_confirm_start = 0;
    static constexpr uint32_t FACTORY_CONFIRM_MS = 3000;
    static constexpr uint32_t FACTORY_LED_BLINK_MS = 250;

    /* ── Path split by mode/state for max LOGGING sample rate ──
       Stream: skip BHY2/BLE/LDC (USB pull path).
       LOGGING: BHY2 + LED + serial only — advertise already off,
                LDC arming irrelevant mid-run, BLE transfer idle.
       Else: full peripheral service. */
    DeviceState loop_st = g_sm.state();
    if (g_stream_active) {
        handle_serial();
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
        BHY2.update(); sgc_ble_poll(); sgc_ble_transfer_poll();
        g_led.update(); g_ldc.tick();
        handle_serial();
    }

    /* ── Unified state machine (runs regardless of stream mode) ── */
    g_sm.tick();

    /* ── LDC1612 wake/arm/factory — real-world, non-stream, non-LOGGING ── */
    if (!g_stream_active && loop_st != DeviceState::LOGGING) {
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
                g_sm.force_state(DeviceState::ARMED);  /* handler: start_det auto-init */
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
            g_runs.erase_all();
            g_runs.metadata_sync();
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
    }

    /* ═══════════════════════════════════════════════════════════════
       Feed sensors → Start detector.
       V4.41: transitions are synchronous (on_state_transition callback),
       no lazy g_prev_state check. Start detector uses g_cur_frame from
       feed_sensors — same data that goes into the ring. No fork. 
       ═══════════════════════════════════════════════════════════════ */

    /* ── Feed sensors (10 ms) — sets g_cur_frame from serial or BHY2.
       Advance by +10 from previous tick (not wall 'now') so a late loop
       does not permanently phase-shift the 100 Hz grid. Cap catch-up at
       1 frame/loop to avoid a flash-stall burst. ── */
    if (now - g_last_sensor_ms >= 10) {
        feed_sensors();
        g_last_sensor_ms += 10;
        if ((int32_t)(now - g_last_sensor_ms) > 50)
            g_last_sensor_ms = now;  /* resync after long stall */
    }

    /* ── Start detector (100 ms) — reads g_cur_frame.baro_pa_div2,
       the same data that feed_sensors just wrote to the ring.
       Transition handler runs synchronously inside force_state(). ── */
    if (now - g_last_baro_ms >= 100 && g_sm.state() == DeviceState::ARMED) {
        /* Same data just written to the ring by feed_sensors().
           Skip invalid/zero frames so default/uninitialized data
           cannot poison P₀ before the first real sample arrives. */
        if (g_cur_frame.baro_pa_div2 > 0) {
            float pa = (float)g_cur_frame.baro_pa_div2 * 2.0f;  /* Pa/2→Pa */
            if (g_start_det.feed(pa))
                g_sm.force_state(DeviceState::LOGGING);
        }
        g_last_baro_ms = now;
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
