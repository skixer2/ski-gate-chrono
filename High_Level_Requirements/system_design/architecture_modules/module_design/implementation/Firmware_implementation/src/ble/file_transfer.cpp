/**
 * @file    file_transfer.cpp
 * @brief   BLE file transfer — Phase 10 (FlashManager API).
 */

#include "file_transfer.h"
#include "../storage/spi_flash.h"
#include "../storage/flash_manager.h"
#include "../test_json.h"
#include <ArduinoBLE.h>
#include <Arduino.h>

extern "C" {
    BLECharacteristic*              sgc_ble_ft_chunk_char();
    BLECharacteristic*              sgc_ble_ft_status_char();
    BLEUnsignedIntCharacteristic*   sgc_ble_ft_crc_char();
}

extern FlashManager g_fm;

enum FTState { FT_IDLE = 0, FT_STREAMING = 1, FT_DONE = 2, FT_ERROR = 3 };

static uint8_t   g_ft_state = FT_IDLE;
static uint32_t  g_ft_offset = 0;
static uint32_t  g_ft_size   = 0;
static uint32_t  g_ft_run_start = 0;
static uint32_t  g_ft_crc    = 0;

void sgc_ble_transfer_init() {}
void sgc_ble_transfer_poll()
{
    if (g_ft_state != FT_STREAMING) return;
    if (!BLE.connected()) return;

    static uint32_t last_chunk_ms = 0;
    uint32_t now = millis();
    if (now - last_chunk_ms < 20) return;
    last_chunk_ms = now;

    const size_t chunk_size = 244;
    uint8_t buf[chunk_size];
    size_t remaining = (g_ft_offset < g_ft_size) ? (g_ft_size - g_ft_offset) : 0;
    size_t send_len = (remaining > chunk_size) ? chunk_size : remaining;

    if (send_len == 0) {
        g_ft_state = FT_DONE;
        sgc_ble_ft_crc_char()->writeValue(g_ft_crc);
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_DONE);
        json_begin();
        json_kv("ev", "ft_done");
        Serial.print(','); json_kv("crc", (long)g_ft_crc);
        json_end();
        return;
    }

    g_fm.read_data(g_ft_run_start + g_ft_offset, buf, send_len);
    for (size_t i = 0; i < send_len; i++)
        g_ft_crc = FlashManager::crc32_update(g_ft_crc, buf[i]);
    sgc_ble_ft_chunk_char()->writeValue(buf, send_len);
    g_ft_offset += send_len;
}

void sgc_ble_ft_on_request(uint16_t run_id)
{
    json_begin();
    json_kv("ev", "ft_request");
    Serial.print(','); json_kv("run", (long)run_id);
    json_end();

    /* Look up the run by ID in FlashManager's entry table */
    const RunEntry* entry = nullptr;
    for (uint16_t i = 0; i < g_fm.run_count(); i++) {
        const RunEntry* e = g_fm.get_entry(i);
        if (e && e->run_id == run_id) {
            entry = e;
            break;
        }
    }

    if (!entry) {
        json_begin();
        json_kv("ev", "ft_error");
        Serial.print(','); json_kv("reason", "not_found");
        json_end();
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
        return;
    }

    /* Read the RunHeader to get actual data_size */
    RunHeader hdr;
    if (!g_fm.read_run_header(entry->page_start, hdr) ||
        hdr.format_ver < 1 || hdr.format_ver > 3) {
        json_begin();
        json_kv("ev", "ft_error");
        Serial.print(','); json_kv("reason", "bad_header");
        json_end();
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_ERROR);
        return;
    }

    g_ft_run_start = entry->page_start;
    g_ft_size = sizeof(RunHeader) + hdr.data_size + CRC32_TRAILER_SIZE;
    g_ft_offset = 0;
    g_ft_crc = 0xFFFFFFFF;
    g_ft_state = FT_STREAMING;
    sgc_ble_ft_status_char()->writeValue((uint8_t)FT_STREAMING);

    json_begin();
    json_kv("ev", "ft_start");
    Serial.print(','); json_kv("run", (long)run_id);
    Serial.print(','); json_kv("sz", (long)g_ft_size);
    json_end();
}

/* ── Run list (uses FlashManager) ─────────────────────────────── */

const char* sgc_ble_build_run_list()
{
    static char json_buf[512];
    return g_fm.build_run_list(json_buf, sizeof(json_buf));
}
