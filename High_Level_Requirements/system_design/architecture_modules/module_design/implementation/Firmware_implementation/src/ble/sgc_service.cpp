/**
 * @file    sgc_service.cpp
 * @brief   SGC BLE GATT service — 14 characteristics (PROVEN working set).
 *
 *   ABC0: Time sync (Write)          ABC7: Flash Used % (Read/Notify)
 *   ABC1: Device Name (Read/Write)   ABC8: Run Count + Oldest Age (R/N, 6B)
 *   ABC2: Arm Side (Read/Write)      ABC9: Run List (Read, 100B JSON)
 *   ABC3: Discipline (Read/Write)    ABCA: FT Request (Write)
 *   ABC4: State+Sensors (R/N, packed)ABCB: FT Chunk (Notify, 244B)
 *   ABC5: Battery+Charging (R/N,pkd) ABCC: FT CRC (Read)
 *   ABC6: — (removed, merged→ABC8)   ABCD: FT Status (Read/Notify)
 *                                     ABD0: Cal Accuracy (Read/Notify)
 */

#include "sgc_service.h"
#include "file_transfer.h"
#include "../state_machine/state_machine.h"
#include "../storage/spi_flash.h"
#include "../storage/flash_layout.h"
#include "../storage/raw_run_store.h"
#include "../test_json.h"
#include <ArduinoBLE.h>
#include <Arduino.h>

extern RawRunStore g_runs;
extern StateMachine g_sm;

static bool g_central_connected = false;

/* V5.07: last BLE activity timestamp for zombie link detection */
static uint32_t g_last_ble_activity_ms = 0;

#define SGC_UUID(base16) "5347" base16 "-0000-1000-8000-00805F9B34FB"
static BLEService svc(SGC_UUID("0000"));

/* ── Time + Config ────────────────────────────────────────────── */
static BLEUnsignedIntCharacteristic  char_time      (SGC_UUID("ABC0"), BLEWrite);
static BLEStringCharacteristic char_dev_name  (SGC_UUID("ABC1"), BLERead | BLEWrite, 20);
static BLEByteCharacteristic    char_arm_side  (SGC_UUID("ABC2"), BLERead | BLEWrite);
static BLEByteCharacteristic    char_discipline(SGC_UUID("ABC3"), BLERead | BLEWrite);

/* ── State + Health (packed) ──────────────────────────────────── */
static BLEByteCharacteristic    char_state     (SGC_UUID("ABC4"), BLERead | BLENotify);
static BLEByteCharacteristic    char_battery   (SGC_UUID("ABC5"), BLERead | BLENotify);
// ABC4: bits 7-5=sensor_flags, bits 4-0=state
// ABC5: bit 7=charging, bits 6-0=battery%

/* ── Run info ─────────────────────────────────────────────────── */
static BLEByteCharacteristic    char_flash_used(SGC_UUID("ABC7"), BLERead | BLENotify);
static BLECharacteristic        char_run_info (SGC_UUID("ABC8"), BLERead | BLENotify, 6);  // heap (tiny)
static BLECharacteristic        char_run_list (SGC_UUID("ABC9"), BLERead, 512);            // heap (JSON)

/* ── File transfer ────────────────────────────────────────────── */
static BLECharacteristic        char_ft_req (SGC_UUID("ABCA"), BLEWrite, 8);
static BLECharacteristic        char_ft_chunk  (SGC_UUID("ABCB"), BLENotify, 244);
static BLEUnsignedIntCharacteristic  char_ft_crc  (SGC_UUID("ABCC"), BLERead);
static BLEByteCharacteristic    char_transfer  (SGC_UUID("ABCD"), BLERead | BLENotify);
static BLEByteCharacteristic    char_cal       (SGC_UUID("ABD0"), BLERead | BLENotify);

/* ═══════════════════════════════════════════════════════════════ */
/*  Config persistence                                               */
/* ═══════════════════════════════════════════════════════════════ */

static char    g_dev_name[21] = "SGC";
static uint8_t g_arm_side    = 0;
static uint8_t g_discipline  = 1;
/* Time sync (ABC0): phone writes epoch seconds at connect. Store the sync
   anchor so run timestamps are correct epoch, not 0 (1970). */
