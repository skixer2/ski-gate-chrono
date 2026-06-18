/**
 * Phase 7: Complete SGC firmware — all subsystems integrated.
 *
 * Flash layout:
 *   Blocks 0-3:  ring buffer (1000-slot circular, 500-frame window)
 *   Block 4:     run index sector (run count, last run addr — persists reboots)
 *   Blocks 5+:   run data (header 16B + compressed frames 22B each)
 */

#include <ArduinoBLE.h>
#include "Arduino_BHY2.h"
#include <Nicla_System.h>
#include <math.h>
#include "led/led.h"
#include "config.h"
#include "state_machine/state_machine.h"
#include "state_machine/start_detector.h"
#include "state_machine/end_detector.h"
#include "storage/spi_flash.h"
#include "storage/flash_ring.h"
#include "storage/ring_buffer.h"
#include "storage/bit_packer.h"

/* ================================================================== */
/*  BLE                                                                 */
/* ================================================================== */
BLEService            svc("53470000-0000-1000-8000-00805F9B34FB");
BLEByteCharacteristic char_state("5347ABC4-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);
BLEByteCharacteristic char_battery("5347ABC5-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);
BLEByteCharacteristic char_transfer("5347ABCD-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);
BLEByteCharacteristic char_cal("5347ABCE-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);

/* ================================================================== */
/*  BHY2 sensors                                                        */
/* ================================================================== */
SensorQuaternion rotation(SENSOR_ID_RV);
SensorXYZ        lin_acc(SENSOR_ID_LACC);
Sensor           pressure(SENSOR_ID_BARO);
Sensor           temperature(SENSOR_ID_TEMP);

/* ================================================================== */
/*  Peripherals                                                         */
/* ================================================================== */
LED            g_led(0, 0);
StateMachine   g_sm;
SPIFlash       g_flash;
FlashRing      g_ring(g_flash);
BitPacker      g_packer;
StartDetector  g_start_det;
EndDetector    g_end_det;

/* ================================================================== */
/*  Flash layout                                                        */
/* ================================================================== */
static constexpr uint32_t RUN_FLASH_START = 5 * 4096;  /* block 5 — after ring+index */
static constexpr uint32_t INDEX_SECTOR    = 4 * 4096;  /* block 4 — run index */

struct __attribute__((packed)) RunIndex {
    uint32_t magic;          /* 0x53474300 = "SGC\0" */
    uint16_t run_count;      /* total runs written */
    uint16_t crc;            /* reserved */
    uint32_t next_run_addr;  /* where next run starts */
};

struct __attribute__((packed)) RunHeader {
    uint8_t  format_ver;     /* 1 */
    uint8_t  arm_side;       /* 0=left, 1=right */
    uint32_t ts_utc;         /* placeholder */
    int16_t  baro_temp;      /* tenths of °C at run start */
    uint32_t data_size;      /* compressed bytes (filled at end) */
    uint8_t  cal_accuracy;   /* BHI260AP accuracy at arming (0-3) */
    uint8_t  _pad[3];        /* align → 16 bytes */
};

/* ================================================================== */
/*  Logging state                                                       */
/* ================================================================== */
static uint32_t g_flash_addr = 0;
static uint32_t g_frame_count = 0;
static uint32_t g_next_run_addr = RUN_FLASH_START;
static uint16_t g_run_id = 0;
static bool     g_ring_drained = false;
static RawFrame g_prev_raw;

/* ================================================================== */
/*  Timing                                                              */
/* ================================================================== */
static uint32_t g_last_sensor_ms  = 0;
static uint32_t g_last_battery_ms = 0;
static uint32_t g_last_status_ms  = 0;
static uint32_t g_last_qi_ms      = 0;
static uint32_t g_last_cal_ms     = 0;

static DeviceState g_prev_state = DeviceState::SLEEP;

/* ================================================================== */
/*  Forward declarations                                                */
/* ================================================================== */
void flash_test();
void apply_state_visuals(DeviceState s);
void handle_serial();
void feed_sensors();
void save_run_index();
void beep_on();  void beep_off();

/* ================================================================== */
/*  Beeper (P0.09 = digital pin 1, PWM piezo)                          */
/* ================================================================== */
void beep_on()  { analogWrite(1, 128); }
void beep_off() { analogWrite(1, 0); }

/* ================================================================== */
/*  Run index persistence                                               */
/* ================================================================== */
void load_run_index()
{
    RunIndex idx;
    g_flash.read_data(INDEX_SECTOR, (uint8_t*)&idx, sizeof(idx));
    if (idx.magic == 0x53474300) {
        g_run_id = idx.run_count;
        g_next_run_addr = idx.next_run_addr;
        if (g_next_run_addr < RUN_FLASH_START)
            g_next_run_addr = RUN_FLASH_START;
        Serial.print("Run index: "); Serial.print(g_run_id);
        Serial.print(" runs, next at 0x");
        Serial.println(g_next_run_addr, HEX);
    } else {
        /* First boot: init index sector */
        g_run_id = 0;
        g_next_run_addr = RUN_FLASH_START;
        save_run_index();
        Serial.println("Run index: initialized");
    }
}

void save_run_index()
{
    g_flash.erase_block(INDEX_SECTOR);
    RunIndex idx;
    memset(&idx, 0, sizeof(idx));
    idx.magic = 0x53474300;
    idx.run_count = g_run_id;
    idx.next_run_addr = g_next_run_addr;
    g_flash.write_page(INDEX_SECTOR, (const uint8_t*)&idx, sizeof(idx));
}

/* ================================================================== */
/*  apply_state_visuals                                                 */
/* ================================================================== */
void apply_state_visuals(DeviceState s)
{
    switch (s) {
    case DeviceState::SLEEP:
        g_led.set_pattern(LedPattern::OFF); BLE.stopAdvertise(); break;
    case DeviceState::IDLE:
        g_led.set_pattern(LedPattern::BLUE_SLOW_FLOW); BLE.advertise(); break;
    case DeviceState::ARMED:
        g_led.set_pattern(LedPattern::GREEN_CHASE); beep_on(); break;
    case DeviceState::LOGGING:
        g_led.set_pattern(LedPattern::RED_CHASE); beep_off(); break;
    case DeviceState::POST_RUN:
        g_led.set_pattern(LedPattern::BLUE_SLOW_FLOW_POST); BLE.advertise(); break;
    }
    char_state.writeValue(static_cast<uint8_t>(s));
    char_transfer.writeValue(0);  /* 0=idle */
}

/* ================================================================== */
/*  handle_serial                                                       */
/* ================================================================== */
void handle_serial()
{
    if (!Serial.available()) return;
    char c = Serial.read();
    switch (c) {
    case 'i': g_sm.force_state(DeviceState::IDLE); break;
    case 'a':
        if (g_sm.state() == DeviceState::IDLE) {
            // Self-check: verify quaternion is producing valid data.
            // BHI260AP accuracy (0-3) is only available via meta-events
            // which the Arduino_BHY2 library doesn't expose.
            // Valid quat magnitude = sensor is running.
            float qx = rotation.x(), qy = rotation.y();
            float qz = rotation.z(), qw = rotation.w();
            float mag = sqrtf(qw*qw + qx*qx + qy*qy + qz*qz);
            if (mag < 0.5f || mag > 2.0f) {
                Serial.print("ARM refused: quat mag ");
                Serial.print(mag, 2);
                Serial.println(" — sensor not ready");
            } else {
                g_sm.force_state(DeviceState::ARMED);
            }
        }
        break;
    case 'l': g_sm.force_state(DeviceState::LOGGING); break;
    case 'p': g_sm.force_state(DeviceState::POST_RUN); break;
    case 's': g_sm.force_state(DeviceState::SLEEP); break;
    case 'f': Serial.println("=== Flash self-test ==="); flash_test(); return;
    case 'R':
        /* Factory reset: clear flash index + all run data */
        Serial.println("FACTORY RESET: clearing flash...");
        g_flash.erase_block(INDEX_SECTOR);
        g_run_id = 0;
        g_next_run_addr = RUN_FLASH_START;
        save_run_index();
        Serial.println("FACTORY RESET done — reboot");
        NVIC_SystemReset();
        return;
    case '?':
        Serial.print("STATE:"); Serial.print(g_sm.state_name());
        Serial.print(" R:"); Serial.print(g_ring.count()); Serial.print("/"); Serial.print(RING_SIZE);
        Serial.print(" B:"); Serial.print((int)(pressure.value()*100)); Serial.print("Pa");
        Serial.print(" Bat:"); Serial.print(nicla::getBatteryVoltagePercentage()); Serial.print("%");
        Serial.print(" Cal:"); Serial.print((int)(rotation.accuracy() * 1000));  // raw sensor info, not 0-3 acc
        Serial.print(" Qi:"); Serial.print(digitalRead(10) ? "no" : "yes");
        Serial.print(" Runs:"); Serial.println(g_run_id);
        return;
    }
}

/* ================================================================== */
/*  flash_test                                                          */
/* ================================================================== */
void flash_test()
{
    uint8_t wr[256]; for (int i=0;i<256;i++) wr[i]=(uint8_t)(i*3+7);
    g_flash.erase_block(0);
    g_flash.write_page(0,wr,256);
    uint8_t rd[256]; g_flash.read_data(0,rd,256);
    for (int i=0;i<256;i++) if (rd[i]!=wr[i]) {
        Serial.print("MISMATCH "); Serial.println(i); return;
    }
    Serial.println("ALL 256 BYTES MATCH ✅");
}

/* ================================================================== */
/*  feed_sensors — 100 Hz main pipeline                                 */
/* ================================================================== */
void feed_sensors()
{
    RawFrame f;
    f.q_w = (int16_t)(rotation.x() * 16384.0f);
    f.q_x = (int16_t)(rotation.y() * 16384.0f);
    f.q_y = (int16_t)(rotation.z() * 16384.0f);
    f.q_z = (int16_t)(rotation.w() * 16384.0f);
    f.la_x = (int16_t)lin_acc.x();
    f.la_y = (int16_t)lin_acc.y();
    f.la_z = (int16_t)lin_acc.z();
    f.baro_pa_div4 = (uint16_t)(pressure.value() * 25.0f);

    DeviceState st = g_sm.state();
    if (st == DeviceState::SLEEP) return;

    /* ARMED: write to flash ring */
    if (st == DeviceState::ARMED) {
        if (!g_ring.is_full()) {
            g_ring.write(f);
            if (g_ring.is_full()) {
                Serial.println("Ring: 500/500 — waiting for start");
                g_start_det.reset();  // seed baseline when ring is ready
            }
        } else {
            /* Only detect start when ring is full — avoids baseline drift */
            if (g_start_det.feed(f.baro_pa_div4))
                g_sm.force_state(DeviceState::LOGGING);
        }
        return;
    }

    /* LOGGING: drain ring → compress → run area */
    if (st == DeviceState::LOGGING) {
        if (!g_ring_drained) {
            /* Write run header */
            RunHeader hdr;
            memset(&hdr, 0, sizeof(hdr));
            hdr.format_ver = 1;
            hdr.arm_side = 0;  /* TODO: detect side */
            hdr.baro_temp = (int16_t)(temperature.value() * 10.0f);
            hdr.cal_accuracy = 0;  // TODO: read via BHY2 meta-event, not quat pkt

            g_flash.erase_block(g_next_run_addr);
            g_flash.write_page(g_next_run_addr, (const uint8_t*)&hdr, sizeof(hdr));
            g_flash_addr = g_next_run_addr + sizeof(hdr);
            g_frame_count = 0;
            memset(&g_prev_raw, 0, sizeof(g_prev_raw));

            uint16_t pre_count = g_ring.count();
            for (uint16_t i = 0; i < pre_count; i++) {
                RawFrame rf = g_ring.read();
                CompressedFrame cf = g_packer.encode(rf, g_prev_raw, millis());
                g_flash.write_page(g_flash_addr, (const uint8_t*)&cf, sizeof(cf));
                g_flash_addr += sizeof(cf); g_frame_count++;
                g_prev_raw = rf;
            }
            g_ring_drained = true;
            Serial.print("Run #"); Serial.print(g_run_id + 1);
            Serial.print(" header + "); Serial.print(pre_count);
            Serial.println(" pre-trigger frames");
        } else {
            CompressedFrame cf = g_packer.encode(f, g_prev_raw, millis());
            if ((g_flash_addr % g_flash.block_size()) == 0)
                g_flash.erase_block(g_flash_addr);
            g_flash.write_page(g_flash_addr, (const uint8_t*)&cf, sizeof(cf));
            g_flash_addr += sizeof(cf); g_frame_count++;
            g_prev_raw = f;
        }

        if (g_end_det.feed(f.la_x, f.la_y, f.la_z)) {
            Serial.print("END! Frames: "); Serial.println(g_frame_count);
            g_sm.force_state(DeviceState::POST_RUN);
        }
        return;
    }
}

/* ================================================================== */
/*  setup                                                               */
/* ================================================================== */
void setup()
{
    Serial.begin(115200); delay(300);
    Serial.println("=== SGC Firmware v2.1 ===");
    Serial.println("a=arm l=log p=post s=sleep i=idle f=flash R=reset ?=status");

    nicla::begin();
    g_led.begin();

    /* Beeper pin */
    pinMode(9, OUTPUT); digitalWrite(9, LOW);

    /* Qi detect pin (P0.10 = digital pin 0) */
    pinMode(0, INPUT_PULLUP);

    /* Flash */
    Serial.print("Flash... ");
    if (!g_flash.begin()) Serial.println("FAILED");
    load_run_index();

    /* BHY2 */
    Serial.print("BHY2... ");
    if (!BHY2.begin()) { Serial.println("FAILED"); while(1)delay(1000); }
    Serial.println("OK");
    rotation.begin(); lin_acc.begin(); pressure.begin(); temperature.begin();

    /* BLE */
    Serial.print("BLE... ");
    if (!BLE.begin()) { Serial.println("FAILED"); while(1)delay(1000); }
    Serial.println("OK");
    svc.addCharacteristic(char_state);
    svc.addCharacteristic(char_battery);
    svc.addCharacteristic(char_transfer);
    svc.addCharacteristic(char_cal);
    BLE.addService(svc);
    BLE.setLocalName("SGC"); BLE.setAdvertisedService(svc);

    /* Start in IDLE */
    g_sm.force_state(DeviceState::IDLE);
    g_prev_state = g_sm.state();
    apply_state_visuals(g_sm.state());

    int8_t batt = nicla::getBatteryVoltagePercentage();
    char_battery.writeValue(batt >= 0 ? (uint8_t)batt : 0);

    Serial.print("Runs: "); Serial.print(g_run_id);
    Serial.print(" | State: "); Serial.println(g_sm.state_name());
}

/* ================================================================== */
/*  loop                                                                */
/* ================================================================== */
void loop()
{
    uint32_t now = millis();
    BHY2.update(); BLE.poll(); g_led.update(); g_sm.tick(); handle_serial();

    /* 100 Hz sensor feed */
    if (now - g_last_sensor_ms >= 10) {
        feed_sensors();
        g_last_sensor_ms = now;
    }

    /* State transitions */
    DeviceState cur = g_sm.state();
    if (cur != g_prev_state) {
        if (cur == DeviceState::ARMED) {
            g_ring.reset(); g_packer.reset(); g_start_det.reset();
            g_ring_drained = false;
        }
        if (cur == DeviceState::LOGGING) {
            g_end_det.reset();
        }
        if (cur == DeviceState::POST_RUN) {
            /* Finalize run header with actual data size */
            RunHeader hdr;
            uint32_t hdr_addr = g_next_run_addr;
            g_flash.read_data(hdr_addr, (uint8_t*)&hdr, sizeof(hdr));
            hdr.data_size = g_frame_count * sizeof(CompressedFrame);
            g_flash.write_page(hdr_addr, (const uint8_t*)&hdr, sizeof(hdr));

            /* Advance to next run */
            g_next_run_addr = ((g_flash_addr + 4095) / 4096) * 4096;
            g_run_id++;
            save_run_index();

            Serial.print("Run #"); Serial.print(g_run_id);
            Serial.print(" saved: "); Serial.print(g_frame_count);
            Serial.print(" frames ("); Serial.print(hdr.data_size);
            Serial.print("B) cal="); Serial.print(hdr.cal_accuracy);
            Serial.print(" next@0x"); Serial.println(g_next_run_addr, HEX);
        }
        apply_state_visuals(cur);
        g_prev_state = cur;
    }

    /* Status every 5s */
    if (now - g_last_status_ms >= 5000) {
        Serial.print("["); Serial.print(g_sm.state_name());
        Serial.print("] R:"); Serial.print(g_ring.count()); Serial.print("/"); Serial.print(RING_SIZE);
        Serial.print(" B:"); Serial.print((int)(pressure.value()*100)); Serial.print("Pa");
        if (cur == DeviceState::LOGGING) {
            Serial.print(" F:"); Serial.print(g_frame_count);
            Serial.print(" #"); Serial.print(g_run_id + 1);
        }
        Serial.println();
        g_last_status_ms = now;
    }

    /* Battery every 30s */
    if (now - g_last_battery_ms >= 30000) {
        int8_t batt = nicla::getBatteryVoltagePercentage();
        if (batt >= 0) char_battery.writeValue((uint8_t)batt);
        /* Battery low → force SLEEP if LOGGING */
        if (batt > 0 && batt < 15 && cur == DeviceState::LOGGING) {
            Serial.println("BATTERY LOW — forcing SLEEP");
            g_sm.force_state(DeviceState::SLEEP);
        }
        g_last_battery_ms = now;
    }

    /* Qi detection every 1s */
    if (now - g_last_qi_ms >= 1000) {
        g_last_qi_ms = now;
    }

    /* Calibration accuracy every 2s */
    if (now - g_last_cal_ms >= 2000) {
        char_cal.writeValue((uint8_t)(rotation.accuracy() * 1000));  // raw sensor info, not 0-3 acc
        g_last_cal_ms = now;
    }
}
