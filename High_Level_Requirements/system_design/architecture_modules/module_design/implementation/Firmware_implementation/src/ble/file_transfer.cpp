/**
 * @file    file_transfer.cpp
 * @brief   BLE file transfer — V2 request-response protocol (phone pulls chunks).
 *
 * V5.14: Switched from device-push streaming to phone-pull request-response.
 *        Phone writes chunk offset to ABCA, device responds with one notification
 *        on ABCB. Phone controls the pace — zero buffer overflow risk.
 *
 * V4.93: 20 B chunks (min ATT payload) to avoid HCI ACL hang / truncate.
 * V4.95: 25 ms cadence + BLE.poll() after each notify; progress JSON;
 *        abort cleanly on disconnect; ft_done only after full stream.
 *        (Streaming cadence removed in V5.14 — phone controls pace now.)
 */

#include "file_transfer.h"
#include "../config.h"
#include "sgc_service.h"
#include "../storage/raw_run_store.h"
#include "../test_json.h"
#include <ArduinoBLE.h>
#include <Arduino.h>

/* V5.07: request_ble_radio_restart declared in sgc_service.h */

extern "C" {
    BLECharacteristic*              sgc_ble_ft_chunk_char();
    BLECharacteristic*              sgc_ble_ft_status_char();
    BLEUnsignedIntCharacteristic*   sgc_ble_ft_crc_char();
}

extern RawRunStore g_runs;

/* V2 protocol commands (phone → device via ABCA) */
enum FTCommand {
    FT_CMD_START = 0,
    FT_CMD_CHUNK = 1,
    FT_CMD_ABORT = 2,
};

enum FTState { FT_IDLE = 0, FT_READY = 1, FT_DONE = 2, FT_ERROR = 3 };

static uint8_t   g_ft_state   = FT_IDLE;
static uint32_t  g_ft_offset  = 0;
static uint32_t  g_ft_size    = 0;
static uint16_t  g_ft_run_id  = 0;
static uint32_t  g_ft_crc     = 0;
static uint32_t  g_ft_chunks  = 0;
static uint32_t  g_ft_start_ms = 0;
static uint32_t  g_ft_last_prog_ms = 0;  /* stall watchdog */

/* V5.13: use full negotiated MTU. Phone confirms MTU=247 (onConfigureMTU status=0),
   so ATT payload = 247 - 3 = 244 B per notification. */
static constexpr size_t FT_CHUNK_SIZE = 244;

void sgc_ble_transfer_init() {}
bool sgc_ble_ft_active() { return g_ft_state == FT_READY; }

void sgc_ble_ft_abort(const char* reason)
{
    if (g_ft_state != FT_READY) return;
    g_ft_state = FT_IDLE;
    json_begin();
    json_kv("ev", "ft_abort");
    Serial.print(','); json_kv("reason", reason ? reason : "?");
    Serial.print(','); json_kv("off", (long)g_ft_offset);
    Serial.print(','); json_kv("sz", (long)g_ft_size);
    Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
    json_end();
    /* Best-effort status notify if still linked. */
    if (BLE.connected()) {
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
        BLE.poll();
    }
}

void sgc_ble_transfer_poll()
{
    /* V5.14: request-response mode — chunks are sent in response to phone
       write events, not polled. Stall watchdog only. */
    if (g_ft_state != FT_READY) return;

    if (!BLE.connected()) {
        sgc_ble_ft_abort("disconnect");
        return;
    }

    uint32_t now = millis();

    /* V5.00: if phone stops sending chunk requests and we're stuck in
       FT_READY, abort so main loop can resume BHY2. */
    if (g_ft_last_prog_ms != 0 && (now - g_ft_last_prog_ms) > FT_STALL_TIMEOUT_MS) {
        /* V5.42: soft recovery instead of hard radio restart.
           Phone BLE buffer overflow at ~32 KB causes phone to stop
           requesting chunks. Abort FT + disconnect + re-advertise.
           If Cordio is truly stuck, zombie timeout escalates to
           radio restart (main loop desync heal). */
        uint32_t elapsed = now - g_ft_last_prog_ms;
        sgc_ble_ft_abort("stall");
        if (BLE.connected()) {
            BLE.disconnect();
            BLE.poll();
        }
        sgc_ble_force_recover("ft_stall");
        json_begin();
        json_kv("ev", "ft_stall_info");
        Serial.print(','); json_kv("elapsed", (long)elapsed);
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
        json_end();
        return;
    }
}

