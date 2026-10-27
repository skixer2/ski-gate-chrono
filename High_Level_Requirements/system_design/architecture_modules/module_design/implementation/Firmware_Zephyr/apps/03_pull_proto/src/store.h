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

/* ── Stable run-id addressing (v0.22.1, JP 2026-10-27) ─────────────
 * Wire ids (ABC9 "id", D "run", s/H/d arguments) are the per-run
 * run_id assigned at record time — monotonic u16, NOT the entries[]
 * position. Deletes leave holes; no wire id ever shifts. Listings
 * read ids via store_run_id(); requests resolve a wire id to its
 * current position via store_run_pos() (-1 = unknown run id → the
 * protocol's not-found replies: E 1 / "already gone"). The read
 * accessors below take the POSITION. */
uint16_t store_run_id(uint8_t pos);           /* wire id at position */
int16_t  store_run_pos(uint16_t run_id);      /* position, -1 = none */

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
 * a chunked state machine (write → close → runway top-up), non-
 * blocking, and feeds ramp bytes for now — the sensor pipeline later
 * calls the same path with real frames. SGL2: NO erase at start —
 * the runway for a max-length run is freed right AFTER each run
 * (and at boot / after wipe), so a start of unknown length always
 * has clean space. Returns false if busy (e.g. top-up in flight),
 * length out of policy (0 < payload_len <= MAX_RUN_BUDGET - 22),
 * or the runway invariant is violated (loud printk). */
bool store_record_start(uint32_t payload_len);
bool store_busy(void);

/* ── Streaming append API (M3 producer path, JP GO 2026-09-29) ────
 * The recording state machine records with UNKNOWN length at start:
 *   store_record_begin()  — plans at the cursor (wrap rule uses the
 *                           budget-sized TARGET as the fit test — the
 *                           runway guarantees it), writes the 16 B header
 *                           immediately, arms the budget cap.
 *   store_record_append(src, len) — RAM-buffered (256 B, page-flushed),
 *                           CRC on the fly. Returns bytes ACCEPTED
 *                           (≤ len); < len means the budget cap hit —
 *                           caller MUST end() (truncate-at-budget + close,
 *                           run flagged truncated via cal_accuracy bit 7).
 *   store_record_end()    — trailer, index entry, persist, then the
 *                           async runway TOPUP (POST_RUN erases).
 * Synchronous in the caller's (work) context; only TOPUP self-schedules.
 * The 'w' bench path (store_record_start) is unchanged. */
bool store_record_begin(uint8_t arm_side);   /* side from device config (v0.18.0) */
int32_t store_record_append(const uint8_t *src, uint32_t len);
bool store_record_end(void);

/* ── Active run budget (SGL3, discipline-driven) ──────────────────
 * Payload-byte ceiling for future runs; drives the runway target.
 * App maps disciplines at 1 KB/s: SL 90 s=90 KB, GS/SG 120 s=120 KB,
 * DH 150 s=150 KB (default), Custom 60–600 s=60–600 KB. Bounds:
 * (4096-22) B .. MAX_RUN_BUDGET (600 KiB). Setter persists + re-tops
 * the runway when idle. */
bool store_set_run_budget(uint32_t payload_bytes);
uint32_t store_run_budget(void);

/* How many FULL budget runs still fit (athlete-facing estimate). */
uint32_t store_free_full_runs(void);

/* ── F46 per-run delete (v0.22.0; v0.22.1 stable-id resolution) ────
 * Remove one run from the INDEX only. Instant and erase-free: the
 * run's flash sectors join the lazy reclamation pool — nothing reads
 * them (every access path is list-driven: D directory, ABC9, 's'
 * stream, 'H' hexdump all resolve the wire run_id via
 * store_run_pos()), and the runway top-up erases them in place when
 * the log wraps over them, exactly like post-wipe garbage.
 * `pos` = the entries[] POSITION from store_run_pos(), not the wire
 * id. Idempotent defense: a pos past the end is success ("already
 * gone") so phone retries are safe. Internally the array still
 * compacts (positions shift down one slot) — wire ids do NOT (stable
 * run_ids, holes expected); callers resolve fresh per request.
 * Returns false only when the store is busy (record/top-up in
 * flight): caller refuses with a busy error, retry then. */
bool store_run_delete(uint8_t pos);

/* Bench reset: erase the index, restart the log empty. Re-tops the
 * runway in the background (SGL2: a wipe leaves flash dirty; the
 * top-up makes the first 244 KiB clean again). */
void store_wipe(void);

/* ABC0 time sync: phone writes UTC epoch seconds at connect. */
void store_epoch_sync(uint32_t epoch);
uint32_t store_epoch_now(void);   /* 0 = never synced */

/* v0.18.0 status surfaces (ABC7 / error-tolerance audit). */
const struct device *store_flash_device(void);
uint8_t store_used_pct(void);      /* % of LOG region in live runs */

/* ── B4 step-0: ring drain-load simulation (bench only) ───────────
 * Emulates the pre-roll design's per-tick flash cost during LOGGING:
 * 2 pops (20 B read each) + 1 push (20 B) into the reserved ring
 * region 0x0-0x4FFF (5 sectors, min page-multiple granting 10 s of
 * pre-trigger at 100 Hz x 20 B). Page-buffered writes (256 B), no
 * erase during logging — region erased once at reset. */

#endif /* SGC_STORE_H */
