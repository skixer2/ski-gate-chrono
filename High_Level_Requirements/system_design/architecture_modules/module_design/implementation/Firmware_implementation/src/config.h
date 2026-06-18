/**
 * @file    config.h
 * @brief   SGC timing constants and thresholds.
 */

#pragma once

#include <stdint.h>

/* --- State timeouts (milliseconds) --- */
static constexpr uint32_t SLEEP_TIMEOUT_MS       = 120000;  /* 2 min idle → sleep */
static constexpr uint32_t ARM_TIMEOUT_MS         = 30000;   /* 30 s armed → idle  */
static constexpr uint32_t POST_RUN_COOLDOWN_MS   = 10000;   /* 10 s before re-arm  */

/* --- Detector thresholds --- */
static constexpr float    SPEED_THRESHOLD_MPS     = 5.0f;    /* min speed for start detect */
static constexpr float    DROP_THRESHOLD_M        = 1.5f;    /* min baro drop for descent  */
static constexpr uint16_t FLATLINE_SAMPLES        = 10;      /* consecutive still frames  */
static constexpr uint32_t FACTORY_RESET_MS        = 5000;    /* proximity hold duration   */

/* --- Sensor feed rates --- */
static constexpr uint32_t BARO_FEED_PERIOD_MS     = 100;     /* 10 Hz baro to detectors  */
static constexpr uint32_t BATTERY_PERIOD_MS       = 30000;   /* 30 s battery refresh     */