static uint32_t g_epoch_at_sync  = 0;
static uint32_t g_millis_at_sync = 0;
/* V5.01: config at top-of-chip (0x1FC000 on 2 MB; scales with SFDP size). */
struct __attribute__((packed)) FlashConfig {
    uint32_t magic;  // 0x53474343 = "SGCC"
    char     dev_name[21];
    uint8_t  arm_side;
    uint8_t  discipline;
    uint8_t  _pad[5];
};

extern SPIFlash g_flash;

void sgc_ble_config_load()
{
    FlashConfig cfg;
    uint32_t addr = flash_config_addr();
    if (addr == 0) addr = 0x1FC000u; /* boot order fallback */
    g_flash.read_data(addr, (uint8_t*)&cfg, sizeof(cfg));
    if (cfg.magic == 0x53474343) {
        memcpy(g_dev_name, cfg.dev_name, 20); g_dev_name[20] = '\0';
        g_arm_side = cfg.arm_side; g_discipline = cfg.discipline;
    }
    char_dev_name.writeValue(g_dev_name);
    char_arm_side.writeValue(g_arm_side);
    char_discipline.writeValue(g_discipline);
    BLE.setLocalName(g_dev_name);
}

void sgc_ble_config_save()
{
    FlashConfig cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.magic = 0x53474343;
    strncpy(cfg.dev_name, g_dev_name, 20);
    cfg.arm_side = g_arm_side; cfg.discipline = g_discipline;
    uint32_t addr = flash_config_addr();
    if (addr == 0) addr = 0x1FC000u;
    g_flash.erase_block(addr);
    g_flash.write_page(addr, (const uint8_t*)&cfg, sizeof(cfg));
}

const char* sgc_ble_get_device_name() { return g_dev_name; }
uint8_t     sgc_ble_get_arm_side()    { return g_arm_side; }
uint8_t     sgc_ble_get_discipline()  { return g_discipline; }

/** Current UTC epoch seconds (0 if never synced via ABC0). */
uint32_t sgc_ble_epoch_now() {
    if (g_epoch_at_sync == 0) return 0;
    return g_epoch_at_sync + (millis() - g_millis_at_sync) / 1000u;
}

/* ═══════════════════════════════════════════════════════════════ */
/*  Callbacks                                                        */
/* ═══════════════════════════════════════════════════════════════ */

static void on_time_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    uint32_t epoch = char_time.value();
    if (epoch > 1000000000u) {   /* sanity: reject pre-2001 garbage */
        g_epoch_at_sync  = epoch;
        g_millis_at_sync = millis();
        g_last_ble_activity_ms = millis();  // V5.07
    }
}
static void on_dev_name_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    String v = char_dev_name.value();
    int len = v.length(); if (len > 20) len = 20;
    memcpy(g_dev_name, v.c_str(), len); g_dev_name[len] = '\0';
    BLE.setLocalName(g_dev_name); sgc_ble_config_save();
    g_last_ble_activity_ms = millis();  // V5.07
}
static void on_arm_side_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    g_arm_side = char_arm_side.value() ? 1 : 0; sgc_ble_config_save();
    g_last_ble_activity_ms = millis();  // V5.07
}
static void on_discipline_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    uint8_t v = char_discipline.value(); if (v > 3) v = 1;
    g_discipline = v; sgc_ble_config_save();
    g_last_ble_activity_ms = millis();  // V5.07
}
static void on_ft_request(BLEDevice c, BLECharacteristic ch) {
    (void)c;
    extern void sgc_ble_ft_on_request(const uint8_t*, int);
    sgc_ble_ft_on_request(ch.value(), ch.valueLength());
    g_last_ble_activity_ms = millis();  // V5.07
}

void sgc_ble_restart_advertising(const char* why)
{
    /* V5.00: after unclean app kill, Cordio often keeps a half-open link and
       bare advertise() is a no-op. Full stop → restore ADV payload → start.
       Do NOT disconnect here — caller decides; disconnect while already in
       BLEDisconnected handler is unsafe / recursive. */
    BLE.stopAdvertise();
    BLE.setLocalName(g_dev_name);
    BLE.setAdvertisedService(svc);
    BLE.advertise();
    if (why) {
        json_begin();
        json_kv("ev", "ble_adv");
        Serial.print(','); json_kv("why", why);
        json_end();
    }
}

