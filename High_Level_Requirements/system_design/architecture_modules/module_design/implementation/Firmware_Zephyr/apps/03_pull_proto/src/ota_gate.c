/*
 * ota_gate.c — REQ-OTA constraint: SMP transfers only in SLEEP.
 * ARMED/LOGGING/POST_RUN guard the acquisition path — an image swap
 * while recording would kill a run mid-descent. Denies every mgmt
 * command while the recorder is outside SLEEP: uploader sees
 * MGMT_ERR_EBUSY ("busy").
 *
 * API per zephyr/include/zephyr/mgmt/mcumgr/mgmt/callbacks.h (NCS 3.4):
 * 7-param handler, enum mgmt_cb_return, event_id (not mask),
 * MGMT_EVT_OP_CMD_RECV = every SMP command received.
 */
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include "recorder.h"

static struct mgmt_callback gate_cb;

static enum mgmt_cb_return gate_handler(uint32_t event,
					enum mgmt_cb_return prev_status,
					int32_t *rc, uint16_t *group,
					bool *abort_more, void *data,
					size_t data_size)
{
	ARG_UNUSED(event);
	ARG_UNUSED(prev_status);
	ARG_UNUSED(group);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);

	if (recorder_state_idx() != 0u) {   /* 0 = SLEEP */
		*abort_more = true;
		*rc = MGMT_ERR_EBUSY;
		return MGMT_CB_ERROR_RC;
	}
	return MGMT_CB_OK;
}

void ota_gate_init(void)
{
	gate_cb.callback = gate_handler;
	gate_cb.event_id = MGMT_EVT_OP_CMD_RECV;
	mgmt_callback_register(&gate_cb);
}
