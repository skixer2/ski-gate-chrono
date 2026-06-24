/**
 * @file    sgc_service.h
 * @brief   SGC BLE GATT service — 14 characteristics (proven working set).
 *
 * Packed encoding:
 *   ABC4 bits 7-5 = sensor flags, bits 4-0 = state
 *   ABC5 bit 7 = charging, bits 6-0 = battery %
 *   ABC8 = run_count[2] + oldest_age[4] (6 bytes)
 */

#pragma once
#include <stdint.h>
enum class DeviceState : uint8_t;

void sgc_ble_init();
void sgc_ble_update_state(DeviceState s);
void sgc_ble_poll();

void sgc_ble_set_battery(uint8_t pct);
void sgc_ble_set_cal(uint8_t cal);
void sgc_ble_set_run_count(uint16_t count);
void sgc_ble_set_transfer(uint8_t status);
void sgc_ble_set_flash_used(uint8_t pct);
void sgc_ble_set_charging(uint8_t status);
void sgc_ble_set_sensor_status(uint8_t bitfield);

void sgc_ble_config_load();
void sgc_ble_config_save();
const char* sgc_ble_get_device_name();
uint8_t     sgc_ble_get_arm_side();
uint8_t     sgc_ble_get_discipline();

void sgc_ble_ft_init();
void sgc_ble_ft_poll();
