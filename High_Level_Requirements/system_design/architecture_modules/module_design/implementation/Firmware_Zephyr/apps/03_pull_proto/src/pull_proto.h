/*
 * pull_proto.h — product console service + pull protocol (step 3c)
 */
#ifndef SGC_PULL_PROTO_H
#define SGC_PULL_PROTO_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>

/* Init work items (call before ble_lifecycle_start). */
void pull_proto_start(void);

/* Called by ble_lifecycle on disconnect: abort any stream, clear the
 * request slot (stateless design — no session survives a link). */
void pull_proto_on_disconnect(void);

/* Advertisement payloads (defined here because the product service
 * UUID is the single source of truth in pull_proto.c — the ADV packet
 * names it, setAdvertisedService parity). */
const struct bt_data *pull_proto_ad(void);
const struct bt_data *pull_proto_sd(void);
uint8_t pull_proto_ad_len(void);
uint8_t pull_proto_sd_len(void);

#endif /* SGC_PULL_PROTO_H */
