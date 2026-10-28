/*
 * led_state.h — LED as link-state indicator (step 3c)
 */
#ifndef SGC_LED_STATE_H
#define SGC_LED_STATE_H

/* Init the IS31FL3194-backed RGB LED + start the 1 Hz render tick.
 * Returns 0, or -1 if the LED device isn't ready (main aborts). */
int led_state_init(void);

/* 0.23.5: clear the IS31 (all channels dark) + stop the render
 * timers — call right before sys_poweroff(): the LED chip latches
 * its last state when the SoC dies (solid-blue bench bug). */
void led_state_all_off(void);

#endif /* SGC_LED_STATE_H */
