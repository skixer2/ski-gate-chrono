/**
 * @file    pull_transfer.cpp
 * @brief   Phone-pull transfer implementation — FW 5.76.
 *
 * See pull_transfer.h for the protocol. Key implementation rules:
 *   - BLE event handlers only CAPTURE the request (single-slot buffer);
 *     all flash reads and writeValue() calls happen from the main loop
 *     (pull_poll), never inside BLE.poll() dispatch (V5.16 lesson).
 *   - Frame TX is paced (PULL_FRAME_GAP_MS) so the LL TX queue stays
 *     shallow — the wedge lesson of the push era (V5.59).
 *   - Everything is stateless between requests: no offset memory, no
 *     session — a disconnect cannot corrupt anything.
 */

#include "pull_transfer.h"
#include <Arduino.h>
#include <ArduinoBLE.h>
#include "sgc_service.h"
#include "file_transfer.h"
#include "../config.h"
#include "../storage/raw_run_store.h"
#include "../storage/littlefs_storage.h"   /* RunHeader, CRC32_TRAILER_SIZE */
#include "../test_json.h"

extern RawRunStore g_runs;

extern "C" BLECharacteristic* sgc_ble_console_char();

/* ── Request capture (filled by BLE handler / serial, consumed by poll) ── */
static char           g_req[80];
static volatile bool  g_req_pending = false;
static bool           g_req_via_ble = false;

/* ── Response TX state machine ─────────────────────────────────────── */
enum class TxState : uint8_t { IDLE, DIR_LINE, FRAME, TAIL };
static TxState   g_tx = TxState::IDLE;
static uint32_t  g_tx_last_ms = 0;

/* Directory walk */
static uint16_t  g_dir_idx = 0;

/* Chunk job: r <id> <off> <len> */
static uint16_t  g_job_run = 0;
static uint32_t  g_job_off = 0;
static uint32_t  g_job_len = 0;      /* requested (already clamped) */
static uint32_t  g_job_sent = 0;
static uint8_t   g_job_seq = 0;
static char      g_tail[24];         /* "OK <n>" or "E <n>" */
static bool      g_job_err = false;

static uint8_t   g_frame[PULL_FRAME_PAYLOAD];

/* ── Watchdog ──────────────────────────────────────────────────────── */
static bool      g_wdt_armed = false;
static uint32_t  g_last_req_ms = 0;

void sgc_pull_init()
{
    g_req_pending = false;
    g_tx = TxState::IDLE;
    g_wdt_armed = false;
}

void sgc_pull_touch()
{
    g_last_req_ms = millis();
    g_wdt_armed = true;
}

void sgc_pull_link_reset()
{
    g_wdt_armed = false;
    g_req_pending = false;
    g_tx = TxState::IDLE;
}

/* ── Output: same text to console char (notify) or USB serial ─────── */
static void emit_line(const char* s)
{
    if (g_req_via_ble) {
        BLECharacteristic* c = sgc_ble_console_char();
        if (c) c->writeValue((const uint8_t*)s, strlen(s));
    }
    Serial.println(s);   /* USB always mirrors (forensics / serial parity) */
}

static const char* HEXD = "0123456789ABCDEF";
static size_t hex_encode(const uint8_t* d, size_t n, char* out)
{
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = HEXD[d[i] >> 4];
        out[2 * i + 1] = HEXD[d[i] & 0xF];
    }
    return 2 * n;
}

/* ── Request intake ────────────────────────────────────────────────── */
void sgc_pull_handle_line(const char* line, bool via_ble)
{
    /* Drop anything that smells binary (legacy FT CMD_START bytes). */
    if ((uint8_t)line[0] < 0x20 || (uint8_t)line[0] > 0x7E) return;
    if (!line[0]) return;
    if (g_req_pending) return;          /* phone must wait for the reply */

    strncpy(g_req, line, sizeof(g_req) - 1);
    g_req[sizeof(g_req) - 1] = '\0';
    g_req_via_ble = via_ble;
    g_req_pending = true;
    sgc_pull_touch();                   /* any request feeds the watchdog */
}

/* ── Paced TX poll (main loop only) ───────────────────────────────── */
static void start_dir()
{
    g_dir_idx = 0;
    g_tx = TxState::DIR_LINE;
    g_tx_last_ms = 0;   /* first line immediately */
}

static void start_chunk(uint16_t run, uint32_t off, uint32_t len)
{
    g_job_run = run; g_job_off = off; g_job_len = len;
    g_job_sent = 0; g_job_seq = 0; g_job_err = false;
    g_tx = TxState::FRAME;
    g_tx_last_ms = millis();
}