void sgc_ble_ft_on_request(const uint8_t* data, int len)
{
    if (len < 1) return;

    uint8_t cmd = data[0];

    /* ── CMD_START: [0, runId_lo, runId_hi] ─────────────────────── */
    if (cmd == FT_CMD_START) {
        if (len < 3) return;
        uint16_t run_id = (uint16_t)data[1] | ((uint16_t)data[2] << 8);

        sgc_ble_touch_activity();

        /* Abort any in-flight transfer cleanly. */
        if (g_ft_state == FT_READY) {
            g_ft_state = FT_IDLE;
            json_begin();
            json_kv("ev", "ft_abort");
            Serial.print(','); json_kv("reason", "new_request");
            json_end();
        }

        const RunEntry* entry = nullptr;
        for (uint16_t i = 0; i < g_runs.run_count(); i++) {
            const RunEntry* e = g_runs.get_entry(i);
            if (e && e->run_id == run_id) {
                entry = e;
                break;
            }
        }

        if (!entry) {
            sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
            BLE.poll();
            json_begin();
            json_kv("ev", "ft_error");
            Serial.print(','); json_kv("reason", "no_run");
            Serial.print(','); json_kv("run", (long)run_id);
            json_end();
            return;
        }

        RunHeader hdr;
        memset(&hdr, 0xFF, sizeof(hdr));
        bool read_ok = g_runs.read_run_header(run_id, hdr);
        if (!read_ok || hdr.format_ver < 1 || hdr.format_ver > 3) {
            json_begin();
            json_kv("ev", "ft_error");
            Serial.print(','); json_kv("reason", "bad_header");
            Serial.print(','); json_kv("run", (long)run_id);
            Serial.print(','); json_kv_bool("read_ok", read_ok);
            Serial.print(','); json_kv("format_ver", (long)hdr.format_ver);
            Serial.print(','); json_kv("data_size", (long)hdr.data_size);
            json_end();
            sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
            BLE.poll();
            return;
        }

        /* Prefer RAM index size (authoritative after close_run). */
        uint32_t data_sz = hdr.data_size;
        if (data_sz == 0)
            data_sz = entry->compressed_size;
        if (data_sz == 0 || data_sz > 200000) {
            json_begin();
            json_kv("ev", "ft_error");
            Serial.print(','); json_kv("reason", "bad_size");
            Serial.print(','); json_kv("sz", (long)data_sz);
            json_end();
            sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
            BLE.poll();
            return;
        }

        g_ft_run_id   = run_id;
        g_ft_size     = sizeof(RunHeader) + data_sz + CRC32_TRAILER_SIZE;
        g_ft_offset   = 0;
        g_ft_crc      = 0xFFFFFFFF;
        g_ft_chunks   = 0;
        g_ft_start_ms = millis();
        g_ft_last_prog_ms = g_ft_start_ms;
        g_ft_state    = FT_READY;
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_READY);
        BLE.poll();

        json_begin();
        json_kv("ev", "ft_start");
        Serial.print(','); json_kv("run", (long)run_id);
        Serial.print(','); json_kv("sz", (long)g_ft_size);
        Serial.print(','); json_kv("chunk", (long)FT_CHUNK_SIZE);
        json_end();
        return;
    }

    /* ── CMD_CHUNK: [1, off0, off1, off2, off3] (u32 LE) ─────────── */
    if (cmd == FT_CMD_CHUNK) {
        if (len < 5) return;
        if (g_ft_state != FT_READY) {
            json_begin();
            json_kv("ev", "ft_ign");
            Serial.print(','); json_kv("why", "not_ready");
            json_end();
            return;
        }

        uint32_t offset = (uint32_t)data[1] | ((uint32_t)data[2] << 8) |
                          ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
        sgc_ble_touch_activity();
        g_ft_last_prog_ms = millis();

        /* Calculate read length */
        size_t remaining = (offset < g_ft_size) ? (g_ft_size - offset) : 0;
        size_t read_len = (remaining > FT_CHUNK_SIZE) ? FT_CHUNK_SIZE : remaining;

        if (read_len == 0) {
            /* Offset at or past end — treat as done */
            uint32_t final_crc = RawRunStore::crc32_finalize(g_ft_crc);
            sgc_ble_ft_crc_char()->writeValue(final_crc);
            BLE.poll();
            sgc_ble_ft_status_char()->writeValue((uint8_t)FT_DONE);
            BLE.poll();
            json_begin();
            json_kv("ev", "ft_done");
            Serial.print(','); json_kv("crc", (long)final_crc);
            Serial.print(','); json_kv("sz", (long)g_ft_size);
            Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
            Serial.print(','); json_kv("ms", (long)(millis() - g_ft_start_ms));
            json_end();
            g_ft_state = FT_IDLE;
            return;
        }

        uint8_t buf[FT_CHUNK_SIZE];
        if (!g_runs.read_run_data(g_ft_run_id, offset, buf, read_len)) {
            g_ft_state = FT_ERROR;
            sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
            BLE.poll();
            json_begin();
            json_kv("ev", "ft_error");
            Serial.print(','); json_kv("reason", "read_fail");
            Serial.print(','); json_kv("off", (long)offset);
            json_end();
            return;
        }

        /* Update CRC with the bytes read */
        for (size_t i = 0; i < read_len; i++)
            g_ft_crc = RawRunStore::crc32_update(g_ft_crc, buf[i]);

        /* V5.12: check link before writeValue. If the phone disconnected,
           abort cleanly instead of blocking on a dead HCI buffer. */
        if (!BLE.connected()) {
            sgc_ble_ft_abort("link_gone");
            json_begin();
            json_kv("ev", "ft_error");
            Serial.print(','); json_kv("reason", "link_gone");
            json_end();
            return;
        }

        /* Notify chunk via ABCB */
        sgc_ble_ft_chunk_char()->writeValue(buf, read_len);
        BLE.poll();

        g_ft_chunks++;
        g_ft_offset = offset + read_len;

        /* Progress every 20 chunks */
        if ((g_ft_chunks % 20) == 0) {
            json_begin();
            json_kv("ev", "ft_prog");
            Serial.print(','); json_kv("off", (long)g_ft_offset);
            Serial.print(','); json_kv("sz", (long)g_ft_size);
            Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
            json_end();
        }

        /* Check if this was the last chunk */
        if ((uint32_t)offset + read_len >= g_ft_size) {
            uint32_t final_crc = RawRunStore::crc32_finalize(g_ft_crc);
            sgc_ble_ft_crc_char()->writeValue(final_crc);
            BLE.poll();
            sgc_ble_ft_status_char()->writeValue((uint8_t)FT_DONE);
            BLE.poll();
            json_begin();
            json_kv("ev", "ft_done");
            Serial.print(','); json_kv("crc", (long)final_crc);
            Serial.print(','); json_kv("sz", (long)g_ft_size);
            Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
            Serial.print(','); json_kv("ms", (long)(millis() - g_ft_start_ms));
            json_end();
            g_ft_state = FT_IDLE;
        }
        return;
    }

    /* ── CMD_ABORT: [2] ────────────────────────────────────────── */
    if (cmd == FT_CMD_ABORT) {
        if (g_ft_state == FT_READY) {
            g_ft_state = FT_IDLE;
            json_begin();
            json_kv("ev", "ft_abort");
            Serial.print(','); json_kv("reason", "phone");
            json_end();
        }
        return;
    }
}

const char* sgc_ble_build_run_list()
{
    static char json_buf[512];
    return g_runs.build_run_list(json_buf, sizeof(json_buf));
}
