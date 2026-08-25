/**
 * @file    file_transfer.cpp
 * @brief   BLE file transfer — device-push streaming with 244 B chunks.
 *
 * V5.45: Revert to device-push (V4.97 style) but with 244 B chunks (V5.13).
 *        Phone-pull (V5.14) added per-chunk GATT round-trip overhead and
 *        caused S22 LINK_SUPERVISION_TIMEOUT from cumulative ACL pressure.
 *        Device-push lets the device control the pace — phone just listens.
 *
 *        SPI isolation from V4.96/4.97 stays: while FT active, skip
 *        BHY2.update(), feed_sensors(), and ambient pressure.
 *
 *        244 B chunks @ 25ms cadence = ~9.5 KB/s theoretical, ~5-8 KB/s real.
 *        39 KB run in ~5-8s.
 */

#include "file_transfer.h"
#include "../config.h"
#include "sgc_service.h"
#include "../storage/raw_run_store.h"
#include "../test_json.h"
#include <ArduinoBLE.h>
#include <Arduino.h>
#include "nrf.h"  /* NRF_WDT for feed in FT poll */

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
static uint32_t  g_ft_last_chunk_ms = 0;

/* 244 B chunks (MTU 247 - 3 ATT header). Phone confirms MTU=247. */
static constexpr size_t   FT_CHUNK_SIZE = 244;
/* 50ms cadence — S22 can't sustain 30ms (8 KB/s overflows ACL buffer →
   LINK_SUPERVISION_TIMEOUT → device writeValue blocks → watchdog reboot).
   50ms = ~5 KB/s, still 2.5x faster than V4.79 (128 B @ 30ms = 4 KB/s). */
static constexpr uint32_t FT_CHUNK_MS   = 50;
static constexpr uint32_t FT_PROG_EVERY = 20;

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

    uint32_t now = millis();
    if (now - g_ft_last_chunk_ms < FT_CHUNK_MS) return;
    g_ft_last_chunk_ms = now;

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

    /* V5.46: Feed WDT before writeValue — if the BLE link died and
       writeValue blocks on a full HCI buffer, WDT would reboot the
       device. Feeding here gives the stall watchdog time to detect
       the dead link and abort cleanly. */
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    sgc_ble_ft_chunk_char()->writeValue(buf, send_len);
    BLE.poll();

    g_ft_offset += send_len;
    g_ft_chunks++;

    if ((g_ft_chunks % FT_PROG_EVERY) == 0 || g_ft_offset >= g_ft_size) {
        json_begin();
        json_kv("ev", "ft_prog");
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        Serial.print(','); json_kv("sz", (long)g_ft_size);
        Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
        Serial.print(','); json_kv("ms", (long)(millis() - g_ft_start_ms));
        json_end();
    }
}

/* V5.45: simplified request handler — phone sends CMD_START only.
   No CMD_CHUNK, no CMD_ABORT — device-push, phone just listens.
   Keep CMD_ABORT for clean cancel. */
void sgc_ble_ft_on_request(const uint8_t* data, int len)
{
    if (len < 1) return;
    uint8_t cmd = data[0];

    if (cmd == 0) {  /* CMD_START: [0, runId_lo, runId_hi] */
        if (len < 3) return;
        uint16_t run_id = (uint16_t)data[1] | ((uint16_t)data[2] << 8);

        sgc_ble_touch_activity();

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
            if (e && e->run_id == run_id) { entry = e; break; }
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

        uint32_t data_sz = hdr.data_size;
        if (data_sz == 0) data_sz = entry->compressed_size;
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
        g_ft_last_chunk_ms = 0;
        g_ft_state    = FT_STREAMING;
        sgc_ble_ft_status_char()->writeValue((uint8_t)FT_STREAMING);
        BLE.poll();

        json_begin();
        json_kv("ev", "ft_start");
        Serial.print(','); json_kv("run", (long)run_id);
        Serial.print(','); json_kv("sz", (long)g_ft_size);
        Serial.print(','); json_kv("chunk", (long)FT_CHUNK_SIZE);
        Serial.print(','); json_kv("cad_ms", (long)FT_CHUNK_MS);
        Serial.print(','); json_kv("ms", (long)0);
        json_end();
        return;
    }

    if (cmd == 2) {  /* CMD_ABORT */
        if (g_ft_state == FT_STREAMING) {
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
