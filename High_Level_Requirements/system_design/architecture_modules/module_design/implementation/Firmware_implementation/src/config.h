/**
 * @file    config.h
 * @brief   SGC timing constants and thresholds.
 */

#pragma once

#include <stdint.h>

#define FW_VERSION "5.35"

/* --- SK6812 strip / bench (strip hardware NOT required) ---
 * LED_STRIP_COUNT 0   = onboard Nicla RGB only
 * LED_STRIP_COUNT 5   = design length (tagged v4.71-best-s04 @ ~99.5 fps)
 * LED_STRIP_COUNT 10  = stress / alternate BOM length
 * LED_STRIP_PIN   0   = timing-only (IRQ-mask + delays, no GPIO)
 * LED_STRIP_PIN  12   = Nicla ESLOV INT = P0.19 (production LED_STRIP net)
 *                       Real NZR bit-bang — open pin OK, no strip needed
 * Override: -DLED_STRIP_COUNT=5 -DLED_STRIP_PIN=12
 *
 * Baseline tag: v4.71-best-s04 (5 LEDs). v4.72 defaults to 10 for stress.
 */
#ifndef LED_STRIP_COUNT
#define LED_STRIP_COUNT 10
#endif
#ifndef LED_STRIP_PIN
/* Default: real GPIO on ESLOV INT (P0.19) — measures full digitalWrite path */
#define LED_STRIP_PIN 12
#endif

/* --- State timeouts (milliseconds) --- */

/* V5.08: Hardware watchdog timer — independent of CPU. If loop() blocks
   >5s (e.g. writeValue stuck inside Cordio HCI), WDT reboots the device.
   This is the ONLY recovery path when the main loop is stuck inside a
   blocking call. Software watchdogs (FT stall, zombie) can't fire because
   they run inside loop(). WDT runs in hardware, independent of CPU.
   Timeout = 5s: safe for flash erase (~2-3s/sector) and BLE radio_restart
   (~100–400 ms with V5.15 settle+retry; still << 5s). */
static constexpr uint32_t WDT_TIMEOUT_MS = 5000;
//static constexpr uint32_t SLEEP_SYSTEM_OFF_MS    = 3600000; /* 1 h sleep → System Off */
static constexpr uint32_t SLEEP_SYSTEM_OFF_MS    = 30000;   // 30 s — JP test override
// TODO: restore to 3600000 (1 h) after System Off wake testing
static constexpr uint32_t ARM_TIMEOUT_MS         = 30000;   /* 30 s armed → sleep */
static constexpr uint32_t POST_RUN_COOLDOWN_MS   = 10000;   /* 10 s before sleep */
static constexpr uint32_t MAX_LOG_DURATION_MS    = 150000;  /* 150 s log limit → POST_RUN (overridable via BLE) */
/* BLE FT watchdog: abort if no progress for this long (phone gone / wedge). */
static constexpr uint32_t FT_STALL_TIMEOUT_MS    = 8000;   // 8s — faster abort on stuck FT

/* Zombie BLE link: 30s no BLE activity (GATT write / connect / FT chunk) = dead phone. */
static constexpr uint32_t BLE_ZOMBIE_TIMEOUT_MS  = 30000;

/* --- Detector thresholds (per design spec) --- */
static constexpr float    SPEED_THRESHOLD_MPS     = 1.5f;    /* m/s for 200ms window */
static constexpr float    DROP_THRESHOLD_M        = 2.0f;    /* meters from arming P₀ */
static constexpr uint16_t QUIET_FRAMES            = 1000;    /* 10s at 100 Hz for end det */
static constexpr uint32_t FACTORY_RESET_MS        = 20000;   /* proximity hold duration   */

/* --- LDC1612 proximity sensor --- */
static constexpr uint32_t LDC_ARM_HOLD_MS           = 1000;    /* F03: continuous prox → arm   */
static constexpr uint32_t LDC_FACTORY_HOLD_MS       = 20000;   /* F42: continuous prox → reset */
static constexpr uint32_t LDC_TICK_PERIOD_MS        = 20;      /* poll interval                */

/* --- Sensor feed rates --- */
static constexpr uint32_t BARO_FEED_PERIOD_MS     = 100;     /* 10 Hz baro to detectors  */
static constexpr uint32_t BATTERY_PERIOD_MS       = 30000;   /* 30 s battery refresh     */
