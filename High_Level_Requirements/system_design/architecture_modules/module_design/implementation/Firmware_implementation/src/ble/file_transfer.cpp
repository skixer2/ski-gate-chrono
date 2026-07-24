/**
 * @file    file_transfer.cpp
 * @brief   BLE file transfer — LittleFSStorage API.
 */

#include "file_transfer.h"
#include "../storage/littlefs_storage.h"
#include "../test_json.h"
#include <ArduinoBLE.h>
#include <Arduino.h>

extern "C" {
    BLECharacteristic*              sgc_ble_ft_chunk_char();
    BLECharacteristic*              sgc_ble_ft_status_char();
    BLEUnsignedIntCharacteristic*   sgc_ble_ft_crc_char();
}

extern LittleFSStorage g_fs;

enum FTState { FT_IDLE = 0, FT_STREAMING = 1, FT_DONE = 2, FT_ERROR = 3 };

static uint8_t   g_ft_state   = FT_IDLE;
static uint32_t  g_ft_offset  = 0;
static uint32_t  g_ft_size    = 0;
static uint16_t  g_ft_run_id  = 0;
static uint32_t  g_ft_crc     = 0;

void sgc_ble_transfer_init() {}
void sgc_ble_transfer_poll()
{
    if (g_ft_state != FT_STREAMING) return;
    if (!BLE.connected()) return;

    static uint32_t last_chunk_ms = 0;
    uint32_t now = millis();
    if (now - last_chunk_ms < 30) return;  /* V4.10: relaxed from 20ms */
    last_chunk_ms = now;

    const size_t chunk_size = 128;  /* V4.10: reduced for BLE reliability */
    uint8_t buf[chunk_size];
    size_t remaining = (g_ft_offset < g_ft_size) ? (g_ft_size - g_ft_offset) : 0;
    size_t send_len = (remaining > chunk_size) ? chunk_size : remaining;

    if (send_len == 0) {
        g_ft_state = FT_DONE;
        uint32_t final_crc = LittleFSStorage::crc32_finalize(g_ft_crc);
        sgc_ble_ft_crc_char()->writeValue(final_crc);
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_DONE);
        json_begin();
        json_kv("ev", "ft_done");
        Serial.print(','); json_kv("crc", (long)final_crc);
        json_end();
        return;
    }

    if (!g_fs.read_run_data(g_ft_run_id, g_ft_offset, buf, send_len)) {
        g_ft_state = FT_ERROR;
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
        return;
    }

    for (size_t i = 0; i < send_len; i++)
        g_ft_crc = LittleFSStorage::crc32_update(g_ft_crc, buf[i]);
    sgc_ble_ft_chunk_char()->writeValue(buf, send_len);
    g_ft_offset += send_len;
}

void sgc_ble_ft_on_request(uint16_t run_id)
{
    json_begin();
    json_kv("ev", "ft_request");
    Serial.print(','); json_kv("run", (long)run_id);
    json_end();

    const RunEntry* entry = nullptr;
    for (uint16_t i = 0; i < g_fs.run_count(); i++) {
        const RunEntry* e = g_fs.get_entry(i);
        if (e && e->run_id == run_id) {
            entry = e;
            break;
        }
    }

    if (!entry) {
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
        return;
    }

    RunHeader hdr;
    memset(&hdr, 0xFF, sizeof(hdr));
    bool read_ok = g_fs.read_run_header(run_id, hdr);
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
        return;
    }

    g_ft_run_id = run_id;
    g_ft_size   = sizeof(RunHeader) + hdr.data_size + CRC32_TRAILER_SIZE;
    g_ft_offset = 0;
    g_ft_crc    = 0xFFFFFFFF;
    g_ft_state  = FT_STREAMING;
    sgc_ble_ft_status_char()->writeValue((uint8_t)FT_STREAMING);

    json_begin();
    json_kv("ev", "ft_start");
    Serial.print(','); json_kv("run", (long)run_id);
    Serial.print(','); json_kv("sz", (long)g_ft_size);
    json_end();
}

const char* sgc_ble_build_run_list()
{
    static char json_buf[512];
    return g_fs.build_run_list(json_buf, sizeof(json_buf));
}
