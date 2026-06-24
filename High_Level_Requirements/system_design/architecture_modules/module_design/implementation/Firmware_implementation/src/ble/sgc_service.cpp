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
#include "../state_machine/state_machine.h"
#include "../storage/spi_flash.h"
#include <ArduinoBLE.h>
#include <Arduino.h>

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
static BLECharacteristic        char_run_list (SGC_UUID("ABC9"), BLERead, 100);            // heap (JSON)

/* ── File transfer ────────────────────────────────────────────── */
static BLEUnsignedShortCharacteristic char_ft_req (SGC_UUID("ABCA"), BLEWrite);
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
static constexpr uint32_t CONFIG_FLASH_ADDR = 509 * 4096;

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
    g_flash.read_data(CONFIG_FLASH_ADDR, (uint8_t*)&cfg, sizeof(cfg));
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
    g_flash.erase_block(CONFIG_FLASH_ADDR);
    g_flash.write_page(CONFIG_FLASH_ADDR, (const uint8_t*)&cfg, sizeof(cfg));
}

const char* sgc_ble_get_device_name() { return g_dev_name; }
uint8_t     sgc_ble_get_arm_side()    { return g_arm_side; }
uint8_t     sgc_ble_get_discipline()  { return g_discipline; }

/* ═══════════════════════════════════════════════════════════════ */
/*  Callbacks                                                        */
/* ═══════════════════════════════════════════════════════════════ */

static void on_time_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
}
static void on_dev_name_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    String v = char_dev_name.value();
    int len = v.length(); if (len > 20) len = 20;
    memcpy(g_dev_name, v.c_str(), len); g_dev_name[len] = '\0';
    BLE.setLocalName(g_dev_name); sgc_ble_config_save();
}
static void on_arm_side_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    g_arm_side = char_arm_side.value() ? 1 : 0; sgc_ble_config_save();
}
static void on_discipline_written(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    uint8_t v = char_discipline.value(); if (v > 3) v = 1;
    g_discipline = v; sgc_ble_config_save();
}
static void on_ft_request(BLEDevice c, BLECharacteristic ch) {
    (void)c; (void)ch;
    extern void sgc_ble_ft_on_request(uint16_t);
    sgc_ble_ft_on_request(char_ft_req.value());
}

/* ═══════════════════════════════════════════════════════════════ */
/*  Init                                                             */
/* ═══════════════════════════════════════════════════════════════ */

void sgc_ble_init()
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

    sgc_ble_config_load();

    uint8_t z6[6] = {0}; char_run_info.writeValue(z6, 6);
    char_transfer.writeValue(0); char_flash_used.writeValue(0); char_cal.writeValue(0);

    extern const char* sgc_ble_build_run_list();
    const char* json = sgc_ble_build_run_list();
    char_run_list.writeValue((const uint8_t*)json, strlen(json));

    BLE.setLocalName(g_dev_name);
    BLE.setAdvertisedService(svc);
    BLE.advertise();
}

/* ═══════════════════════════════════════════════════════════════ */
/*  State / poll                                                     */
/* ═══════════════════════════════════════════════════════════════ */

void sgc_ble_update_state(DeviceState s)
{
    switch (s) {
    case DeviceState::SLEEP: BLE.stopAdvertise(); break;
    case DeviceState::IDLE: case DeviceState::POST_RUN: BLE.advertise(); break;
    default: break;
    }
    uint8_t sf = char_state.value() & 0xE0;
    char_state.writeValue(sf | (static_cast<uint8_t>(s) & 0x1F));
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
    extern uint32_t g_oldest_run_age;
    uint8_t buf[6];
    buf[0] = (uint8_t)(count & 0xFF);
    buf[1] = (uint8_t)((count >> 8) & 0xFF);
    uint32_t age = g_oldest_run_age;
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
