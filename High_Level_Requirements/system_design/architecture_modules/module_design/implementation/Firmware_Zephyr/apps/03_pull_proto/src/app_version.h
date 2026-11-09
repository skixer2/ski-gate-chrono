/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.25.7"   /* piezo arming rework (JP 2026-11-04/05): 1s hold gate removed, single press arms in SLEEP, other states reject, 200ms quiet window. v6.1 F42-compliant (button = arm+wake only) */

#endif /* SGC_APP_VERSION_H */