void sgc_ble_force_recover(const char* why)
{
    /* V5.03: force-clear any zombie / half-open BLE link and the sticky
       hold_sleep flag, then re-advertise only when the state wants to be
       discoverable (IDLE / POST_RUN). NEVER call from on_ble_disconnected —
       that handler already runs its own teardown; use this from serial,
       state entry, or the main-loop desync heal instead. */
    const char* reason = why ? why : "state";

    sgc_ble_ft_abort(reason);
    BLE.stopAdvertise();
    if (BLE.connected()) {
        BLE.disconnect();
        /* A few short polls let Cordio finish link teardown. Non-blocking. */
        for (int i = 0; i < 3 && BLE.connected(); i++) BLE.poll();
    }
    g_central_connected = false;
    g_sm.set_hold_sleep(false);

    DeviceState st = g_sm.state();
    if (st == DeviceState::SLEEP || st == DeviceState::POST_RUN)
        sgc_ble_restart_advertising(reason);

    json_begin();
    json_kv("ev", "ble_recover");
    Serial.print(','); json_kv("why", reason);
    Serial.print(','); json_kv("st", StateMachine::state_name_for(st));
    json_end();
}

bool sgc_ble_central_connected() { return g_central_connected; }

static void on_ble_connected(BLEDevice central)
{
    g_central_connected = true;
    g_sm.set_hold_sleep(true);
    g_last_ble_activity_ms = millis();  // V5.07
    json_begin();
    json_kv("ev", "ble_conn");
    Serial.print(','); json_kv("addr", central.address().c_str());
    json_end();
}

static void on_ble_disconnected(BLEDevice central)
{
    (void)central;
    g_central_connected = false;
    g_last_ble_activity_ms = millis();  // V5.07: prevent immediate zombie re-trigger after real disconnect
    /* Abort FT first so sgc_ble_ft_active() clears before hold_sleep. */
    sgc_ble_ft_abort("disconnect");
    g_sm.set_hold_sleep(false);
    /* Re-ADV only when discoverable states want it. SLEEP intentionally drops
       the link with ADV off — do not fight that path (disconnect is ours). */
    DeviceState st = g_sm.state();
    if (st == DeviceState::SLEEP || st == DeviceState::POST_RUN)
        sgc_ble_restart_advertising("disconnect");
    json_begin();
    json_kv("ev", "ble_disc");
    Serial.print(','); json_kv("st", StateMachine::state_name_for(st));
    json_end();
}

/* ═══════════════════════════════════════════════════════════════ */
/*  Init                                                             */
/* ═══════════════════════════════════════════════════════════════ */

/* Structural GATT registration: characteristics + service + event handlers.
   Idempotent across BLE.end()/begin() — clearAttributes() empties the service's
   characteristic list and the GATT attribute table each cycle, and our static
   service/characteristic objects are refcounted so they are never freed. */
static void sgc_ble_add_service()
{
    svc.addCharacteristic(char_time);       svc.addCharacteristic(char_dev_name);
    svc.addCharacteristic(char_arm_side);   svc.addCharacteristic(char_discipline);
    svc.addCharacteristic(char_state);      svc.addCharacteristic(char_battery);
    svc.addCharacteristic(char_flash_used); svc.addCharacteristic(char_run_info);
    svc.addCharacteristic(char_run_list);
    svc.addCharacteristic(char_ft_req);     svc.addCharacteristic(char_ft_chunk);
    svc.addCharacteristic(char_ft_crc);     svc.addCharacteristic(char_transfer);
    svc.addCharacteristic(char_cal);

    BLE.addService(svc);

    char_time.setEventHandler(BLEWritten, on_time_written);
    char_dev_name.setEventHandler(BLEWritten, on_dev_name_written);
    char_arm_side.setEventHandler(BLEWritten, on_arm_side_written);
    char_discipline.setEventHandler(BLEWritten, on_discipline_written);
    char_ft_req.setEventHandler(BLEWritten, on_ft_request);

    /* V5.00: connection lifecycle — hold IDLE while linked; on drop abort FT
       and hard-restart advertising (app kill left zombie link + no ADV). */
    BLE.setEventHandler(BLEConnected, on_ble_connected);
    BLE.setEventHandler(BLEDisconnected, on_ble_disconnected);
}

