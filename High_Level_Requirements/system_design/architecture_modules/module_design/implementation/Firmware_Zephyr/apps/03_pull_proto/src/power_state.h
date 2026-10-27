/*
 * power_state.h — SLEEP low-power actuator (F12/F13, v0.23.0)
 * ===========================================================
 * The recorder owns the athlete-visible machine (SLEEP/ARMED/LOGGING/
 * POST_RUN); this module owns what SLEEP MEANS physically:
 * slow 2 s advertising, sensor-hub suspension, and the 1 h
 * unconnected System-Off window with the button as wake source.
 */
#ifndef SGC_POWER_STATE_H
#define SGC_POWER_STATE_H

/* Boot bring-up: System-Off timer + reset-reason line. Prints
 * {"ev":"boot","rr":N} — rr=4 means the previous shutdown was System
 * Off and THIS boot is a button-sense cold boot (F13). */
void power_state_init(void);

/* Actions on every SLEEP entry (boot, POST_RUN completion, arm
 * timeout): slow advertising mode, sensor suspension, open a fresh
 * 1 h System-Off window (unless a phone is connected). */
void power_state_on_sleep_enter(void);

/* Actions on leaving SLEEP (button/serial arm intent): close the
 * System-Off window, fast advertising mode, sensors back at 100 Hz. */
void power_state_on_sleep_exit(void);

/* Connection hooks (called from ble_lifecycle callbacks): a phone
 * connection cancels the System-Off window — connected holds SLEEP
 * indefinitely (V5.00); the disconnect opens a FRESH 1 h window when
 * in SLEEP (F12: the timer resets on disconnect→connect cycles). */
void power_state_on_connect(void);
void power_state_on_disconnect(void);

#endif /* SGC_POWER_STATE_H */
