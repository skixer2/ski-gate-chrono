/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.25.8"   /* version-source sync: VERSION file (drives the MCUboot-signed image version) bumped to match the banner — was drifting (file 0.24.7 vs banner 0.25.7, so phone-OTA would have reported 0.24.7). Harness end-to-end test build. */

#endif /* SGC_APP_VERSION_H */
