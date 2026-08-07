/**
 * @file    config.h
 * @brief   SGC timing constants and thresholds.
 */

#pragma once

#include <stdint.h>

#define FW_VERSION "4.68"

/* --- SK6812 strip / bench (strip hardware NOT required) ---
 * LED_STRIP_COUNT 0   = onboard Nicla RGB only
 * LED_STRIP_COUNT 5   = design length (full-chain show cost)
 * LED_STRIP_COUNT 10  = stress
 * LED_STRIP_PIN   0   = timing-only (IRQ-mask + delays, no GPIO)
 * LED_STRIP_PIN  12   = Nicla ESLOV INT = P0.19 (production LED_STRIP net)
 *                       Real digitalWrite NZR bit-bang — open pin OK, no strip needed
 * Override: -DLED_STRIP_COUNT=5 -DLED_STRIP_PIN=12
 */
#ifndef LED_STRIP_COUNT
#define LED_STRIP_COUNT 5
#endif
#ifndef LED_STRIP_PIN
/* Default: real GPIO on ESLOV INT (P0.19) — measures full digitalWrite path */
#define LED_STRIP_PIN 12
#endif

/* --- State timeouts (milliseconds) --- */
static constexpr uint32_t SLEEP_TIMEOUT_MS       = 120000;  /* 2 min idle → sleep */
static constexpr uint32_t ARM_TIMEOUT_MS         = 30000;   /* 30 s armed → idle  */
static constexpr uint32_t POST_RUN_COOLDOWN_MS   = 10000;   /* 10 s before re-arm  */
static constexpr uint32_t MAX_LOG_DURATION_MS    = 150000;  /* 150 s log limit → POST_RUN (overridable via BLE) */

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
