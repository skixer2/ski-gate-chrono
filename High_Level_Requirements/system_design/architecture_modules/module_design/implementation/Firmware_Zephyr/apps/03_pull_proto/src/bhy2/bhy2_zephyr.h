/*
 * bhy2_zephyr.h — public surface of the Zephyr BHI260 host shim.
 */
#ifndef SGC_BHY2_ZEPHYR_H
#define SGC_BHY2_ZEPHYR_H

#include <stdbool.h>
#include "bhy2.h"

/* Full bring-up: SPI shim init → soft reset → boot_from_flash.
 * Returns true when boot_status indicates hub running (0x13 expected). */
bool bhy2_bringup(void);

/* Device handle for the vendored bhy2 C API (virt-sensor config, FIFO).
 * NULL until bhy2_bringup() succeeds. */
struct bhy2_dev *bhy2_zephyr_dev(void);

#endif /* SGC_BHY2_ZEPHYR_H */
