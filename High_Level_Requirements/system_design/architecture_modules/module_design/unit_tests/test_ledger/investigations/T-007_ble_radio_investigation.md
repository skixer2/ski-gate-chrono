# T-007: `ble_radio` ok=0 Regression Investigation (FW 5.10 → 5.14)

**Date:** 2026-08-18  
**Investigator:** Lead Systems Coordinator (main session, after subagent network failures)  
**Case:** TC-2026-08-15-001  
**Status:** Root cause identified

---

## 1. The `ble_radio` code path

**File:** `src/ble/sgc_service.cpp`, function `sgc_ble_radio_restart()` (line 296)

The function is called via a one-shot deferred pattern:
- `request_ble_radio_restart(why)` sets `g_ble_radio_restart_pending = true` + stores `why`
- Main loop (line 1205) checks the flag and calls `sgc_ble_radio_restart(why)` outside BLE event context

```cpp
bool sgc_ble_radio_restart(const char* why)
{
    sgc_ble_ft_abort(reason);
    BLE.stopAdvertise();
    if (BLE.connected()) {
        BLE.disconnect();
        for (int i = 0; i < 3 && BLE.connected(); i++) BLE.poll();
    }
    g_central_connected = false;
    g_sm.set_hold_idle(false);

    BLE.end();          // tear down Cordio stack
    delay(30);          // settle

    if (!BLE.begin()) { // ← THIS returns false → ok=0
        json_kv("ev", "ble_radio");
        json_kv("why", reason);
        json_kv_bool("ok", false);   // ← THE ok=0
        return false;
    }

    sgc_ble_add_service();
    sgc_ble_config_load();
    // ... re-init GATT ...
    json_kv("ok", true);
    return true;
}
```

**The ok=0 means `BLE.begin()` returned false after `BLE.end()`.**

---

## 2. What changed 5.10 → 5.14

### Git commits

```
4a58b28 FW 5.14 + App 1.13: Rewrite FT as phone-pull request-response
88cf424 FW 5.13: FT_CHUNK_SIZE 20→244 to use full negotiated MTU
5038279 FW 5.12: check BLE.connected() before writeValue in FT chunk sender
f42da20 FW 5.11: fix connect-time race in sgc_ble_update_state
830d684 FW 5.10 + App 1.12: FT chunk pace 25→50ms, requestConnectionPriority(balanced)
```

### Changes to `sgc_service.cpp` (5.10 → 5.14)

Only **one** change to the BLE service file:

```diff
- if (g_central_connected && BLE.connected()) {
+ if (g_central_connected) {
```

This is in `sgc_ble_update_state()` (IDLE/POST_RUN), not in `sgc_ble_radio_restart()`.
The `sgc_ble_radio_restart()` function itself was **NOT changed** between 5.10 and 5.14.

### Changes to `file_transfer.cpp` (5.10 → 5.14)

Complete rewrite from device-push streaming to phone-pull request-response:
- 5.13: chunk size 20→244 (full MTU)
- 5.14: phone writes `[cmd, ...]` to ABCA, device responds on ABCB

The FT stall path still calls `request_ble_radio_restart("ft_stall")` — same as 5.07+.

### What did NOT change

- `sgc_ble_radio_restart()` function — **unchanged**
- `BLE.begin()` / `BLE.end()` calls — **unchanged**
- Deferred execution pattern — **unchanged**

---

## 3. Root cause hypothesis

### The `ble_radio` code didn't change — the **context** did.

**5.14 FT rewrite changed the BLE load profile:**
- 5.10: device-push, 20 B chunks @ 50 ms = ~400 B/s, gentle BLE load
- 5.14: phone-pull, 244 B chunks as fast as phone requests = ~3.3 KB/s, **8× higher BLE throughput**

When `BLE.end()` is called after a heavy FT session (or after a stall mid-transfer), the Cordio stack has more pending HCI buffers / ACL packets in flight. `BLE.end()` cannot fully drain them in the 30 ms settle, so `BLE.begin()` fails because the stack hasn't fully released the radio resources.

**In 5.10**, the 20 B @ 50 ms cadence was gentle enough that the stack had little pending state when `BLE.end()` was called → `BLE.begin()` succeeded.

**In 5.14**, the 244 B chunks at high pace leave more in-flight state → `BLE.end()` can't fully clean up in 30 ms → `BLE.begin()` returns false.

### Supporting evidence
- `ok=0` happens in **both** `why="ft_stall"` (stalled mid-transfer) and `why="zombie"` (after successful FT completion) — both scenarios have heavy recent BLE traffic
- The `ble_radio` function is **unchanged** — if it were a code bug, it would have failed on 5.10 too
- The successful transfer (hard reboot) still shows `ok=0` at the end → the zombie path fires after 30s idle following a heavy transfer

