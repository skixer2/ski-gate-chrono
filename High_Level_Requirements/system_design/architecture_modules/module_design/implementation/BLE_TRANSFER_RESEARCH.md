# BLE Transfer Research — Why the disconnections, and how the big boys do it

**Date:** 2026-09-10 (post-bench, per JP) · **Status:** reference for next session
**Question 1:** Why do WE get continuous disconnections while Garmin updates
multi-MB files to the watch over BLE smoothly and quickly?
**Question 2:** How do other applications do BLE file transfer? Best practices?

---

## 1. What the sources say (all verified 2026-09-10)

### 1.1 The physics: our link is capable of 20–40× more than we use

| Source | Number |
|---|---|
| Nordic SoftDevice spec (S132/S140, MTU 247 + DLE, notifications, 1M PHY) | **738 kbps ≈ 92 KB/s** |
| Nordic SoftDevice spec (same, 2M PHY) | 1321 kbps ≈ 165 KB/s |
| Punch Through measured on Android/iOS (DLE, 1M PHY) | **~50 KB/s** |
| uynguyen.dev (MTU 517, no-response, flow control) | 30–100 KB/s on Android |
| **SGC pull, clean link (measured today)** | **2.4 KB/s** |
| SGC push, best-ever (5.58 turbo, mcp control) | ~5 KB/s |

Both our protocols run an order of magnitude below the documented ceiling.

### 1.2 The consensus protocol shape (every source agrees)

From Punch Through (parts 1–4), Nordic DFU spec, Infineon throughput example,
hubble.com OTA guide, uynguyen.dev, Stack Overflow streaming schemes:

1. **Never write-with-response in the bulk path.** Write Request = one packet
   per connection event (ACK round trip). Write **Command** (no response) +
   **notifications** = multiple packets per connection event. "4–6× faster."
2. **No per-chunk request/response round trips.** Bulk-stream one direction;
   ACK every N chunks (credit window). Nordic DFU: "Packet Receipt
   Notification" every N packets (default 16–32) — this is exactly the
   flow-control the Android stack gives you for free via
   `onCharacteristicWrite` buffering.
