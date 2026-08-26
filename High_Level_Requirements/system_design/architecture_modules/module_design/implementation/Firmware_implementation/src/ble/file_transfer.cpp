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
#include "../state_machine/state_machine.h"

extern "C" {
    BLECharacteristic* sgc_ble_ft_request_char();
    BLECharacteristic* sgc_ble_ft_stream_char();
}

extern StateMachine g_sm;
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

/* 20 B chunks for Safe-Mode debugging. If this works, the 244 B payloads were triggering memory corruption or stack overflow. */
static constexpr size_t   FT_CHUNK_SIZE = 20;
/* Increased cadence to 200ms to ensure zero buffer pressure. */
static constexpr uint32_t FT_CHUNK_MS   = 200;
static constexpr uint32_t FT_PROG_EVERY = 10;

// V5.52: Move buffer to static memory to eliminate stack overflow risk
static uint8_t g_ft_buffer[FT_CHUNK_SIZE];

void sgc_ble_transfer_init() {}
bool sgc_ble_ft_active() { return g_ft_state == FT_STREAMING; }

void sgc_ble_ft_handle_ack()
{
    /* V5.57: ACKs are now informational. They no longer trigger state changes.
       We maintain a continuous paced-push stream. */
}

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
        sgc_ble_ft_stream_char()->writeValue((uint8_t)FT_ERROR);
        BLE.poll();
    }
}

void sgc_ble_transfer_poll()
{
    if (g_ft_state == FT_IDLE || g_ft_state == FT_DONE || g_ft_state == FT_ERROR) return;

    if (!BLE.connected()) {
        sgc_ble_ft_abort("disconnect");
        return;
    }

    uint32_t now = millis();

    if (now - g_ft_last_chunk_ms < FT_CHUNK_MS) return;
    g_ft_last_chunk_ms = now;

    uint8_t* buf = g_ft_buffer;
    size_t remaining = (g_ft_offset < g_ft_size) ? (g_ft_size - g_ft_offset) : 0;
    // Reserve 2 bytes for Type and Index
    size_t send_len = (remaining > (FT_CHUNK_SIZE - 2)) ? (FT_CHUNK_SIZE - 2) : remaining;

    if (send_len == 0) {
        g_ft_state = FT_DONE;
        uint32_t final_crc = RawRunStore::crc32_finalize(g_ft_crc);
        
        // Packet Type 0x03: CRC/Finalize
        uint8_t crc_pkt[5];
        crc_pkt[0] = 0x03;
        crc_pkt[1] = (uint8_t)(final_crc & 0xFF);
        crc_pkt[2] = (uint8_t)((final_crc >> 8) & 0xFF);
        crc_pkt[3] = (uint8_t)((final_crc >> 16) & 0xFF);
        crc_pkt[4] = (uint8_t)((final_crc >> 24) & 0xFF);
        
        Serial.println("SND_CRC");
        sgc_ble_ft_stream_char()->writeValue(crc_pkt, 5);
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

    if (!g_runs.read_run_data(g_ft_run_id, g_ft_offset, buf + 2, send_len)) {
        g_ft_state = FT_ERROR;
        uint8_t err_pkt[2] = {0x04, 0x01}; // Type 0x04, Error 0x01 (Read Fail)
        sgc_ble_ft_stream_char()->writeValue(err_pkt, 2);
        BLE.poll();
        json_begin();
        json_kv("ev", "ft_error");
        Serial.print(','); json_kv("reason", "read_fail");
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        json_end();
        return;
    }

    for (size_t i = 0; i < send_len; i++)
        g_ft_crc = RawRunStore::crc32_update(g_ft_crc, buf[2 + i]);

    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    // Packet Type 0x02: Data Chunk
    buf[0] = 0x02; 
    buf[1] = (uint8_t)(g_ft_chunks & 0xFF);
    
    Serial.print("SND_DATA "); Serial.println(g_ft_chunks);
    sgc_ble_ft_stream_char()->writeValue(buf, send_len + 2);
    
    /* V5.57: Safe-Stream Pacing
       Small delay to prevent overloading the nRF52 BLE stack.
       The 40ms connection interval handles the rest. */
    delay(10); 
    BLE.poll();

    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    g_ft_offset += send_len;
    g_ft_chunks++;

    /* V5.57: Removed state = FT_WAITING_ACK. 
       We now stay in FT_STREAMING for Paced-Push. */

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

        // Settle Gap: Give S22 time to clear internal GATT lock after the write.
        delay(100); 

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
        // V5.54: Error packets now go through the Stream characteristic
        uint8_t err_pkt[2] = {0x04, 0x01}; // Type 0x04, Error 0x01 (No Run)
        sgc_ble_ft_stream_char()->writeValue(err_pkt, 2);
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
        // V5.54: Error packets now go through the Stream characteristic
        uint8_t err_pkt[2] = {0x04, 0x02}; // Type 0x04, Error 0x02 (Bad Header)
        sgc_ble_ft_stream_char()->writeValue(err_pkt, 2);
        BLE.poll();
        json_begin();
        json_kv("ev", "ft_error");
        Serial.print(','); json_kv("reason", "bad_header");
        Serial.print(','); json_kv("run", (long)run_id);
        Serial.print(','); json_kv_bool("read_ok", read_ok);
        Serial.print(','); json_kv("format_ver", (long)hdr.format_ver);
        Serial.print(','); json_kv("data_size", (long)hdr.data_size);
        json_end();
        return;
    }

    uint32_t data_sz = hdr.data_size;
    if (data_sz == 0) data_sz = entry->compressed_size;
    if (data_sz == 0 || data_sz > 200000) {
        // V5.54: Error packets now go through the Stream characteristic
        uint8_t err_pkt[2] = {0x04, 0x03}; // Type 0x04, Error 0x03 (Bad Size)
        sgc_ble_ft_stream_char()->writeValue(err_pkt, 2);
        BLE.poll();
        json_begin();
        json_kv("ev", "ft_error");
        Serial.print(','); json_kv("reason", "bad_size");
        Serial.print(','); json_kv("sz", (long)data_sz);
        json_end();
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
    
    // Sync global state machine to prevent SLEEP timers from interfering
    g_sm.force_state(DeviceState::LOGGING);

    // V5.54: Send Start/Metadata packet [0x01, runId_lo, runId_hi, size...]
    uint8_t start_pkt[10];
    start_pkt[0] = 0x01;
    start_pkt[1] = (uint8_t)(run_id & 0xFF);
    start_pkt[2] = (uint8_t)((run_id >> 8) & 0xFF);
    start_pkt[3] = (uint8_t)(g_ft_size & 0xFF);
    start_pkt[4] = (uint8_t)((g_ft_size >> 8) & 0xFF);
    start_pkt[5] = (uint8_t)((g_ft_size >> 16) & 0xFF);
    start_pkt[6] = (uint8_t)((g_ft_size >> 24) & 0xFF);
    
    Serial.println("SND_START");
    sgc_ble_ft_stream_char()->writeValue(start_pkt, 7);
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
