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
 *
 * V5.59: BURST & BREATHE — S22 LINK_SUPERVISION_TIMEOUT mitigation.
 *
 *        Root-cause analysis (ArduinoBLE/Cordio internals, verified in-tree):
 *        HCI.sendAclPkt() busy-blocks (`while (_pendingPkt >= _maxPkt) poll();`)
 *        when the controller TX queue is full, and ATT.handleNotify() DISCARDS
 *        its return value — so writeValue() can freeze the FT loop for seconds
 *        with zero error signal. Pushing 244 B @ 30ms exceeds what the link
 *        drains (~1 pkt per 40-60ms connection event), so the device sits
 *        permanently at the congestion edge; when the S22 controller/host
 *        suffocates and delays LL ACKs, the link starves → phone declares
 *        LINK_SUPERVISION_TIMEOUT (0x08) after its 5 s supervision window.
 *
 *        Strategy: bursts of FT_BURST_COUNT chunks @ 30 ms, then a 100 ms
 *        "breathe" gap so Android can drain its GATT/HCI queue and service
 *        Link Layer housekeeping. Effective cadence ~40 ms/chunk — barely
 *        slower than turbo, but the queue never stays pinned at _maxPkt.
 *
 *        TX block forensics: time every writeValue(). blocked > 50 ms logs
 *        ft_txblk; > 100 ms triggers an adaptive extra breathe; > 8000 ms
 *        aborts the transfer ("tx_blocked") — the link is already dying and
 *        the app deserves a clean FT_ERROR instead of a zombie timeout.
 *
 * V5.60: WDT SURVIVAL — bench result 2026-08-31 (TC-2026-08-26-001):
 *        5.59 stalled at chunk 78, then the device REBOOTED (boot rr:2 =
 *        RESETREAS bit1 = watchdog). Sequence: phone wedged → writeValue()
 *        blocked inside sendAclPkt() → nobody feeds NRF_WDT (main loop stuck)
 *        → 5 s WDT fired mid-transfer → radio died → phone logged
 *        LINK_SUPERVISION_TIMEOUT as a SYMPTOM. The 5.59 block forensics
 *        never printed because writeValue() never returned before the reset.
 *
 *        nRF52 WDT cannot be stopped/reconfigured once started (CRV locked),
 *        so the only way to survive unbounded library blocks is feeding from
 *        interrupt context: mbed::Ticker feeds NRF_WDT->RR[0] every 500 ms
 *        while FT is active (scoped to FT only — WDT still guards all other
 *        code paths). A phone wedge now = transfer PAUSE (blocked write),
 *        then either recovery (adaptive deep-throttle continues) or a clean
 *        disconnect abort. No more mid-transfer reboots.
 *
 * V5.65: ABORT PROTOCOL FIX — the legacy abort wrote a BARE FT_ERROR (3)
 *        byte to the stream char; in the L-STREAM packet protocol 0x03 =
 *        FINAL/CRC, so the app completed "successfully" with a partial
 *        buffer (run #2 never recovered, pre-reset run lost). Abort now
 *        sends a proper ERROR packet [0x04, code]:
 *        0x10 tx_blocked · 0x11 phone · 0x12 new_request · 0xFF other.
 *
 * V5.67: POST-FT WDT GRACE — 2026-09-01 native bench: two runs completed,
 *        third wedged → clean tx_blocked, then the device rebooted (rr:2).
 *        The FT ISR WDT feeder was stopped immediately on ft_abort while the
 *        wedged BLE link was still tearing down. Keep feeding for a 3 s grace
 *        window via mbed::Timeout (stops even if the main loop is blocked),
 *        and feed explicitly around the abort ERROR notify.
 *
 * V5.64: RE-ADVERTISE ON FT EXIT — 5.63 bench: resume worked (app failed
 *        fast at 26 620 B, re-requested) but the reconnect hit
 *        GATT_CONNECTION_TIMEOUT: CMD_START forces the SM to LOGGING (no
 *        ADV) and never restored it, and the txfail retry window (~18 s)
 *        kept the device busy while the phone hammered reconnects.
 *        Now: pre-FT state captured at CMD_START and restored on EVERY FT
 *        exit (done/error/abort/cmd_abort) + immediate re-advertise
 *        (100 ms) when not connected. Retry window cut to 3 fails (~6.6 s)
 *        — the S22 wedge proved terminal in every bench (0 recoveries), so
 *        extra patience only delayed re-advertising.
 *
 * V5.63: RESUME-CAPABLE TRANSFER — CMD_START accepts optional offset.
 *        5.62 bench (Garmin watch DISCONNECTED — major confounder found):
 *        steady 60 ms cadence streamed 126/162 chunks (77.5%) before the
 *        S22 wedged; 6 escalating retries ALL blocked 2000 ms → wedge is
 *        TERMINAL within an attempt (never recovers, phone kills link at
 *        supervision). Conclusion: transfers must survive phone wedges by
 *        RESUMING from the last received byte instead of restarting.
 *
 *        CMD_START wire format (backwards compatible):
 *          [0, runId_lo, runId_hi]                          → offset 0
 *          [0, runId_lo, runId_hi, off0, off1, off2, off3]  → resume
 *        On resume the device CRC-prefills the skipped prefix from flash
 *        (no BLE traffic) so the final CRC32 remains valid over the whole
 *        run, then streams from offset. App tracks received payload bytes
 *        and re-requests from there after tx_blocked/disconnect.
 *
 * V5.61: VENDORED + PATCHED ArduinoBLE (lib/ArduinoBLE, SGC_PATCHES.md).
 *        5.60 bench: device survived the wedge (no reboot) but ZOMBIE-HUNG
 *        inside sendAclPkt's busy-poll — _pendingPkt is never cleared on
 *        disconnect, so the loop spins forever even after the phone is gone.
 *        Library patches: (1) sendAclPkt busy-wait bounded at 2000 ms,
 *        returns -1; (2) _pendingPkt=0 on EVT_DISCONN_COMPLETE; (3)
 *        handleNotify propagates the failure → writeValue() returns 0.
 *        FT now checks writeValue() != 0: 3 consecutive TX failures with
 *        300 ms holds → ft_abort("tx_blocked"). The main loop regains
 *        control at least every ~2 s in the worst case.
 */

#include "file_transfer.h"
#include "../config.h"
#include "sgc_service.h"
#include "../storage/raw_run_store.h"
#include "../test_json.h"
#include <ArduinoBLE.h>
#include <Arduino.h>
#include <mbed.h>  /* mbed::Ticker for ISR-context WDT feed (V5.60) */
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

/* Use production MTU: 244 B (nRF52/Android standard) */
static constexpr size_t   FT_CHUNK_SIZE  = 244;
/* V5.62 pacing experiment: steady 60 ms cadence, NO breathe.
   Bench history: 30 ms turbo wedges the S22 within seconds (stalls at
   chunk 12/78/136 — noisy, always early). Hypothesis to test: a gentle
   steady stream (2.4 KB/s → 39 KB in ~16 s) stays under the S22's wedge
   threshold entirely. Burst & Breathe disabled (breathe gaps may cause
   Android to batch-deliver queued notifications → main-thread hitch). */
static constexpr uint32_t FT_CHUNK_MS    = 60;   /* steady cadence */
static constexpr uint32_t FT_BURST_COUNT = 0xFFFF; /* breathe disabled */
static constexpr uint32_t FT_BREATHE_MS  = 100;  /* unused while count=0xFFFF */
static constexpr uint32_t FT_PROG_EVERY  = 10;
/* V5.59/V5.60 TX block forensics thresholds */
static constexpr uint32_t FT_BLK_LOG_MS      = 50;   /* log ft_txblk above this */
static constexpr uint32_t FT_BLK_THROTTLE_MS = 100;  /* adaptive breathe above this */
static constexpr uint32_t FT_BLK_DEEP_MS     = 1000; /* deep congestion: +500 ms hold */
static constexpr uint32_t FT_BLK_ABORT_MS    = 8000; /* abort tx_blocked above this */

/* V5.64: SM state before CMD_START forced LOGGING — restored on FT exit. */
static DeviceState g_ft_pre_state = DeviceState::SLEEP;
static bool        g_ft_pre_state_valid = false;

/* V5.60: interrupt-context WDT feeder, active only while FT_STREAMING.
   Survives writeValue() blocking inside HCI.sendAclPkt() (see header). */
static mbed::Ticker g_ft_wdt_ticker;
static bool g_ft_wdt_ticker_on = false;
static mbed::Timeout g_ft_wdt_stop_timeout;
static void ft_wdt_feed_isr() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }
static void ft_wdt_ticker_stop_isr()
{
    /* Runs even if the main loop is blocked in Cordio after ft_abort: the
       FT grace period really expires, then the normal WDT guards again. */
    g_ft_wdt_ticker.detach();
    g_ft_wdt_ticker_on = false;
}
static void ft_wdt_ticker_start()
{
    g_ft_wdt_stop_timeout.detach();
    if (g_ft_wdt_ticker_on) return;
    g_ft_wdt_ticker.attach_us(ft_wdt_feed_isr, 500000);  /* 500 ms << 5 s WDT */
    g_ft_wdt_ticker_on = true;
}
static void ft_wdt_ticker_stop()
{
    g_ft_wdt_stop_timeout.detach();
    if (!g_ft_wdt_ticker_on) return;
    g_ft_wdt_ticker.detach();
    g_ft_wdt_ticker_on = false;
}
static void ft_wdt_ticker_grace(uint32_t grace_ms)
{
    if (!g_ft_wdt_ticker_on) return;
    g_ft_wdt_stop_timeout.attach_us(ft_wdt_ticker_stop_isr, grace_ms * 1000);
}

