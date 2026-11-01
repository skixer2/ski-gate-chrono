/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.24.1"   /* OTA upload test build: image version aligned for MCUboot swap (+VERSION file drives imgtool sign). All v0.24.0 features: ABC6 fw char, charger self-heal, LED-off-before-SystemOff, VIN no-SysOff, serial y batt debug, connected-aware batt poll */

#endif /* SGC_APP_VERSION_H */
