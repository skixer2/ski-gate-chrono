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
 * business while acquiring. Resume re-advertises when back to IDLE. */
void ble_lifecycle_suspend(void);
void ble_lifecycle_resume(void);

#endif /* SGC_BLE_LIFECYCLE_H */
