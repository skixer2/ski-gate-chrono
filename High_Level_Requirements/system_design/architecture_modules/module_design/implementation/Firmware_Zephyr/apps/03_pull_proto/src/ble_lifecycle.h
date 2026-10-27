/*
 * ble_lifecycle.h — advertising + connection lifecycle (step 3c)
 */
#ifndef SGC_BLE_LIFECYCLE_H
#define SGC_BLE_LIFECYCLE_H

#include <stdbool.h>

/* bt_enable + first advertising. Async: readiness callback prints. */
void ble_lifecycle_start(void);

/* Link state (written by the conn callbacks, read everywhere). */
bool ble_lifecycle_connected(void);

/* Recording mode (JP 2026-09-29): ARM tears the link down — no phone
 * business while acquiring. Resume re-advertises when back to SLEEP. */
void ble_lifecycle_suspend(void);
void ble_lifecycle_resume(void);

/* F12 advertising mode: slow 2 s interval in SLEEP, fast when active.
 * Safe to call before ble_lifecycle_start() (boot sets slow — the
 * first ad set is then born slow); live-restarts the ad set when it
 * is up and the stack is ready. */
void ble_lifecycle_set_adv_slow(bool slow);

#endif /* SGC_BLE_LIFECYCLE_H */