void sgc_ble_init()
{
    sgc_ble_add_service();

    sgc_ble_config_load();

    uint8_t z6[6] = {0}; char_run_info.writeValue(z6, 6);
    char_transfer.writeValue(0); char_flash_used.writeValue(0); char_cal.writeValue(0);

    extern const char* sgc_ble_build_run_list();
    const char* json = sgc_ble_build_run_list();
    char_run_list.writeValue((const uint8_t*)json, strlen(json));

    BLE.setLocalName(g_dev_name);
    BLE.setAdvertisedService(svc);
    BLE.advertise();

    g_last_ble_activity_ms = millis();  // V5.07: boot counts as activity
}

/* ═══════════════════════════════════════════════════════════════ */
/*  Hard radio restart                                                */
/* ═══════════════════════════════════════════════════════════════ */

bool sgc_ble_radio_restart(const char* why)
{
    const char* reason = why ? why : "hard";

    /* Hard path ONLY — never call from a BLE event handler (that runs inside
       BLE.poll()/HCI dispatch; BLE.end() tears down the very stack that is
       dispatching it). Use from serial, stream-end, or recovery escalation. */
    sgc_ble_ft_abort(reason);
    BLE.stopAdvertise();
    if (BLE.connected()) {
        BLE.disconnect();
        /* A few short polls let Cordio finish link teardown. */
        for (int i = 0; i < 3 && BLE.connected(); i++) BLE.poll();
    }
    g_central_connected = false;
    g_sm.set_hold_sleep(false);

    BLE.end();

    /* V5.15 T-008b: longer settle after heavy FT (244 B chunks); one retry. */
    uint32_t settle = millis();
    while ((int32_t)(millis() - settle) < 100) {
        delay(1);
    }

    bool began = BLE.begin();
    long retries = 0;
    if (!began) {
        delay(300);
        began = BLE.begin();
        retries = 1;
    }
    if (!began) {
        json_begin();
        json_kv("ev", "ble_radio");
        Serial.print(','); json_kv("why", reason);
        Serial.print(','); json_kv_bool("ok", false);
        Serial.print(','); json_kv("retry", retries);
        Serial.print(','); json_kv("reboot", (long)1);
        json_end();
        Serial.flush();
        delay(50);        /* let JSON drain before radio reset */
        NVIC_SystemReset();   /* V5.16 T-008c: reboot — warm_deinit (T-008a) ensures clean BLE init */
        return false;          /* never reached — keeps compiler happy */
    }

    /* Re-register GATT + persisted config on the fresh radio. */
    sgc_ble_add_service();
    sgc_ble_config_load();

    /* Repopulate run telemetry (ABC8/ABC9/ABC7) from current g_runs — do NOT
       zero like the boot path. */
    sgc_ble_set_run_count(g_runs.run_count());
    sgc_ble_set_flash_used(g_runs.flash_used_pct());
    sgc_ble_set_transfer(0);

    BLE.setLocalName(g_dev_name);
    BLE.setAdvertisedService(svc);
    DeviceState st = g_sm.state();
    if (st == DeviceState::SLEEP || st == DeviceState::POST_RUN)
        BLE.advertise();

    g_last_ble_activity_ms = millis();  // V5.07: fresh start after radio restart

    json_begin();
    json_kv("ev", "ble_radio");
    Serial.print(','); json_kv("why", reason);
    Serial.print(','); json_kv_bool("ok", true);
    Serial.print(','); json_kv("retry", retries);
    json_end();
    return true;
}

/* ═══════════════════════════════════════════════════════════════ */
/*  State / poll                                                     */
/* ═══════════════════════════════════════════════════════════════ */