3. **Max MTU (247–517), DLE on (251-byte LL PDUs), request 2M PHY.** On
   Android request `CONNECTION_PRIORITY_HIGH` during transfer (15 ms interval
   sweet spot; phones won't give 7.5 ms because of WiFi coexistence).
4. **App-level reliability on top, not per packet**: sequence numbers,
   end-to-end CRC, resume from offset. The LL already guarantees per-packet
   integrity+order — "CRC at a higher level adds low overhead" (argenox).
5. **Android flow control for write-without-response**: wait for
   `onCharacteristicWrite` callback before queueing more; the stack buffers
   and paces to the controller. Never blind-loop.
6. Standard GATT trio: **Control** (write+response: start/size/hash/status),
   **Data** (write-without-response or notify, MTU-3 payload), **Progress**
   (notify: ACK/NAK every N, errors).

### 1.3 Why the disconnections (status 8 / 0x08 = link supervision timeout)

- 0x08 = "the two Bluetooth chips lost each other" — LL-level, resets only if
  no valid packet for supervision_timeout. Android **hardcodes** it: 5 s
  (Android 10+; 20 s before) vs iOS 750 ms. Constraint:
  `supervision_timeout > (1+latency)×interval×2` (hubble.com checklist).
- Status 133 (0x85) = Android catch-all: stale GATT handles (must `close()`
  after every disconnect), reconnect storms, GATT cache mismatch after
  reflash (!! — we reflash constantly; `refresh()`/clearGattCache matters),
  bonding races.
- Known-hard truth from David Young (AltBeacon): Android BLE connection
  failure rate ~20% real-world vs 3% iOS; Android 10's 5 s timeout did NOT
  statistically improve reliability. **Some wedge rate on Samsung is weather,
  not our bug.**
- Classy Code war story: after abnormal link death, Android can hold dead
  connection state ~20 s; peripheral must disconnect cleanly. Matches our
  5.69-era "half-open slot" observations exactly.
- ATT 30 s transaction timeout: a Write Request that never gets answered →
  stack tears down the link at exactly 30 s. (Our wedges show faster, but any
  blocking writeValue on our side can trigger this.)

### 1.4 Why Garmin feels smooth (reasoned from the above; no public spec)

1. **One-way bulk streaming, no per-chunk round trip.** Garmin's phone→watch
   file push = write-command/notification stream with credit-based ACK.
   Nothing in their path does request→wait→response per few hundred bytes.
   Fewer round trips = fewer state transitions = fewer failure surfaces.
2. **Mature peripheral stack**: Garmin watches run Nordic-class SoCs/
   SoftDevices with DLE + 2M PHY negotiated; effective ~10–50 KB/s means a
   5 MB update in 2–8 min — "smooth" because progress is continuous and the
   protocol survives link hiccups transparently (LL retransmits; app-level
   resume invisible to user).
3. **Battle-tested Android app**: years of per-OEM quirk handling (serialised
   GATT ops, proper close(), cache refresh, jittered backoff, foreground
   service). Plus Garmin pushes big updates over WiFi/USB when possible —
   BLE is the slow path they engineered for reliability, not speed.
4. Their transfer is **not actually fast** (~2–4 kB/s in older captures of
   Garmin BLE DFU) — it's *steady and uninterrupted*, which is what the user
   perceives as "smooth and quick".

---

## 2. What this means for SGC — concrete deltas

**Our pull today = anti-pattern per every source:** write-WITH-response per
512 B request + per-request round trip + hex (2× air bytes) + 20 ms manual
pacing + request echo visible. Ceiling ≈ 2.4 KB/s — measured exactly that.

**The fix that keeps JP's pull model ("phone asks, device sends") but adopts
the consensus mechanics — "streaming pull":**

1. Phone sends ONE command: `r <id> <off> <len>` (len = whole remaining run).
2. Device streams binary (or base64) notifications back-to-back, MTU-3
   payload each, **no manual pacing** — Cordio queues, LL/Android flow-control
   pace it. Seq number + total in a 4–6 B header per notification.
3. Phone ACKs every N notifications (credit window, e.g. 8–16) or just
   tracks last-seq; on wedge: reconnect, `r <id> <last_offset> <rest>` —
   **our stateless resume already does exactly this.**
4. End: device sends trailer notify (CRC). Phone verifies.
5. Kill the echo: don't log/surface the value-changed event of our own write
   (or unsubscribe-notify handling of echo lines).
6. Params we already have right: MTU 247 ✓, HIGH priority (15 ms) ✓,
   foreground service ✓. To add: **request 2M PHY** (`gatt.readPhy` +
   `setPreferredPhy(LE_2M)`), check DLE is active on Cordio (verify via
   sniffer or Android log), consider binary frames (hex was for
   debuggability — a `?debug=1` could switch encodings).
7. Optional pro-path: **L2CAP LE Credit-based CoC** (Android API 29+):
   dedicated channel, built-in credits, no GATT overhead. Highest reliable
   throughput; bigger FW+app change. Keep as plan B.

**Expected result:** clean run 39 KB in ~1–2 s at 20–50 KB/s (vs 16 s today),
wedge cost unchanged (~30 s, mostly phone-side recovery), zero visible errors.

---

## 3. Sources

- Punch Through: *Maximizing BLE Throughput parts 1–4* (punchthrough.com/ble-throughput-part-4) — DLE/MTU/interval/write-type experiments
- Punch Through: *Android BLE Ultimate Guide* + *Write Requests vs Write Commands* (2025)
- Nordic SoftDevice Specification: *BLE data throughput* tables (docs.nordicsemi.com)
- Nordic DevZone: *BLE Throughput and DFU* (WiFi coexistence, 15–20 ms floor)
- Nordic DFU profile spec (Secure DFU: control point + packet char write-without-response + PRN every N)
- EnGenius: *BLE 5.0 Throughput Testing* (2M PHY / DLE / MTU=247 alignment)
- argenox: *Maximizing BLE Throughput* (protocol design: no app ACK per packet)
- hubble.com: *Reliable OTA over BLE*, *Supervision timeout explained*, *10 most common BLE bugs* (reason-code-first debugging checklist)
- uynguyen.dev: *Reliable BLE Data Transfer* (Android flow control, 30–100 KB/s, checklist)
- David Young (davidgyoungtech.com): Android 20% connection failure rate, hardcoded supervision timeouts
- Classy Code: *Android BLE connection timeouts* (dead-state 20 s, clean peripheral disconnect)
- Stack Overflow 37151579 (streaming schemes; LL ACK is the flow control), 43741849 (Android no-response buffering), 77343657 (L2CAP CoC option)
- crickshaw.dev / dev.to / Medium M. van Welie: GATT 133 production causes (serialize, close(), backoff, cache)
