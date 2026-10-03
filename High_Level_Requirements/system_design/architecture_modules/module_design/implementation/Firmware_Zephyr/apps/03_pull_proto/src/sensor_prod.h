/*
 * sensor_prod.h — Phase B step B1 (TC-2026-10-02-002):
 * real BHI260 virtual sensors → FIFO drain → per-sensor rate stats.
 *
 * Docs: Firmware_Zephyr/docs/phase_b_requirements.md
 * Decisions (JP 11:31): FIFO poll in a 10 ms tick; INT pin = fallback only.
 */
#ifndef SGC_SENSOR_PROD_H
#define SGC_SENSOR_PROD_H

#include <stdbool.h>

/* Call once after bhy2_bringup() returns true. Configures the four
 * ArduinoFW-parity virtual sensors, registers FIFO parse callbacks and
 * spawns the drain thread. Safe no-op if the hub is not up. */
void sensor_prod_start(void);

/* B3: latest-value snapshot for the recorder (hold-last semantics,
 * ArduinoFW parity). seqlock on the quaternion (single writer = drain
 * thread; reader = recorder workqueue). false until first RV frame. */
#include "bitpacker.h"
bool sensor_prod_get_frame(struct sgc_raw_frame *out);

#endif /* SGC_SENSOR_PROD_H */
