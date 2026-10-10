/*
 * SGC Zephyr — App 03: pull-protocol product app (v0.5.0, step 3c)
 * ==================================================================
 * Step 3c split main.c (~700 dense lines) into modules; this file is
 * now WIRING ONLY:
 *
 *   store.h + store_fake.c   run blobs (byte-exact ArduinoFW layout);
 *                            the flash port later implements the same
 *                            header and swaps one CMake line
 *   led_state.c              LED as link state (own 1 Hz tick)
 *   ble_lifecycle.c          advertising + conn callbacks + restart
 *   pull_proto.c             0x5347 product service: D/s commands,
 *                            binary frames, END marker, error paths
 *   demo_svc.c               8d5a counter/button canary (app-02 heir)
 *
 * Bench equivalence for 3c = one App-1.46 round: D + both runs + CRC
 * (nothing behavioral changed; only ownership moved).
 *
 * The SGC concurrency pattern holds everywhere: ISR/callback context
 * only SUBMITS work; real work runs from the sysworkq; main() idles.
 */
#include <zephyr/kernel.h>
#include "store.h"
#include "recorder.h"
#include "ble_config.h"

/* M3 Phase A: BHI260AP hub bring-up; Phase B1: real sensor producer. */
#include "bhy2/bhy2_zephyr.h"
#include "sensor_prod.h"
#include "led_state.h"
#include "ble_lifecycle.h"
#include "pull_proto.h"
#include "demo_svc.h"

#define APP_VERSION "0.19.1"

int main(void)
{
	printk("SGC app-03 pull_proto v%s\n", APP_VERSION);

	/* Order matters: store data first (listings + streams read it),
	 * then peripherals, then BT last (callbacks can fire from
	 * bt_enable onward — everything they touch must exist). */
	store_init();
	ble_config_start();   /* v0.18.0: config chars + battery poller */
	recorder_init();
	if (bhy2_bringup()) {
		sensor_prod_start();
	}

	if (led_state_init() != 0) {
		return 0;
	}
	if (demo_svc_start() != 0) {
		return 0;
	}
	pull_proto_start();
	ble_lifecycle_start();

	printk("init complete, entering idle\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
