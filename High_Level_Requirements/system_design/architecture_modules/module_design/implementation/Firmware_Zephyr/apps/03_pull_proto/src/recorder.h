/*
 * recorder.h — M3 step 1: recording state machine interface
 */
#ifndef SGC_RECORDER_H
#define SGC_RECORDER_H

void recorder_init(void);
void recorder_arm(void);      /* 'a' — IDLE → ARMED (120 s timeout)   */
void recorder_start(void);    /* 'g' — ARMED → LOGGING (synthetic;
			       *   production: 2.0 m drop detector)     */
void recorder_end(void);      /* 'e' — LOGGING → POST_RUN (synthetic;
			       *   production: 5 s flatline < 2 m)      */
const char *recorder_state_str(void);   /* "IDLE|ARMED|LOGGING|POST_RUN" */

#endif /* SGC_RECORDER_H */