void sgc_ble_update_state(DeviceState s)
{
    switch (s) {
    case DeviceState::POST_RUN:
        /* V5.11: trust g_central_connected here. BLE.connected() lags by one
           poll cycle after on_ble_connected fires, causing a race where
           update_state sees connected=false and force-recovers — killing
           the fresh link. The main-loop desync heal (g_central_connected &&
           !BLE.connected() after timeout) still catches real zombie links. */
        if (g_central_connected) {
            /* Connect event fired — keep link, refresh name only. */
            BLE.setLocalName(g_dev_name);
        } else {
            /* Restore default advertising interval before advertising. */
            BLE.setAdvertisingInterval(160);  // 160 * 0.625 ms = 100 ms default
            sgc_ble_force_recover(nullptr);
        }
        break;
    case DeviceState::SLEEP:
        /* T3: SLEEP is now the primary waiting state — advertise at slow
           interval (~2 s = 3200 units * 0.625 ms = 2000 ms) to save power.
           Do NOT force_recover — just set interval and advertise. */
        if (g_central_connected) {
            BLE.setLocalName(g_dev_name);
        } else {
            BLE.setAdvertisingInterval(3200);  // 3200 * 0.625 ms = 2000 ms
            BLE.advertise();
        }
        break;
    default:
        /* ARMED/LOGGING — save power, prevent brown-out; keep link if any */
        BLE.stopAdvertise();
        break;
    }
    uint8_t sf = char_state.value() & 0xE0;
    char_state.writeValue(sf | (static_cast<uint8_t>(s) & 0x1F));
    /* Do not zero transfer status here during active FT — that races poll. */
    if (!sgc_ble_ft_active())
        char_transfer.writeValue(0);
}
void sgc_ble_poll() { BLE.poll(); }

/* ═══════════════════════════════════════════════════════════════ */
/*  Notify setters (with packed encoding)                            */
/* ═══════════════════════════════════════════════════════════════ */

void sgc_ble_set_battery(uint8_t pct) {
    uint8_t chg = char_battery.value() & 0x80;
    char_battery.writeValue(chg | (pct & 0x7F));
}
void sgc_ble_set_cal(uint8_t cal) { char_cal.writeValue(cal); }
void sgc_ble_set_transfer(uint8_t s) { char_transfer.writeValue(s); }
void sgc_ble_set_flash_used(uint8_t pct) { char_flash_used.writeValue(pct); }

void sgc_ble_set_charging(uint8_t st) {
    uint8_t batt = char_battery.value() & 0x7F;
    char_battery.writeValue((st ? 0x80 : 0x00) | batt);
}
void sgc_ble_set_sensor_status(uint8_t bf) {
    uint8_t st = char_state.value() & 0x1F;
    char_state.writeValue(((bf & 0x07) << 5) | st);
}

void sgc_ble_set_run_count(uint16_t count)
{
    uint32_t age = g_runs.oldest_run_age();
    uint8_t buf[6];
    buf[0] = (uint8_t)(count & 0xFF);
    buf[1] = (uint8_t)((count >> 8) & 0xFF);
    buf[2] = (uint8_t)(age & 0xFF);
    buf[3] = (uint8_t)((age >> 8) & 0xFF);
    buf[4] = (uint8_t)((age >> 16) & 0xFF);
    buf[5] = (uint8_t)((age >> 24) & 0xFF);
    char_run_info.writeValue(buf, 6);

    extern const char* sgc_ble_build_run_list();
    const char* json = sgc_ble_build_run_list();
    char_run_list.writeValue((const uint8_t*)json, strlen(json));
}

/* ═══════════════════════════════════════════════════════════════ */
/*  Bridge to file_transfer.cpp                                      */
/* ═══════════════════════════════════════════════════════════════ */

extern "C" {
    BLECharacteristic* sgc_ble_ft_chunk_char()  { return &char_ft_chunk; }
    BLECharacteristic* sgc_ble_ft_status_char() { return &char_transfer; }
    BLEUnsignedIntCharacteristic* sgc_ble_ft_crc_char() { return &char_ft_crc; }
}

/* V5.07: BLE activity tracking for zombie link detection */
uint32_t sgc_ble_last_activity_ms() { return g_last_ble_activity_ms; }
void     sgc_ble_touch_activity()   { g_last_ble_activity_ms = millis(); }
