/*
 * SGC Zephyr — App 03: pull-protocol product app (v0.20.0)
 * ==================================================================
 * Wiring only (step 3c split; v0.20.0 producer-merge cleanup removed
 * the retired scaffolding — bp_ref oracle, boot self-test, demo_svc,
 * store_fake):
 *
 *   store.h + store_flash.c  run blobs (byte-exact ArduinoFW layout)
 *   led_state.c              LED as link state (own 1 Hz tick)
 *   ble_lifecycle.c          advertising + conn callbacks + restart
 *   pull_proto.c             0x5347 product service: D/s commands,
 *                            binary frames, END marker, error paths
 *   recorder.c               ARMED/LOGGING/POST_RUN machine + baro-drop
 *   sensor_prod.c            BHI260 snapshots (recorder feed)
 *   sensor_synth.c           bench: synthetic FRAME runs ('S') through
 *                            the identical pipeline (pole/turn truth)
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
#include "button.h"
#include "ble_lifecycle.h"
#include "pull_proto.h"

#define APP_VERSION "0.21.0"

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
	button_init();     /* F03: press-to-arm (P0.02, 20 ms debounce) */
	pull_proto_start();
	ble_lifecycle_start();

	printk("init complete, entering idle\n");
	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
