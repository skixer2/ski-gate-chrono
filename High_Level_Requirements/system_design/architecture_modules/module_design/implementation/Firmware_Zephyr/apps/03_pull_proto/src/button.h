#ifndef SGC_BUTTON_H
#define SGC_BUTTON_H

/* F03: single press of the sealed piezo/monostable button arms the
 * device from SLEEP. Init after recorder_init(). */
int button_init(void);

/* F12/F13 System Off wake arming: reconfigures P0.02 to a level
 * (SENSE) input — the only pin config that survives System OFF.
 * Call immediately before sys_poweroff(). The wake is a clean cold
 * boot; button_init() re-arms the normal edge path on it. */
void button_arm_systemoff_wake(void);

#endif
