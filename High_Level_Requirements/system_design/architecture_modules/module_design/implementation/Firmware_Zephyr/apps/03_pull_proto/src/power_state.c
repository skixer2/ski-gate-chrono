/*
 * power_state.c — SLEEP low-power actuator (F12/F13, v0.23.0)
 * ===========================================================
 * System-ON SLEEP (the primary waiting state, ADR-004): RAM retained,
 * LFCLK/RTC running, BLE advertising at the slow 2 s interval, sensor
 * hub parked, SoC in tickless idle (Zephyr default; main() parks in
 * k_sleep(K_FOREVER) — no busy loops). Wake: piezo button press →
 * ARMED in < 100 ms, no reboot, no re-init (the GPIO edge interrupt
 * stays armed in System ON).
 *
 * After 1 h of UNCONNECTED SLEEP → System OFF (F12): full shutdown —
 * no radio, no serial. The ONLY wake source is the piezo button
 * (P0.02) via the GPIO SENSE/DETECT mechanism, which is the one
 * piece of pin config that survives System OFF. Wake = clean cold
 * boot; power_state_init() prints rr:4 to mark it (F13).
 *
 * Sealed System-Off line (F12 verification), serial only — the device
 * is unconnected by definition, there is nothing to notify:
 *   {"ev":"sm","from":"SLEEP","to":"SYSTEM_OFF"}
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/drivers/hwinfo.h>

#include "power_state.h"
#include "ble_lifecycle.h"
#include "recorder.h"
#include "pull_proto.h"
#include "button.h"
#include "sensor_prod.h"

/* F12, verbatim from the sealed requirement: SLEEP_SYSTEM_OFF_MS. */
#define SLEEP_SYSTEM_OFF_MS 3600000u

static struct k_timer off_timer;
static struct k_work  off_work;

static void off_work_handler(struct k_work *w)
{
	/* Guards (F12/F13 + design item 7). A trip here leaves the
	 * system ON; the state hooks own the timer and will re-arm (or
	 * cancel) it on the next transition. */
	if (ble_lifecycle_connected()) {
		return;   /* connected ⇒ no System Off, ever (V5.00) */
	}
	if (recorder_state_idx() != 0u) {   /* 0 = SLEEP */
		return;   /* ARMED/LOGGING/POST_RUN never go dark */
	}
	if (pull_proto_busy()) {
		/* Stream/directory in flight (defence in depth: both
		 * abort on disconnect anyway) — fresh full window. */
		k_timer_start(&off_timer, K_MSEC(SLEEP_SYSTEM_OFF_MS),
			      K_NO_WAIT);
		return;
	}

	/* Arm the ONLY wake source before going dark: piezo button
	 * P0.02, level (SENSE) low — same recipe as the Zephyr
	 * samples/boards/nordic/system_off GPIO wake ("interrupt as
	 * level active to allow wake-up"); this button is active-low. */
	button_arm_systemoff_wake();
	printk("{\"ev\":\"sm\",\"from\":\"SLEEP\",\"to\":\"SYSTEM_OFF\"}\n");
	sys_poweroff();   /* FUNC_NORETURN — button press = cold boot */
}

static void off_timer_fn(struct k_timer *t)
{
	k_work_submit(&off_work);
}

void power_state_on_sleep_enter(void)
{
	ble_lifecycle_set_adv_slow(true);   /* F12: 2 s while sleeping */
	ble_lifecycle_resume();             /* undo arm suspension; boot no-op */
	sensor_prod_suspend();              /* hub 0 Hz + drain thread parked */
	if (!ble_lifecycle_connected()) {
		k_timer_start(&off_timer, K_MSEC(SLEEP_SYSTEM_OFF_MS),
			      K_NO_WAIT);
	}
}

void power_state_on_sleep_exit(void)
{
	k_timer_stop(&off_timer);
	ble_lifecycle_set_adv_slow(false);
	sensor_prod_resume();
}

void power_state_on_connect(void)
{
	k_timer_stop(&off_timer);   /* a phone holds SLEEP indefinitely */
}

void power_state_on_disconnect(void)
{
	if (recorder_state_idx() == 0u) {   /* SLEEP */
		k_timer_start(&off_timer, K_MSEC(SLEEP_SYSTEM_OFF_MS),
			      K_NO_WAIT);   /* fresh 1 h window (F12) */
	}
}

void power_state_init(void)
{
	k_timer_init(&off_timer, off_timer_fn, NULL);
	k_work_init(&off_work, off_work_handler);

	/* F13 rr line: RESET_LOW_POWER_WAKE is the nRF RESETREAS.OFF bit
	 * — System-Off wake by GPIO SENSE. Decode to the reset-reason
	 * codes (bit indices): 4 = OFF (the sealed F13 rr:4), 0 = pin,
	 * 1 = watchdog, 2 = soft request, 0xFF = other/unknown. */
	uint32_t cause = 0;

	if (hwinfo_get_reset_cause(&cause) == 0) {
		uint8_t rr = 0xFFu;

		if (cause & RESET_LOW_POWER_WAKE) {
			rr = 4u;
		} else if (cause & RESET_WATCHDOG) {
			rr = 1u;
		} else if (cause & RESET_SOFTWARE) {
			rr = 2u;
		} else if (cause & RESET_PIN) {
			rr = 0u;
		}
		printk("{\"ev\":\"boot\",\"rr\":%u}\n", (unsigned)rr);
	}
	hwinfo_clear_reset_cause();   /* fresh window for the next boot */
}