/* V5.64: called on EVERY FT exit — restore the pre-FT state (CMD_START
   forces LOGGING, which stops advertising) and re-advertise immediately at
   100 ms so a resume reconnect finds us at once. */
static void ft_exit_restore_state()
{
    /* V5.67: keep the ISR WDT feed alive briefly AFTER FT exits. The link is
       still tearing down: the abort/error notify + next BLE.poll() can sit on
       the same wedged Cordio path that triggered tx_blocked. Stopping the feed
       here let the 5 s WDT fire immediately after ft_abort (bench 2026-09-01:
       two native downloads OK, third tx_blocked → boot rr:2). The Timeout
       detaches the Ticker even if the main loop stays blocked. */
    ft_wdt_ticker_grace(3000);
    if (g_ft_pre_state_valid) {
        g_sm.force_state(g_ft_pre_state);
        g_ft_pre_state_valid = false;
    }
    if (!BLE.connected()) {
        BLE.setAdvertisingInterval(160);  /* 160 * 0.625 ms = 100 ms */
        BLE.advertise();
    }
}

// V5.52: Move buffer to static memory to eliminate stack overflow risk
static uint8_t g_ft_buffer[FT_CHUNK_SIZE];

/* V5.59 state */
static uint16_t g_ft_burst_pos   = 0;  /* chunks sent in current burst */
static uint32_t g_ft_hold_until_ms = 0; /* adaptive throttle: no chunk before this */
static uint32_t g_ft_tx_blocks   = 0;  /* cumulative blocked-write events (>50ms) */
static uint32_t g_ft_tx_blk_ms   = 0;  /* cumulative ms spent blocked in writeValue */
static uint8_t  g_ft_tx_fails    = 0;  /* V5.61: consecutive writeValue()==0 failures */
static uint8_t  g_ft_tx_recov    = 0;  /* V5.62: recoveries after >=1 failure */
/* V5.64: short flat retry — the S22 wedge proved TERMINAL in every bench
   (5.61: 3×2001 ms; 5.62: 6×2001 ms, zero recoveries). Patience beyond
   ~6 s only delays re-advertising for the resume reconnect.
   Holds: 300 ms flat, abort after 3 → ~6.6 s worst case. */
