/**
 * @file    sgc_service.cpp
 * @brief   SGC BLE GATT service implementation.
 */

#include "sgc_service.h"
#include "../state_machine/state_machine.h"
#include <ArduinoBLE.h>

/* ================================================================== */
/*  GATT service and characteristics                                    */
/* ================================================================== */
static BLEService svc("53470000-0000-1000-8000-00805F9B34FB");
static BLEByteCharacteristic char_state("5347ABC4-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);
static BLEByteCharacteristic char_battery("5347ABC5-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);
static BLEByteCharacteristic char_transfer("5347ABCD-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);
static BLEByteCharacteristic char_cal("5347ABCE-0000-1000-8000-00805F9B34FB", BLERead | BLENotify);

/* ================================================================== */
/*  Public API                                                          */
/* ================================================================== */

void sgc_ble_init()
{
    svc.addCharacteristic(char_state);
    svc.addCharacteristic(char_battery);
    svc.addCharacteristic(char_transfer);
    svc.addCharacteristic(char_cal);
    BLE.addService(svc);
    BLE.setLocalName("SGC");
    BLE.setAdvertisedService(svc);
}

void sgc_ble_update_state(DeviceState s)
{
    switch (s) {
    case DeviceState::SLEEP:
        BLE.stopAdvertise(); break;
    case DeviceState::IDLE:
    case DeviceState::POST_RUN:
        BLE.advertise(); break;
    default:
        break;  // ARMED/LOGGING: leave advertising as-is
    }
    char_state.writeValue(static_cast<uint8_t>(s));
    char_transfer.writeValue(0);  /* reset transfer status */
}

void sgc_ble_poll()
{
    BLE.poll();
}

void sgc_ble_set_battery(uint8_t pct)
{
    char_battery.writeValue(pct);
}

void sgc_ble_set_cal(uint8_t cal)
{
    char_cal.writeValue(cal);
}

void sgc_ble_set_transfer(uint8_t status)
{
    char_transfer.writeValue(status);
}
