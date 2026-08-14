/**
 * @file    file_transfer.cpp
 * @brief   BLE file transfer — RawRunStore API (Opt A, v4.63).
 *
 * V4.93: 20 B chunks (min ATT payload) to avoid HCI ACL hang / truncate.
 * V4.95: 25 ms cadence + BLE.poll() after each notify; progress JSON;
 *        abort cleanly on disconnect; ft_done only after full stream.
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

enum FTState { FT_IDLE = 0, FT_STREAMING = 1, FT_DONE = 2, FT_ERROR = 3 };

static uint8_t   g_ft_state   = FT_IDLE;
static uint32_t  g_ft_offset  = 0;
static uint32_t  g_ft_size    = 0;
static uint16_t  g_ft_run_id  = 0;
static uint32_t  g_ft_crc     = 0;
static uint32_t  g_ft_chunks  = 0;
static uint32_t  g_ft_start_ms = 0;
static uint32_t  g_ft_last_prog_ms = 0;  /* stall watchdog */

/* Single ATT notification payload at min MTU 23: opcode+handle leave 20 B.
   Keep 20 until we have a reliable negotiated-MTU path (ATT max often stays 23
   on this Cordio build even if the phone requests 247). */
static constexpr size_t   FT_CHUNK_SIZE = 20;
static constexpr uint32_t FT_CHUNK_MS   = 25;   /* was 20 — extra headroom for ACL */
static constexpr uint32_t FT_PROG_EVERY = 50;   /* serial progress every N chunks */

void sgc_ble_transfer_init() {}
bool sgc_ble_ft_active() { return g_ft_state == FT_STREAMING; }

void sgc_ble_ft_abort(const char* reason)
{
    if (g_ft_state != FT_STREAMING) return;
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
    if (g_ft_state != FT_STREAMING) return;

    if (!BLE.connected()) {
        sgc_ble_ft_abort("disconnect");
        return;
    }

    static uint32_t last_chunk_ms = 0;
    uint32_t now = millis();

    /* V5.00: if writeValue/HCI wedges and we stop advancing, abort so main
       loop can resume BHY2 and advertise path after disconnect recovery. */
    if (g_ft_last_prog_ms != 0 && (now - g_ft_last_prog_ms) > FT_STALL_TIMEOUT_MS) {
        sgc_ble_ft_abort("stall");
        if (BLE.connected()) {
            BLE.disconnect();
            BLE.poll();
        }
        request_ble_radio_restart("ft_stall");  // V5.07: radio restart after FT stall
        return;
    }

    if (now - last_chunk_ms < FT_CHUNK_MS) return;
    last_chunk_ms = now;

    uint8_t buf[FT_CHUNK_SIZE];
    size_t remaining = (g_ft_offset < g_ft_size) ? (g_ft_size - g_ft_offset) : 0;
    size_t send_len = (remaining > FT_CHUNK_SIZE) ? FT_CHUNK_SIZE : remaining;

    if (send_len == 0) {
        g_ft_state = FT_DONE;
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
        return;
    }

    if (!g_runs.read_run_data(g_ft_run_id, g_ft_offset, buf, send_len)) {
        g_ft_state = FT_ERROR;
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
        BLE.poll();
        json_begin();
        json_kv("ev", "ft_error");
        Serial.print(','); json_kv("reason", "read_fail");
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        json_end();
        return;
    }

    for (size_t i = 0; i < send_len; i++)
        g_ft_crc = RawRunStore::crc32_update(g_ft_crc, buf[i]);

    sgc_ble_ft_chunk_char()->writeValue(buf, send_len);
    /* Drain HCI after each notify so sendAclPkt flow-control cannot spin
       the next writeValue with a full pending queue. */
    BLE.poll();

    g_ft_offset += send_len;
    g_ft_chunks++;
    g_ft_last_prog_ms = now;
    sgc_ble_touch_activity();  // V5.07: chunk sent = phone is active

    if ((g_ft_chunks % FT_PROG_EVERY) == 0 || g_ft_offset >= g_ft_size) {
        json_begin();
        json_kv("ev", "ft_prog");
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        Serial.print(','); json_kv("sz", (long)g_ft_size);
        Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
        json_end();
    }
}

void sgc_ble_ft_on_request(uint16_t run_id)
{
    sgc_ble_touch_activity();  // V5.07: request counts as BLE activity
    json_begin();
    json_kv("ev", "ft_request");
    Serial.print(','); json_kv("run", (long)run_id);
    json_end();

    /* Abort any in-flight transfer cleanly. */
    if (g_ft_state == FT_STREAMING) {
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
    g_ft_state    = FT_STREAMING;
    sgc_ble_ft_status_char()->writeValue((uint8_t)FT_STREAMING);
    BLE.poll();

    json_begin();
    json_kv("ev", "ft_start");
    Serial.print(','); json_kv("run", (long)run_id);
    Serial.print(','); json_kv("sz", (long)g_ft_size);
    Serial.print(','); json_kv("chunk", (long)FT_CHUNK_SIZE);
    Serial.print(','); json_kv("cad_ms", (long)FT_CHUNK_MS);
    json_end();
}

const char* sgc_ble_build_run_list()
{
    static char json_buf[512];
    return g_runs.build_run_list(json_buf, sizeof(json_buf));
}
