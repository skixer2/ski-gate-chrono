/*
 * led_state.h — LED as link-state indicator (step 3c)
 */
#ifndef SGC_LED_STATE_H
#define SGC_LED_STATE_H

/* Init the IS31FL3194-backed RGB LED + start the pattern timer.
 * F41 (JP 2026-11-22): states are encoded by FLASH RHYTHM, not steady
 * colour. The render timer's period is the current state's half-period,
 * so it is re-timed on every state change (armed 1 Hz 50 %, logging
 * 4 Hz, error 8 Hz, sleep 100 ms / 2 s, ...).
 * Returns 0, or -1 if the LED device isn't ready (main aborts). */
int led_state_init(void);

/* 0.23.5: clear the IS31 (all channels dark) + stop the render
 * timers — call right before sys_poweroff(): the LED chip latches
 * its last state when the SoC dies (solid-blue bench bug). */
void led_state_all_off(void);

#endif /* SGC_LED_STATE_H */