static void parse_request()
{
    char cmd = g_req[0];

    if (cmd == 'D') { start_dir(); return; }

    if (cmd == 'r') {
        long id = -1, off = -1, len = -1;
        if (sscanf(g_req, "r %ld %ld %ld", &id, &off, &len) != 3 ||
            id < 0 || off < 0 || len <= 0) {
            Serial.print("{\"ev\":\"pull_dbg\",\"req\":\"");
            Serial.print(g_req);
            Serial.println("\"}");   /* 5.76 bench: why E 3 */
            strncpy(g_tail, "E 3", sizeof(g_tail));   /* malformed */
            g_tx = TxState::TAIL; g_tx_last_ms = millis();
            return;
        }
        if (!g_runs.get_entry_by_id((uint16_t)id)) {
            strncpy(g_tail, "E 1", sizeof(g_tail));   /* bad run */
            g_tx = TxState::TAIL; g_tx_last_ms = millis();
            return;
        }
        /* Total on-disk size = header + payload + CRC trailer. */
        const RunEntry* e = g_runs.get_entry_by_id((uint16_t)id);
        uint32_t total = sizeof(RunHeader) + e->compressed_size + CRC32_TRAILER_SIZE;
        if ((uint32_t)off >= total) {
            strncpy(g_tail, "E 2", sizeof(g_tail));   /* bad offset */
            g_tx = TxState::TAIL; g_tx_last_ms = millis();
            return;
        }
        uint32_t remain = total - (uint32_t)off;
        if ((uint32_t)len > remain) len = (long)remain;
        if ((uint32_t)len > PULL_CHUNK_BYTES) len = PULL_CHUNK_BYTES;
        start_chunk((uint16_t)id, (uint32_t)off, (uint32_t)len);
        return;
    }

    /* Unknown command */
    strncpy(g_tail, "E 4", sizeof(g_tail));
    g_tx = TxState::TAIL; g_tx_last_ms = millis();
}

static void tx_poll()
{
    uint32_t now = millis();
    if (g_tx == TxState::IDLE) return;

    if (g_tx == TxState::DIR_LINE) {
        if (now - g_tx_last_ms < PULL_FRAME_GAP_MS) return;
        g_tx_last_ms = now;
        char line[96];
        if (g_dir_idx < g_runs.run_count()) {
            const RunEntry* e = g_runs.get_entry(g_dir_idx);
            if (e) {
                uint32_t total = sizeof(RunHeader) + e->compressed_size + CRC32_TRAILER_SIZE;
                snprintf(line, sizeof(line), "{\"run\":%u,\"ts\":%lu,\"sz\":%lu,\"side\":%u}",
                         e->run_id, (unsigned long)e->timestamp,
                         (unsigned long)total, e->arm_side);
                emit_line(line);
            }
            g_dir_idx++;
        } else {
            snprintf(line, sizeof(line), "{\"d_end\":%u}", g_runs.run_count());
            emit_line(line);
            g_tx = TxState::IDLE;
        }
        return;
    }

    if (g_tx == TxState::FRAME) {
        if (now - g_tx_last_ms < PULL_FRAME_GAP_MS) return;
        g_tx_last_ms = now;

        uint32_t remain = g_job_len - g_job_sent;
        uint32_t take = (remain > PULL_FRAME_PAYLOAD) ? PULL_FRAME_PAYLOAD : remain;
        if (!g_runs.read_run_data(g_job_run, g_job_off + g_job_sent, g_frame, take)) {
            strncpy(g_tail, "E 5", sizeof(g_tail));   /* flash read failed */
            g_tx = TxState::TAIL;
            return;
        }
        char frame[2 * PULL_FRAME_PAYLOAD + 16];
        char* p = frame;
        *p++ = '[';
        p += snprintf(p, 8, "%u %lu ", g_job_seq, (unsigned long)take);
        p += hex_encode(g_frame, take, p);
        *p++ = ']';
        *p = '\0';
        emit_line(frame);
        g_job_sent += take;
        g_job_seq++;
        if (g_job_sent >= g_job_len) {
            snprintf(g_tail, sizeof(g_tail), "OK %lu", (unsigned long)g_job_sent);
            g_tx = TxState::TAIL;
        }
        return;
    }

    if (g_tx == TxState::TAIL) {
        if (now - g_tx_last_ms < PULL_FRAME_GAP_MS) return;
        g_tx_last_ms = now;
        emit_line(g_tail);
        g_tx = TxState::IDLE;
    }
}

void sgc_pull_poll()
{
    if (g_req_pending && g_tx == TxState::IDLE) {
        g_req_pending = false;
        parse_request();
    }
    tx_poll();

    /* Request watchdog (FWR-3): armed after the first request, fed by any
       request. Silence > PULL_WDT_MS while connected → disconnect + re-ADV. */
    if (g_wdt_armed && sgc_ble_central_connected() &&
        (millis() - g_last_req_ms) > PULL_WDT_MS) {
        g_wdt_armed = false;
        json_begin();
        json_kv("ev", "pull_wdt");
        Serial.print(','); json_kv("idle_ms", (long)(millis() - g_last_req_ms));
        json_end();
        sgc_ble_force_recover("pull_wdt");
    }
}