static constexpr uint8_t FT_TX_FAIL_ABORT = 3;   /* abort after this many in a row */
static uint32_t ft_fail_hold_ms(uint8_t /*fails*/) { return 300; }

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
    ft_exit_restore_state();  /* V5.64: restore SM + re-advertise */
    json_begin();
    json_kv("ev", "ft_abort");
    Serial.print(','); json_kv("reason", reason ? reason : "?");
    Serial.print(','); json_kv("off", (long)g_ft_offset);
    Serial.print(','); json_kv("sz", (long)g_ft_size);
    Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
    json_end();
    if (BLE.connected()) {
        /* V5.65: proper ERROR packet — the bare "3" collided with packet
           type 0x03 (FINAL) in the app parser. */
        NRF_WDT->RR[0] = WDT_RR_RR_Reload;
        uint8_t code = 0xFF;
        if (reason) {
            if (!strcmp(reason, "tx_blocked"))  code = 0x10;
            else if (!strcmp(reason, "phone"))       code = 0x11;
            else if (!strcmp(reason, "new_request")) code = 0x12;
        }
        uint8_t pkt[2] = {0x04, code};
        sgc_ble_ft_stream_char()->writeValue(pkt, 2);
        BLE.poll();
        NRF_WDT->RR[0] = WDT_RR_RR_Reload;
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

    /* V5.59 Burst & Breathe gate:
       - within a burst: FT_CHUNK_MS (30 ms) between chunks
       - after FT_BURST_COUNT chunks: FT_BREATHE_MS (100 ms) gap.
       The gate just returns — the main loop keeps calling sgc_ble_poll()
       during the gap, so Cordio/HCI keeps draining while Android catches up. */
    /* Adaptive throttle hold (signed compare — wrap-safe). */
    if ((int32_t)(now - g_ft_hold_until_ms) < 0) return;

    uint32_t need = (g_ft_burst_pos >= FT_BURST_COUNT) ? FT_BREATHE_MS : FT_CHUNK_MS;
    if (now - g_ft_last_chunk_ms < need) return;
    g_ft_last_chunk_ms = now;
    if (g_ft_burst_pos >= FT_BURST_COUNT) g_ft_burst_pos = 0;  /* breathe done */

    uint8_t* buf = g_ft_buffer;
    size_t remaining = (g_ft_offset < g_ft_size) ? (g_ft_size - g_ft_offset) : 0;
    // Reserve 2 bytes for Type and Index
    size_t send_len = (remaining > (FT_CHUNK_SIZE - 2)) ? (FT_CHUNK_SIZE - 2) : remaining;

    if (send_len == 0) {
        g_ft_state = FT_DONE;
        ft_exit_restore_state();  /* V5.64: restore SM + re-advertise */
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
        ft_exit_restore_state();  /* V5.64: restore SM + re-advertise */
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

    /* V5.59/V5.61: writeValue() can busy-block inside HCI.sendAclPkt()
       (bounded at 2000 ms by the vendored patch, returns 0 on failure).
       Measure the block AND check the return — the earliest possible signal
       that the phone is falling behind. */
    uint32_t wr_t0 = millis();
    int wr_rc = sgc_ble_ft_stream_char()->writeValue(buf, send_len + 2);
    uint32_t wr_blocked = millis() - wr_t0;

    if (wr_rc == 0) {
        /* V5.61: TX queue stuck (bounded poll timed out) or link gone.
           Do NOT advance — hold, then retry the same chunk. */
        g_ft_tx_fails++;
        json_begin();
        json_kv("ev", "ft_txfail");
        Serial.print(','); json_kv("chunk", (long)g_ft_chunks);
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        Serial.print(','); json_kv("fails", (long)g_ft_tx_fails);
        Serial.print(','); json_kv("blk_ms", (long)wr_blocked);
        json_end();
        NRF_WDT->RR[0] = WDT_RR_RR_Reload;
        if (g_ft_tx_fails >= FT_TX_FAIL_ABORT) {
            sgc_ble_ft_abort("tx_blocked");
            return;
        }
        g_ft_hold_until_ms = millis() + ft_fail_hold_ms(g_ft_tx_fails);
        return;
    }
    if (g_ft_tx_fails > 0) {
        /* V5.62: wedge was TRANSIENT — link recovered. Log it: this is the
           key datapoint for the transient-vs-terminal question. */
        g_ft_tx_recov++;
        json_begin();
        json_kv("ev", "ft_recover");
        Serial.print(','); json_kv("chunk", (long)g_ft_chunks);
        Serial.print(','); json_kv("after_fails", (long)g_ft_tx_fails);
        Serial.print(','); json_kv("recov", (long)g_ft_tx_recov);
        json_end();
    }
    g_ft_tx_fails = 0;

    if (wr_blocked > FT_BLK_LOG_MS) {
        g_ft_tx_blocks++;
        g_ft_tx_blk_ms += wr_blocked;
        json_begin();
        json_kv("ev", "ft_txblk");
        Serial.print(','); json_kv("blk_ms", (long)wr_blocked);
        Serial.print(','); json_kv("chunk", (long)g_ft_chunks);
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        Serial.print(','); json_kv("blocks", (long)g_ft_tx_blocks);
        Serial.print(','); json_kv("tot_ms", (long)g_ft_tx_blk_ms);
        json_end();
    }

    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    if (wr_blocked > FT_BLK_ABORT_MS) {
        /* Link is dying (phone stopped LL ACKs for seconds). Clean abort
           beats a zombie supervision timeout on the app side. */
        sgc_ble_ft_abort("tx_blocked");
        return;
    }

    /* V5.57: Safe-Stream Pacing
       Small delay to prevent overloading the nRF52 BLE stack.
       The 40ms connection interval handles the rest. */
    delay(10);
    BLE.poll();

    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    g_ft_offset += send_len;
    g_ft_chunks++;
    g_ft_burst_pos++;

    /* V5.59/V5.60 adaptive throttle: if the controller made us wait, the
       phone is behind — hold the next chunk so queues can drain. Deep
       congestion (>1 s block) gets a 500 ms hold: the link nearly died.
       Uses a dedicated hold-until timestamp; never push g_ft_last_chunk_ms
       into the future (unsigned delta in the gate would wrap and fire early). */
    if (wr_blocked > FT_BLK_DEEP_MS)
        g_ft_hold_until_ms = millis() + 500;
    else if (wr_blocked > FT_BLK_THROTTLE_MS)
        g_ft_hold_until_ms = millis() + FT_BREATHE_MS;

    /* V5.57: Removed state = FT_WAITING_ACK.
       We now stay in FT_STREAMING for Paced-Push. */

    if ((g_ft_chunks % FT_PROG_EVERY) == 0 || g_ft_offset >= g_ft_size) {
        json_begin();
        json_kv("ev", "ft_prog");
        Serial.print(','); json_kv("off", (long)g_ft_offset);
        Serial.print(','); json_kv("sz", (long)g_ft_size);
        Serial.print(','); json_kv("chunks", (long)g_ft_chunks);
        Serial.print(','); json_kv("blocks", (long)g_ft_tx_blocks);
        Serial.print(','); json_kv("blk_ms", (long)g_ft_tx_blk_ms);
        Serial.print(','); json_kv("recov", (long)g_ft_tx_recov);
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

    if (cmd == 0) {  /* CMD_START: [0, runId_lo, runId_hi] (+opt offset V5.63) */
        if (len < 3) return;
        uint16_t run_id = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
        /* V5.63: optional little-endian resume offset in data[3..6]. */
        uint32_t resume_off = 0;
        if (len >= 7) {
            resume_off = (uint32_t)data[3]
                       | ((uint32_t)data[4] << 8)
                       | ((uint32_t)data[5] << 16)
                       | ((uint32_t)data[6] << 24);
        }

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

    /* V5.63: resume — validate offset, then CRC-prefill the skipped prefix
       (pure flash reads, no BLE) so the final CRC32 stays valid over the
       whole run. ~39 KB worst case ≈ tens of ms; WDT fed per block. */
    if (resume_off > 0 && resume_off < g_ft_size) {
        uint32_t pos = 0;
        bool prefill_ok = true;
        while (pos < resume_off) {
            size_t n = (resume_off - pos > (FT_CHUNK_SIZE - 2))
                     ? (FT_CHUNK_SIZE - 2) : (resume_off - pos);
            if (!g_runs.read_run_data(run_id, pos, g_ft_buffer, n)) {
                prefill_ok = false;
                break;
            }
            for (size_t i = 0; i < n; i++)
                g_ft_crc = RawRunStore::crc32_update(g_ft_crc, g_ft_buffer[i]);
            pos += n;
            NRF_WDT->RR[0] = WDT_RR_RR_Reload;
        }
        if (prefill_ok) {
            g_ft_offset = resume_off;
            g_ft_chunks = resume_off / (FT_CHUNK_SIZE - 2);
            json_begin();
            json_kv("ev", "ft_resume");
            Serial.print(','); json_kv("off", (long)resume_off);
            Serial.print(','); json_kv("chunk", (long)g_ft_chunks);
            json_end();
        } else {
            json_begin();
            json_kv("ev", "ft_resume_fail");
            Serial.print(','); json_kv("off", (long)resume_off);
            json_end();
            g_ft_crc = 0xFFFFFFFF;  /* fall back to full transfer */
        }
    }

    g_ft_start_ms = millis();
    g_ft_last_chunk_ms = 0;
    g_ft_burst_pos   = 0;   /* V5.59 */
    g_ft_hold_until_ms = 0; /* V5.59 */
    g_ft_tx_blocks   = 0;   /* V5.59 */
    g_ft_tx_blk_ms   = 0;   /* V5.59 */
    g_ft_tx_fails    = 0;   /* V5.61 */
    g_ft_tx_recov    = 0;   /* V5.62 */
    g_ft_state    = FT_STREAMING;
    ft_wdt_ticker_start();  /* V5.60: ISR WDT feed while streaming */
    
    // Sync global state machine to prevent SLEEP timers from interfering.
    // V5.64: capture pre-FT state — restored on every FT exit so the device
    // advertises again for the resume reconnect.
    // V5.66: only force from ARMED (the only case where the transition can
    // succeed). From SLEEP/IDLE/POST_RUN the main-loop hold_sleep flag
    // (BLE.connected() || ft_active) already guards timers, and the blocked
    // transition just spammed "state_blocked not_armed" every CMD_START.
    g_ft_pre_state = g_sm.state();
    g_ft_pre_state_valid = false;
    if (g_ft_pre_state == DeviceState::ARMED) {
        g_sm.force_state(DeviceState::LOGGING);
        g_ft_pre_state_valid = true;
    }

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
    Serial.print(','); json_kv("off", (long)g_ft_offset);
    Serial.print(','); json_kv("chunk", (long)FT_CHUNK_SIZE);
    Serial.print(','); json_kv("cad_ms", (long)FT_CHUNK_MS);
    Serial.print(','); json_kv("ms", (long)0);
    json_end();
    return;
    }

    if (cmd == 2) {  /* CMD_ABORT */
        if (g_ft_state == FT_STREAMING) {
            g_ft_state = FT_IDLE;
            ft_exit_restore_state();  /* V5.64: restore SM + re-advertise */
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
