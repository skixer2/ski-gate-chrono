/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.24.7"   /* +serial W stack watermarks (DS plan) + MCUmgr reset 100ms (double-tap quirk). Doubles as OTA loop image (alternate 0.24.6/0.24.7) */

#endif /* SGC_APP_VERSION_H */
