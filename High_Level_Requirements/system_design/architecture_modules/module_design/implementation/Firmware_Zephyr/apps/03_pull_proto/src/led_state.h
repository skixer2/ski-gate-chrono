/*
 * led_state.h — LED as link-state indicator (step 3c)
 */
#ifndef SGC_LED_STATE_H
#define SGC_LED_STATE_H

/* Init the IS31FL3194-backed RGB LED + start the 1 Hz render tick.
 * Returns 0, or -1 if the LED device isn't ready (main aborts). */
int led_state_init(void);

#endif /* SGC_LED_STATE_H */
