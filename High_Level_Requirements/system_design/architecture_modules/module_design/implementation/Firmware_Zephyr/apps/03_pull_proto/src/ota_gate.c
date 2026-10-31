/*
 * ota_gate.c — REQ-OTA constraint: SMP transfers only in SLEEP.
 * ARMED/LOGGING/POST_RUN guard the acquisition path — an image swap
 * while recording would kill a run mid-descent. Deny-all mgmt ops
 * while the recorder is outside SLEEP (returns MGMT_ERR_EBUSY to the
 * uploader; nRF Connect shows "busy").
 */
#include <zephyr/kernel.h>
#include <mgmt/mgmt.h>
#include <mgmt/callback.h>
#include "recorder.h"

static struct mgmt_callback gate_cb;

static int gate_handler(uint32_t event, int32_t rc, bool *abort_more,
			void *data, size_t data_size)
{
	ARG_UNUSED(event);
	ARG_UNUSED(rc);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);

	if (recorder_state_idx() != 0u) {   /* 0 = SLEEP */
		*abort_more = true;
		return MGMT_ERR_EBUSY;
	}
	return MGMT_ERR_EOK;
}

void ota_gate_init(void)
{
	gate_cb.callback = gate_handler;
	gate_cb.event_mask = MGMT_EVT_OP_ALL;
	mgmt_callback_register(&gate_cb);
}
