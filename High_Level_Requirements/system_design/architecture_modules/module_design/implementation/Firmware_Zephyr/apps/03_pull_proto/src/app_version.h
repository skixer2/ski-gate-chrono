/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.23.8"   /* +ABC6 fw-version char (JP 21:36: 'have the device firmware version displayed when connected'); +charger self-heal+restore-retry (paused-FAST_CHG hole found on bench 2026-10-28); LED all-off before System Off (IS31 latch); VIN present -> no System Off (JP GO); serial y battery debug; connected-aware battery poll 5s/30s; on-battery battery % read */

#endif /* SGC_APP_VERSION_H */
