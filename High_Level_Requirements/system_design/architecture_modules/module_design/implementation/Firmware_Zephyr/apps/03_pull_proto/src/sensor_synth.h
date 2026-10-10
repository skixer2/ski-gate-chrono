/*
 * sensor_synth.h — BENCH synthetic FRAME runs through the real pipeline
 * (v0.20.0 producer-merge; JP 2026-10-10: "we will need to create
 * realistic runs to check the pole and turn detection").
 *
 * 'S <secs> <gate_interval_s>' drives the recorder with generated
 * sensor frames instead of BHI260 snapshots: BitPacker → ring/store →
 * phone download is byte-identical to a real run. Pole impacts land at
 * exact known timestamps; serial lines carry the ground truth as it is
 * placed into the data.
 */
#ifndef SGC_SENSOR_SYNTH_H
#define SGC_SENSOR_SYNTH_H

#include <stdbool.h>
#include <stdint.h>
#include "bitpacker.h"   /* struct sgc_raw_frame */

/* Arm + run + close a synthetic run. Returns false if the recorder is
 * busy. Completion is asynchronous (serial shows the lifecycle). */
bool sensor_synth_start(uint32_t secs, uint32_t gate_interval_s);

/* Frame source override, called from sensor_prod_get_frame() context
 * (recorder workqueue). false = no synth session active. */
bool sensor_synth_get_frame(struct sgc_raw_frame *f);

#endif /* SGC_SENSOR_SYNTH_H */
