/*
 * ring.h — pre-roll ring (spec v4-final, JP 2026-10-05)
 * =====================================================
 * Region 0x0000–0x13FFF = 4096 linear slots × 20 B (RawFrame + ts_ms).
 * Bank A (slots 0–2047) = 20 s pre-trigger; Bank B (2048–4095) = 20 s
 * post-trigger order-preserving buffer. Linear within one ARM→run
 * cycle (never wraps — drain arithmetic guarantees tail ≤ 2×head-fill);
 * erased on EVERY entry to IDLE (boot, post-run, arm-timeout).
 *
 * Slot: [0..15] struct sgc_raw_frame (LE, as-packed order)
 *       [16..19] ts_ms little-endian (arrival stamp, NOT pop time)
 */
#ifndef SGC_RING_H
#define SGC_RING_H

#include <stdint.h>
#include <stdbool.h>
#include "bitpacker.h"

#define RING_SLOTS      4096u
#define RING_SLOT_BYTES 20u
#define RING_BASE       0x00000u
#define RING_BYTES      (RING_SLOTS * RING_SLOT_BYTES)   /* 0x14000 */

void ring_init(void);          /* boot: erase banks (IDLE invariant) */

/* New ARM cycle. Requires banks erased (invariant). */
void ring_arm_reset(void);

/* Push one slot (arrival ts). false = region exhausted (cannot happen
 * within spec'd ARM ≤ 20 s + drain ≤ backlog; false = bench bug). */
bool ring_push(const struct sgc_raw_frame *f, uint32_t ts_ms);

/* Pop oldest slot. false = empty. */
bool ring_pop_oldest(struct sgc_raw_frame *f, uint32_t *ts_ms);

uint32_t ring_count(void);

/* Erase both banks (~20 × 45–100 ms sectors). Called on IDLE entry,
 * after the state machine's timer stopped. */
void ring_erase_banks(void);

#endif /* SGC_RING_H */
