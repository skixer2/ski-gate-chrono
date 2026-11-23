/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.25.21"   /* 0.25.21: serial 'V' version query implemented (the header claimed a "serial V path" since 0.23.6 but no branch existed; harness got "E 4"). */

#endif /* SGC_APP_VERSION_H */
