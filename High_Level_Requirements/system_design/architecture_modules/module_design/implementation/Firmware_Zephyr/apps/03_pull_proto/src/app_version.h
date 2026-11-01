/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.24.5"   /* +DS transport design: MTU 300, NETBUF 297 (image-list 252-262B vs 244 ceiling = 8-16B overshoot). RAM gate: <=58150B */

#endif /* SGC_APP_VERSION_H */
