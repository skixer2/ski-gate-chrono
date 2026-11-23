/*
 * app_version.h — single source of truth for the firmware version
 * (JP doctrine: one define, everything else reads it). Read by the
 * boot banner (main.c), the serial 'V' path, and — 0.23.6 — the
 * ABC6 GATT characteristic so the phone app can display it.
 */
#ifndef SGC_APP_VERSION_H
#define SGC_APP_VERSION_H

#define APP_VERSION "0.25.20"   /* 0.25.20: F41 temporal LED pattern table (orange 1Hz ARMED, red 4Hz LOGGING, yellow 8Hz ERROR) - bumped because 0.25.19 shipped without the LED change reaching the image. */

#endif /* SGC_APP_VERSION_H */
