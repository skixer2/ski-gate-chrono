/**
 * @file    sgc_service.h
 * @brief   SGC BLE GATT service — characteristic management.
 *
 * UUIDs:
 *   Service:  53470000-0000-1000-8000-00805F9B34FB
 *   State:    5347ABC4  (BLERead | BLENotify)
 *   Battery:  5347ABC5  (BLERead | BLENotify)
 *   Run Count: 5347ABC6  (BLERead | BLENotify)
 *   Transfer: 5347ABCD  (BLERead | BLENotify)
 *   Cal:      5347ABCE  (BLERead | BLENotify)
 */

#pragma once

#include <stdint.h>

// Forward-declare from state_machine
enum class DeviceState : uint8_t;

/**
 * @brief Initialize BLE service, add characteristics, start advertising.
 *        Must be called once in setup(), after BLE.begin().
 */
void sgc_ble_init();

/**
 * @brief Update BLE advertising and characteristics on state change.
 *        SLEEP → stop advertising. IDLE/POST_RUN → start advertising.
 *        Writes state and resets transfer status to 0.
 */
void sgc_ble_update_state(DeviceState s);

/** @brief Poll BLE stack — call every loop iteration. */
void sgc_ble_poll();

/** @brief Notify battery percentage (0-100). */
void sgc_ble_set_battery(uint8_t pct);

/** @brief Notify calibration accuracy (0-3, or 0 if unavailable). */
void sgc_ble_set_cal(uint8_t cal);

/** @brief Notify run count (total runs stored on flash). */
void sgc_ble_set_run_count(uint16_t count);

/** @brief Notify transfer status (0=idle, 1=transferring, 2=done). */
void sgc_ble_set_transfer(uint8_t status);