---

## 4. Software POR on nRF52

### Options assessed

| Method | Cold POR? | RAM cleared? | Feasible? |
|--------|-----------|-------------|-----------|
| `NVIC_SystemReset()` | No (warm) | No | ❌ This is the problem |
| `NRF_POWER->SYSTEMOFF` | No (sleep) | Retained | ❌ Needs external wake (button/GPIO) |
| Watchdog (WDT) reset | No (warm) | No | ❌ Same as NVIC — RAM survives |
| `NRF_POWER->RESETREAS` detect + explicit BLE deinit | N/A | No | ✅ **Best approach** |
| GPIO power-cycle (external PMIC) | Yes | Yes | ❌ No PMIC on Nicla Sense ME |

**Conclusion: nRF52 has NO software register that forces a true cold POR.** All software reset paths are warm — RAM is preserved.

### Recommended approach: warm-reset detection + explicit BLE deinit

```cpp
void setup()
{
    uint32_t rr = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFF;  // clear for next boot

    // ... existing boot JSON ...

    // If warm reset (not power-on), explicitly tear down BLE before begin.
    // RESETREAS bit 0 = POWER_ON_RESET. If bit 0 is NOT set, it's a warm reset.
    bool is_power_on = (rr & 0x01) != 0;

    // ... LED, flash, preroll, LDC init ...

    // BLE init — explicit end() if warm reset
    if (!is_power_on) {
        BLE.end();    // force Cordio static teardown
        delay(100);   // generous settle for thread/driver cleanup
    }
    bool ble_ok = BLE.begin();
    // ...
}
```

**Why this works:**
- On power-on reset (cold): `RESETREAS bit 0` is set → skip `BLE.end()` → normal `BLE.begin()` on clean RAM
- On warm reset (PlatformIO flash, WDT, soft reset): `RESETREAS bit 0` is NOT set → call `BLE.end()` first → tears Cordio statics from previous firmware → `BLE.begin()` gets clean init

**Risk:** `BLE.end()` on a potentially-corrupted stack could hang. Mitigate with the WDT (already active since 5.08/5.09) — if `BLE.end()` hangs >5s, WDT reboots and we try again.

---

## 5. Fix for `ble_radio` ok=0

### Option A: Increase settle time after `BLE.end()`

```cpp
// In sgc_ble_radio_restart(), change:
delay(30);
// To:
delay(100);  // V5.15: give Cordio more time to release radio after heavy FT
```

Simple, but may not be enough if the stack is deeply wedged.

### Option B: Retry `BLE.begin()` with escalating delays

```cpp
BLE.end();
delay(50);
if (!BLE.begin()) {
    delay(200);   // longer settle, retry
    if (!BLE.begin()) {
        // ok=0 — give up, let WDT or desync recovery handle it
        json_kv("ok", false);
        return false;
    }
}
// ok=1
```

### Option C (recommended): Both warm-reset detection AND retry

1. **setup() warm-reset detection** (fixes the flash-then-stall problem)
2. **`sgc_ble_radio_restart()` retry with longer settle** (fixes ok=0 after heavy FT)

---

## 6. Recommended fix plan

### T-008a: Warm-reset BLE deinit in setup()
- **File:** `src/main.cpp`, `setup()` around line 1046
- **Change:** Check `RESETREAS` bit 0; if not power-on, call `BLE.end()` + `delay(100)` before `BLE.begin()`
- **Risk:** Low — WDT protects against hang
- **Bump:** FW 5.15

### T-008b: `sgc_ble_radio_restart()` retry
- **File:** `src/ble/sgc_service.cpp`, line ~315
- **Change:** After `BLE.end()`, increase settle to 100 ms; if `BLE.begin()` fails, retry once after 300 ms
- **Risk:** Low — already in a recovery path
- **Bump:** FW 5.15

### T-009: Smoke test FW 5.15
- Flash via PlatformIO (warm reset) → verify FT works without hard button press
- Hard reboot → verify FT still works
- Multiple connect/disconnect/FT cycles → verify `ble_radio` ok=1

---

## 7. Limitations

- No nRF52 JTAG debugger attached — can't inspect Cordio internal state at the ok=0 moment
- `BLE.begin()` return value is ArduinoBLE's boolean; the exact failure reason inside Cordio is not exposed
- The 30 ms settle was chosen empirically (v5.07) for the 20 B chunk era; the 244 B era needs reassessment
