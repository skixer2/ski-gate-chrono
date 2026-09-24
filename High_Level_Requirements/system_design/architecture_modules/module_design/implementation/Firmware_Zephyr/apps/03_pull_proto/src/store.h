/*
 * store.h — SGC run-store interface (step 3c)
 * ============================================
 * One header, two present and future implementations:
 *
 *   store_fake.c  (now)   — synthesized blobs, zero payload RAM
 *   store_flash.c (later) — RawRunStore port on SPI flash
 *
 * Swapping = one line in CMakeLists.txt. Nothing else in the tree
 * changes: pull_proto asks ONLY through these functions.
 *
 * Blob layout (ArduinoFW-verbatim, DO NOT INVENT):
 *   [0..15]  packed RunHeader (v2: data_size/frame_count = 0 on disk)
 *   [16..]   payload
 *   [last 6] C3 32 magic + CRC32 LE — CRC over PAYLOAD ONLY,
 *            init 0xFFFFFFFF, final xor 0xFFFFFFFF
 */
#ifndef SGC_STORE_H
#define SGC_STORE_H

#include <stdint.h>

/* Boot-time init: fake store precomputes CRCs; flash store mounts. */
void store_init(void);

uint8_t  store_run_count(void);

/* Full blob size: 16 B header + payload + 6 B trailer. This is the `sz`
 * the D command reports (NOTE: ABC9's JSON "size" is payload-only —
 * two surfaces, two sizes, both ArduinoFW-verbatim). */
uint32_t store_run_total(uint8_t id);

uint32_t store_run_payload_len(uint8_t id);
uint32_t store_run_ts(uint8_t id);
uint8_t  store_run_side(uint8_t id);          /* 0=left 1=right */

/* Synthesize/copy blob bytes [off, off+n) — the ONLY data path the
 * pull stream uses. Out-of-range id is a caller bug; offsets were
 * validated against store_run_total() before scheduling. */
void store_read_blob(uint8_t id, uint32_t off, uint8_t *dst, uint32_t n);

/* ── Recording producer (4b: real lifecycle) ──────────────────────
 * Starts recording a run of payload_len bytes. The producer runs as
 * a chunked state machine (erase → write → close), non-blocking, and
 * feeds ramp bytes for now — the sensor pipeline later calls the same
 * path with real frames. Returns false if busy or length out of
 * policy (0 < payload_len <= MAX_RUN_BUDGET - 22). */
bool store_record_start(uint32_t payload_len);
bool store_busy(void);

#endif /* SGC_STORE_H */
