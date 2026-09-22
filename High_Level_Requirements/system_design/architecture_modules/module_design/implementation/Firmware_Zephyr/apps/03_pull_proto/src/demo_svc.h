/*
 * demo_svc.h — 8d5a demo service: counter + button (step 3c)
 */
#ifndef SGC_DEMO_SVC_H
#define SGC_DEMO_SVC_H

/* Init button GPIO + the 1 Hz counter tick. Returns 0 or -1 on button
 * setup failure (main aborts). */
int demo_svc_start(void);

#endif /* SGC_DEMO_SVC_H */
