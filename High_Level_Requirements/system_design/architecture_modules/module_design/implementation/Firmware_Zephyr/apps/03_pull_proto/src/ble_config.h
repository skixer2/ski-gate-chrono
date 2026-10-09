/*
 * ble_config.h — SGC device-config chars (v0.18.0, error-tolerance audit B3)
 * =====================================================================
 * Implements the ArduinoFW 5.x char set the Zephyr port was missing
 * (audit 2026-10-09: the app fabricated defaults for these):
 *   ABC1 Device Name (R/W, ≤20 B)   ABC4 State+Sensors (R)
 *   ABC2 Arm Side (R/W, 0|1)        ABC5 Battery+Charging (R)
 *   ABC3 Discipline (R/W, 0..3)     ABC7 Flash Used % (R)
 *                                   ABD0 Cal Accuracy (R)
 *
 * Persistence: sector 0x1FC000 (LOG_END — reserved since SGL2, the SAME
 * address ArduinoFW used), struct byte-identical to ArduinoFW FlashConfig
 * so a device upgraded from ArduinoFW keeps its saved config.
 *
 * HONESTY RULES (JP directive 2026-10-06):
 *   - battery unknown  -> 0x7F (app renders "unknown"), never a fake 0%
 *   - battery-mode reads skipped until the CD pin port (wrong-pin write
 *     could disable charging — documented, not guessed)
 *   - cal accuracy -> 0 + TODO (BHI calibration status port)
 */
#ifndef SGC_BLE_CONFIG_H
#define SGC_BLE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* Load config from flash + start the battery poller. Call once at boot,
 * after store_init() (shares the flash device) — order vs BLE bring-up
 * does not matter (reads serve RAM cache). */
void ble_config_start(void);

/* ── Config (RAM cache, persisted async) ──────────────────────────── */
const char *ble_config_dev_name(void);   /* NUL-terminated, ≤20 chars */
uint8_t ble_config_arm_side(void);       /* 0=left 1=right            */
uint8_t ble_config_discipline(void);     /* 0..3                      */

/* Setters sanitize, update RAM, and schedule an erase+write on the
 * system workqueue (NEVER in BT RX context — 4 KiB erase ≈ 90 ms).
 * Rapid writes coalesce (one pending flush keeps the newest values). */
void ble_config_set_dev_name(const char *name, uint8_t len);
void ble_config_set_arm_side(uint8_t v);
void ble_config_set_discipline(uint8_t v);

/* ── Status surfaces ──────────────────────────────────────────────── */
/* ABC5: bit7 = charging (0 — TODO: STATUS CHG_STAT port), bits6-0 = %,
 * 0x7F = unknown (no battery / I2C error / battery-mode read skipped). */
uint8_t ble_config_battery_byte(void);

/* ABC4: bits4-0 = recorder state (IDLE=0 ARMED=1 LOGGING=2 POST_RUN=3;
 * ArduinoFW mapped its waiting state to 0xFF — we have no SLEEP yet,
 * IDLE reports 0, documented), bits7-5 = sensor flags (0 — TODO BHI). */
uint8_t ble_config_state_byte(void);

/* ABC7: % of LOG region occupied by live runs (logical bytes). */
uint8_t ble_config_flash_used_pct(void);

/* ABD0: 0 until the BHI calibration-status port. */
uint8_t ble_config_cal(void);

#endif /* SGC_BLE_CONFIG_H */
