# SGC Firmware Test Suite Quality Analysis — Sections 3–7

## Section 3 — Test Quality Assessment (continued)

### 3.1 Timing Sensitivity (continued from Part 1)

#### Fixed Waits: Fragility Spectrum

| Wait | Duration | Test(s) | Mechanism | Fragility | Why |
|------|----------|---------|-----------|-----------|-----|
| Ring buffer fill | 5500–6000 ms | U09, U10, U11, U14, U15 | Fixed `wait_ms` after arm | **MEDIUM** | Depends on sensor sampling rate (200 Hz → 500 samples = 2.5s). The 5.5–6s waits contain ~2× safety margin. BUT: if the sensor loop slows (BLE interference drops it below 100 Hz), 6s may be insufficient for 500 samples. U03's comment about BLE dropping the loop below 100 Hz means 500 samples could take >5s. The margin is only ~1.2× in the BLE-degraded case. |
| Flatline detection | 12000 ms | U07 | Fixed `wait_ms` then poll | **HIGH** | The end detector requires 1000 quiet frames at 100 Hz = 10s. The 12s wait provides 2s margin. However: (a) if BLE degrades the loop to 60 Hz, 1000 frames = 16.7s — the 12s wait fails; (b) the subsequent poll has a 15s timeout, so the test doesn't fail, but the fixed wait is wasted and the test takes 27s instead of 15s; (c) if the loop runs at the expected 100 Hz, 12s is tolerable but still a 20% over-wait. |
| State transitions | 1000 ms (between force_state calls) | All scenarios | Fixed `wait_ms` after each force | **LOW** | State transitions should be instantaneous (milliseconds). 1000 ms is a generous debounce that handles any firmware processing lag. Low risk of flaking due to this alone. |
| POST_RUN→IDLE cooldown | Polled (25s timeout) | U03, U13 | Polling `? STATE:` | **LOW** | Polling is the correct approach for an auto-transition with unknown duration. The cooldown is deterministic in firmware (likely a fixed N-second wait), so polling at 500ms intervals with a 25s cap is robust. |
| Pressure ramp injection | 100 ms/step | U04, U05, U06 | `inject_pressure_ramp(…, delay=100)` | **MEDIUM** | The delay between injection steps matters because the start detector accumulates delta-pressure over a sliding window. If serial delays cause injection to bunch up (the Nicla processes a burst of commands at once), the temporal distribution of pressure values changes. At 100ms steps this is unlikely but could manifest on a heavily loaded host. |

**Verdict on timing**: The fixed waits for ring buffer fill are the most concerning because they're coupled to a variable sensor loop rate. The polling pattern used for state transitions is the right approach and should be extended to replace fixed waits for ring-buffer fill detection. Specifically, querying `?` and checking `SAMPLES:` or ring buffer fill percentage would eliminate the BLE interference risk entirely.

### 3.2 Reliability Analysis: Run 1 vs Run 2

Run 1 had 2 failures (U07 end detector + U18 quaternion injection); Run 2 had zero failures across all 7 files. This pattern strongly suggests **environmental rather than firmware bugs**.

#### Most Likely Root Causes

**U07 — End Detector Flatline (6/7 passed, U08 9/10 passed):**

The most probable explanation is **BLE radio activity during Run 1**. Evidence:
1. U03 explicitly warns: *"Disconnect BLE phone app — BLE polling can slow sensor loop below 100 Hz."*
2. If the sensor loop drops below 100 Hz, the 1000 quiet frames needed for end detection take longer than 10 seconds, and the 12s fixed wait + 15s poll timeout may race against the actual transition.
3. U08 also flaked in Run 1 (9/10, not 10/10), which is suspicious: U08 injects non-zero accel to verify NO premature stop. If the sensor loop is slow, the injected LA values might be processed slowly, causing fewer "movement" frames than expected.
4. Run 2 was likely run with BLE inactive or the phone disconnected, which explains the 100% pass rate.

**U18 — Inject Quaternion (4/5 passed):**

The most probable explanation is **Arduino `Serial.print(float)` precision variability** combined with the string-matching approach:
1. Arduino's `Serial.print(float, 2)` outputs 2 decimal places, but floating-point rounding can produce values like `0.699` instead of `0.70` or `0.707` instead of `0.71`.
2. The test matches `Q=(0.7` — a partial match on only the first component. If the firmware echoes back `Q=(0.70,0.00,0.71,0.00)` and the test looks for `Q=(0.7`, it matches. But if the echo format changes (trailing zero in `0.70` vs `0.7`), or if the Arduino's float formatting produces `0.71` for `0.707` due to rounding-up, the match could fail.
3. Run 2 succeeded fully — this could be because the Nicla was freshly reset, putting serial formatting in a consistent initial state, or because the test mode was properly initialized (the `enable_test_mode(h)` in `force_state(h, 'a')` may have been more reliable on Run 2).

**Other contributing factors for Run 1 flakiness:**
- **Serial buffer state**: Between test files, if old serial data lingers in the buffer, `_read_all()` may pick up stale output from a previous test scenario, causing false matches or misses.
- **Test ordering**: The test runner might execute files in filesystem order. If `test_end_detector.py` runs before `test_state_machine.py`, the firmware may be in an unexpected initial state.
- **Firmware warm-up**: The BHI260AP IMU and BMP390 pressure sensor need calibration time. If tests start immediately after power-on, early readings may be unstable.

#### Recommended Fix for Run 1 Flakiness

1. **Add a pre-test BLE check**: Query `?` and verify BLE is advertising but not actively connected. If connected, warn or fail with a clear message.
2. **Add explicit flush**: Before each test file, drain the serial buffer with a longer timeout (2s of silence).
3. **Add firmware warm-up wait**: After `enable_test_mode()`, wait 2s and verify `Z` echo returns sensor data showing valid (non-zero, non-NaN) values.
4. **Replace fixed ring-buffer waits with polling**: Query `?` for `SAMPLES:500/500` instead of sleeping 6s and hoping.

### 3.3 Assertion Quality

The test harness uses three levels of assertion, all based on string matching:

| Assertion Type | Mechanism | Example | Quality |
|---------------|-----------|---------|---------|
| `expect_contains` | `substring in output` | `'STATE:LOGGING'` | **Adequate** for unambiguous strings |
| `expect_not_contains` | `substring not in output` | `'STATE:LOGGING'` (U08) | **Adequate** for negation |
| Implicit "no errors" | No `ERROR` or `FAIL` in output | Most send+check steps without expect_contains | **WEAK** — passes vacuously |
| Partial value match | Substring of formatted float | `Q=(0.7` (U18) | **WEAK** — only checks first component, false confidence |

#### The U18 Hack: Cost of False Confidence

U18 matches `Q=(0.7` to verify a quaternion of `(0.707, 0.0, 0.707, 0.0)`. This only validates:
- The first component starts with `0.7`
- Nothing about components 2–4
- Nothing about the `=` sign (could be `Q==(0.7` and still match)

If the firmware has a bug that swaps W and Z components (producing `0.0, 0.0, 0.707, 0.707`), the test still passes. If formatting produces `Q=(0.71,0.00,0.71,0.00)` due to rounding, the test still passes but is verifying different values. **This test provides near-zero confidence in the quaternion injection pathway.**

#### The "Auto-Pass" Problem

Steps without `expect_contains` or `expect_not_contains` pass if the output doesn't contain `'ERROR'` or `'FAIL'` (uppercase). This means:
- A firmware command that silently does nothing passes
- A command that echoes `Ok` instead of the expected value passes
- A command that times out (empty output) passes
- The `on_response` callback can still fail the step, but callbacks are used sparingly

**How many steps use auto-pass?** From the context: U18 and U19 have empty `setup_commands` lists, and many send+check steps rely on the implicit pass. This is the single largest source of false confidence in the suite.

### 3.4 False Positive Risk — Could Tests Pass When Firmware Is Broken?

**Scenario 1: Flash corruption undetected**
Flash self-test (U12) writes and reads 256 bytes of a 2MB flash. Corruption in sectors 1–7 (the remaining 99.99% of flash) passes unnoticed. A run stored in sector 5 could read back garbage, but U13 would still see `Runs >= 1` because the count is likely stored near the beginning.

**Scenario 2: Ring buffer never actually fills**
U09 arms for 6s and checks `R:500/500`. But if the firmware has a bug where the ring buffer counter increments without actually storing data, `R:500/500` still appears. There's no test that reads back ring buffer contents and verifies they're non-zero.

**Scenario 3: Bit packer produces corrupt data**
U14 and U15 exercise the bit-packing pipeline but **never validate the actual compressed data**. They check that the run completes and flash stores it, but they don't:
- Verify compressed data size (should be smaller than raw)
- Decompress and compare round-trip values
- Check that T1/T3 packet types are correctly used
- Validate that the anchor packet (T3) contains correct reference values

**Scenario 4: End detector fires on any flat signal**
U08 injects non-zero LA to verify no premature stop. But what if the end detector is broken such that it fires on ANY signal after 1000 frames, movement or not? U08 only tests the "movement → no stop" case. There's no test for "movement + flat → stop only after flat" — the end detector might stop mid-run if there's a brief flat segment.

**Scenario 5: BLE testing (I01–I03) passes without verifying data integrity**
I03 verifies CRC32, but I02 only reads characteristics — it doesn't write them and verify persistence. The BLE characteristics could return dummy data and I02 would still pass if the string matches look plausible.

---

## Section 4 — What's Well-Tested vs What's Fragile

### 4.1 Solid Tests

| Area | Tests | Why Solid |
|------|-------|-----------|
| **State machine** | U01–U03 | Clean, deterministic. Uses `force_state` single-char commands + polling. The full-cycle test (U03) exercises every state transition that auto-completes. State names are unambiguous strings. |
| **Start detector — positive** | U04 (2m), U05 (3m) | Well-designed. Uses `inject_pressure_ramp` helper to simulate realistic descents. Polls for LOGGING state. Differentiates between thresholds (2m vs 3m). |
| **Start detector — negative** | U06 | 10s flat hold verifies no false trigger. Good edge case. |
| **Ring buffer — fill** | U09, U10 | Simple and unambiguous. `R:500/500` is a clear status string. U10's "wait 3 more, still 500" catches wraparound from continuous filling beyond capacity. |
| **Ring buffer — restart** | U11 | Good: arm, fill, IDLE, arm again, verify < 500 (still filling). Tests that the buffer resets between runs. |
| **Flash self-test** | U12 | Clear pass/fail string: `ALL 256 BYTES MATCH`. Deterministic. |
| **Test mode toggle** | U16 | Simple ON→OFF→ON verification via `Z` echo. Tests that `T` command works. |

### 4.2 Fragile Tests

| Area | Tests | Why Fragile |
|------|-------|-------------|
| **End detector — positive** | U07 | Flaky in Run 1. 12s fixed wait + BLE sensitivity. No verification that the end detector transition was caused by flatline specifically (could be timeout). |
| **End detector — negative** | U08 | Flaky in Run 1. Tests "movement → no stop" but not "movement + brief flat → no stop". |
| **Sensor injection — quaternion** | U18 | Format-sensitive. Partial string match on only one component. 4/5 on Run 1. |
| **Sensor injection — pressure/LA** | U17, U19 | Better than U18 but still string-matched. If Arduino float format changes (e.g., `Serial.print(f, 4)` instead of 2 decimals), these break. |
| **Bit packer** | U14, U15 | Never validates compressed data correctness. Exercises the pipeline but doesn't verify the output. |
| **BLE service** | I01, I02 | Read-only verification. No write+readback cycles. No stress testing (concurrent BLE + sensor). |
| **File transfer** | I03 | Good CRC32 check. But only transfers run #0 — doesn't test boundary runs (last run, deleted run, corrupted run). |
| **Acceptance tests** | A01–A04 | Maps to requirements but reuses the same unit test steps. Not independent verification. |

### 4.3 Deep-Dive: U07/U08 Failures

#### Failure Mode Analysis

The key diagnostic: U07 passed 6/7 (86%) and U08 9/10 (90%) on Run 1. These aren't 0% — they're near-passing with occasional failures. This pattern is characteristic of **timing races**, not logic bugs.

**Most likely failure mode (U07): BLE-induced sensor loop slowdown**

The end detector counts "quiet frames" — frames where acceleration and angular velocity are below thresholds. At 100 Hz (normal), 1000 quiet frames = 10 seconds. The test waits 12 seconds then polls for POST_RUN with a 15s timeout.

If BLE is connected and polling actively, the sensor loop can drop below 100 Hz. At 60 Hz, 1000 frames = 16.7 seconds. The 12s fixed wait finishes at 12s, then polling begins. The state transition might occur at 16.7s — within the 15s poll timeout — but the total test time balloons to 27s. If the loop drops to 50 Hz or below, 1000 frames = 20s+, and the 15s poll timeout might expire.

**Most likely failure mode (U08): Edge case in LA injection timing**

U08 injects `L 500 0 0` (non-zero linear acceleration) + `B 100900` (pressure change) to simulate movement, then waits 5s and checks it's still LOGGING. The failure could be:
- If the LA injection doesn't take effect before the end detector's quiet-frame counter starts, the firmware might see a brief window of true quiet frames, partially advancing the counter. After 5s of movement, the counter resets, but if the timing is unlucky, the counter might reach its threshold during the transition.
- With 9/10 passes, this is consistent with a ~10% probability of the injection timing aligning poorly with the sensor loop phase.

**Fix for both:**

1. **Disable BLE advertising during end-detector tests**: Add a `B` command (BLE off) before U07/U08, restore BLE after.
2. **Query ring buffer fill percentage** instead of fixed waits: After forcing LOGGING, poll `?` until `SAMPLES:` shows data accumulating, then begin the flatline wait. This ensures the sensor loop is confirmed running at speed before the test starts.
3. **Increase flatline wait to 15s** (from 12s) with unchanged poll timeout — costs 3s but eliminates the race with BLE-degraded loops.

### 4.4 Deep-Dive: U18 Quaternion Injection Failure

#### What the test actually does:

```python
# Injects identity quaternion: w=1.0, x=0.0, y=0.0, z=0.0
# Then injects tilted: w=0.707, x=0.0, y=0.707, z=0.0
# Verifies with Z echo, matches 'Q=(0.7' in output
```

#### Failure hypothesis:

Arduino's `Serial.print(float)` without arguments uses 2 decimal places by default. However, the actual formatting depends on the firmware's `Serial.print()` call:

- If the firmware does `Serial.print(q_w, 2)`: outputs `0.71` for 0.707 (rounds up), `1.00` for 1.0
- If the firmware does `Serial.print(q_w)`: outputs `0.71` (default 2 decimals)
- If the firmware does `Serial.print(q_w, 4)`: outputs `0.7070`

The test matches `Q=(0.7` — this would match `0.70`, `0.71`, `0.707`, `0.7070`, but NOT `0.69` or `0.6`.

**The 4/5 failure could be caused by:**
1. Floating-point representation edge: 0.70710678... might round to `0.71` on some calls and `0.70` on others depending on the exact float value stored (0.707 vs 0.7070001 vs 0.7069999).
2. Serial echo timing: The Z command's response arrives in fragments. If `_read_all()` stops reading before the full echo arrives, the partial output might not contain the quaternion line at all.
3. Test mode state: If T wasn't properly toggled before the U18 scenario, the firmware uses real sensor data instead of injected values, and the Z echo shows real quaternion values (not the injected ones).

**The fix should be:**
1. Don't match on formatted floats at all. Use a structured protocol (JSON) where `{"q": [0.707, 0.0, 0.707, 0.0]}` can be parsed and compared with a tolerance (e.g., `abs(actual - expected) < 0.01`).
2. In the short term, match on all 4 components: `Q=(0.7*,0.0*,0.7*,0.0*` using a regex or split-and-compare.
3. Add verification that `Z` echo returns test-mode values (not real sensor values) by checking that the echoed values match the last injection.

---

## Section 5 — Test Maintainability

### 5.1 Discoverability of Failures

**Current state: POOR**

When a string-match assertion fails, the output is:
```
FAIL: Expected 'STATE:LOGGING' in output
Output was: <full serial dump>
```

You must manually scan the serial dump to understand what the firmware actually sent. There's no:
- Diff of expected vs actual
- Highlighted mismatch location
- Structured error with context (e.g., "Got STATE:ARMED instead of STATE:LOGGING")
- Test report summary that groups failures by category

The markdown table output (`results_all.md`) helps but requires reading the full serial log column for failures.

**Impact**: A developer encountering a failure must:
1. Open the test log
2. Find the failing step
3. Read the full serial output for that step
4. Mentally parse what the firmware actually returned
5. Compare against what was expected

This is 5+ minutes per failure, per test run. Over a development cycle with 10 failures, that's nearly an hour of debugging overhead.

### 5.2 Adding a New Test Module

**Current state: GOOD (with caveats)**

The dataclass pattern makes adding tests straightforward:

```python
SCENARIOS = [
    TestScenario(
        name="U20: My new test",
        setup_commands=[
            TestStep("arm", "a", wait_ms=500, expect_not_contains="ERROR"),
        ],
        steps=[
            TestStep("do thing", "X 123", wait_ms=100, expect_contains="OK"),
            TestStep("verify", "?", expect_contains="EXPECTED_STATE"),
        ]
    ),
]
```

**Strengths:**
- Declarative: define what, not how
- Reusable helpers: `force_state()`, `inject_pressure_ramp()`, `enable_test_mode()`
- Auto-discovery: just add to SCENARIOS list

**Weaknesses:**
- Must know firmware command set (undocumented single-char commands)
- Must know serial response format (undocumented protocol)
- Must manually handle timing (guess wait_ms values)
- No way to define assertions on structured data
- No test dependency management (can't say "skip U10 if U09 failed")

### 5.3 CI Integration

**Current state: HARD CONSTRAINT**

The test suite requires a physical Nicla Sense ME connected via USB. This makes CI challenging:

**What's needed for CI:**
1. A dedicated test machine with a Nicla permanently connected
2. A way to reset the Nicla between test runs (power cycle or reset pin)
3. A way to flash new firmware onto the Nicla automatically
4. Serial port reliability over extended periods (USB ports can enumerate differently after reboots)

**Feasible CI architecture:**

```
CI Runner (GitHub Actions self-hosted or similar)
  ├── USB-connected Nicla Sense ME
  ├── Firmware flash step: platformio run -t upload
  ├── Test step: python run_all_tests.py --port auto --output results_ci.md
  ├── Artifact: results_ci.md + serial logs
  └── Pass/fail based on exit code
```

**Challenges:**
- **Auto-discovery fragility**: `find_port()` matches on 'nicla', 'arduino', 'mbed', 'nrf52'. If the OS enumerates the Nicla differently (e.g., as `/dev/ttyACM0` with description "MBED CMSIS-DAP"), the match might fail. Needs a fallback to explicit port config.
- **Nicla reset between runs**: The Nicla doesn't have a software reset command in the test protocol. `R` (factory reset) is destructive. Need a hardware reset (DTR toggle or external relay) or add a soft-reboot command to the firmware.
- **Flakiness accumulation**: Over 100 CI runs, a 5% flake rate (as seen in Run 1) means 5 failures are noise. Need retry logic and flake detection.

### 5.4 Debugging a Failure

**Current state: ADEQUATE (with serial logs) but SLOW**

**What helps:**
- `--log` flag captures full serial output
- `--output` flag produces structured markdown table
- `Tee` class logs to both console and file
- `_read_all()` captures complete serial output per step
- The markdown table shows command, wait, expected, and actual output per step

**What's missing:**
- No diff visualization: "Expected X, got Y" is manual
- No structured comparison: can't say "state was ARMED, expected LOGGING"
- No timeline: can't see when each step executed (wall clock time)
- No correlation between test files: can't see if U07 failed because of a command sent in U06
- No firmware-side logging: the Nicla doesn't send debug info over serial (that we know of)

### 5.5 Harness-Firmware Coupling

**Current state: TIGHT (but intentional for v1)**

The harness uses:
- **Undocumented single-char commands**: `s`, `i`, `a`, `l`, `p`, `T`, `Z`, `?`, `f`, `R`
- **Undocumented response format**: `STATE:NAME`, `R:NNN/500`, `Q=(f,f,f,f)`, `ALL 256 BYTES MATCH`
- **Test-mode-only features**: `T` toggle, `Z` echo, `L`/`B`/`Q` injection

This is acceptable for v1 internal testing but becomes a problem when:
- A new developer needs to write tests (must reverse-engineer the protocol)
- The firmware changes its serial format (all tests break)
- The test harness is shared with external QA (they can't understand it)

**What happens when firmware is updated:**

| Firmware Change | Test Impact |
|----------------|-------------|
| New status field added to `?` output | Parsing `STATE:` still works if prefix is preserved, but other fields may shift |
| `?` output format changes (e.g., JSON) | All tests break — every `expect_contains` needs updating |
| New state added to state machine | Polling `wait_for_state` may need new target states |
| `Serial.print(f)` precision changes | U17, U18, U19 string matches break |
| Command characters reassigned | All `force_state()` calls break |
| Ring buffer size changes from 500 | U09–U11 `R:500/500` matches break |

**Mitigation**: A single constants file (`firmware_protocol.py`) mapping command chars, state names, and response patterns would reduce the blast radius of firmware changes.

### 5.6 Summary Matrix

| Aspect | Rating | Key Issue |
|--------|--------|-----------|
| Discoverability | ⭐⭐ | Manual log inspection for failures |
| Adding tests | ⭐⭐⭐⭐ | Clean dataclass pattern, needs protocol docs |
| CI readiness | ⭐⭐ | Needs physical hardware + reset mechanism |
| Debuggability | ⭐⭐⭐ | Good serial logs, no structured diff |
| Coupling | ⭐⭐ | Tightly coupled to undocumented protocol |
| Regression safety | ⭐⭐ | Any firmware format change breaks all tests |

---

## Section 6 — Recommendations (Prioritized by Impact)

### R1: Structured Protocol (JSON-Lines) — CRITICAL

**Problem:** Every assertion is string matching on raw serial output. Format changes break tests. Partial matches create false confidence. Debugging requires manual log reading.

**Proposed fix:** Implement JSON-lines protocol in firmware (`TEST_MODE` only to avoid production overhead):
```
{"type":"state","name":"LOGGING","uptime_ms":12345}
{"type":"sensor","pressure_pa":101325.0,"quat":[1.0,0.0,0.0,0.0],"la_mg":[0,0,-9810]}
{"type":"ring_buffer","samples":500,"capacity":500}
{"type":"flash","sectors_used":3,"total_sectors":64}
{"type":"ack","command":"T","result":"ok"}
```

Harness parses each JSON line, routes to typed handler, stores structured state. Assertions become:
```python
TestStep("verify state", "?", expect_json={"type":"state","name":"LOGGING"})
TestStep("verify quat", "Z", expect_json_path="sensor.quat", expect_near=[0.707, 0.0, 0.707, 0.0], tolerance=0.01)
```

**Effort:** 3–5 days (firmware: add JSON serialization in test mode, ~200 LOC; harness: JSON parser + typed assertions, ~300 LOC; update all existing tests, ~200 LOC)

**Impact:** Eliminates the biggest fragility. Enables structured assertions with tolerance. Makes test output self-documenting. Future-proofs against format changes (add new JSON fields without breaking old tests).

### R2: Timing Robustness — HIGH

**Problem:** Fixed waits for ring buffer fill, flatline detection, and pressure ramps are fragile under BLE interference.

**Proposed fix:**
1. **Ring buffer polling**: Replace fixed 5500ms waits with `poll_for_json(json_path="ring_buffer.samples", condition=">= 500", timeout=15000)`. This adapts to any sensor loop speed.
2. **Flatline detection polling**: Replace 12s fixed wait with `wait_for_state("POST_RUN", timeout=30000)`. Remove the fixed pre-wait entirely — start polling immediately.
3. **Pressure ramp injection**: Instead of a fixed delay between injection steps, verify each injection was received before sending the next:
   ```python
   for p in ramp:
       h.send(f"B {p}")
       h.expect_json({"type":"ack","command":"B","result":"ok"}, timeout=200)
   ```
4. **BLE-aware mode**: Add `--no-ble-check` flag defaulting to True. Before timing-sensitive tests, query BLE connection state via `?` and warn if connected.

**Effort:** 2–3 days (depends on R1 being done first; otherwise requires string-matching polling which is more fragile)

**Impact:** Eliminates the primary cause of Run 1 flakiness. Makes tests robust against BLE interference and variable sensor loop rates.

### R3: Test Isolation — HIGH

**Problem:** No teardown guarantees. Hidden coupling between tests (U18/U19 had empty `setup_commands`). Test ordering matters.

**Proposed fix:**
1. **Add `teardown_commands` to TestScenario**: Run after the scenario (pass or fail) to restore known state.
2. **Mandatory setup for every scenario**: Every scenario must explicitly set the firmware to a known state. Reject scenarios with empty `setup_commands` (warn, don't error, for backward compat).
3. **Pre-test state verification**: Before running each scenario, query `?` and verify the firmware is in the expected initial state. If not, force it.
4. **Serial buffer drain between scenarios**: Add a 2s silence-gap drain after each scenario to prevent cross-contamination.
5. **Nicla soft-reset between test files**: Add a `Ctrl+D` or dedicated reset command to reboot the Nicla between test files (requires firmware support).

**Example enhancement:**
```python
@dataclass
class TestScenario:
    name: str
    setup_commands: list[TestStep]
    steps: list[TestStep]
    teardown_commands: list[TestStep] = field(default_factory=list)
    required_initial_state: str | None = None  # e.g., "IDLE"
    skip_if_ble_connected: bool = False  # U07/U08 would set True
```

**Effort:** 1–2 days (harness changes + updating scenario definitions)

**Impact:** Eliminates test ordering dependencies. Makes "Run 1 vs Run 2" flakiness reproducible. Increases confidence that a passing test means the firmware works under those conditions.

### R4: Expanded Edge Case Coverage — MEDIUM

**Problem:** Critical edge cases uncovered. Ring buffer wraparound, flash boundary conditions, BLE concurrency, start detector precision, sensor fusion validation.

**Proposed new tests (prioritized):**

| Priority | Test ID | Description | Why |
|----------|---------|-------------|-----|
| P0 | U20 | Ring buffer wraparound: arm, let fill completely (500/500), continue running for 3 more seconds, verify buffer still 500/500 (wraps correctly without corruption) | Currently only U10 does this implicitly; explicit test needed |
| P0 | U21 | Flash full run: complete 3 consecutive runs (not just 1), verify each is stored in flash and readable via BLE file transfer | Only 1 run tested in U13; multi-run flash management is completely untested |
| P0 | U22 | Zero-length run: arm then immediately force POST_RUN (no LOGGING), verify firmware handles gracefully | State machine edge case; what does the fw do with 0 samples? |
| P1 | U23 | Flash-full behavior: fill flash to capacity (write runs until no space), verify firmware reports full and doesn't corrupt existing data | 2MB flash holds many runs, but eventually it fills |
| P1 | U24 | Corrupted flash sector: inject corrupted sector via test mode, verify recovery (skip sector, report degraded) | Real-world: flash can develop bad blocks |
| P1 | U25 | BLE + sensor concurrency: run start detector test while BLE is actively connected and polling characteristics | Reproduces the U07/U08 flake condition intentionally |
| P1 | U26 | Start detector precision: inject pressure changes at exactly 1.9m, 2.0m, 2.1m descent equivalents, verify threshold at 2.0m | Validates calibration and threshold logic |
| P2 | U27 | Rapid state toggling: arm→IDLE→arm→IDLE→arm→IDLE rapidly (10 cycles), verify no state corruption | Stress test for state machine |
| P2 | U28 | Sensor timeout: stop injecting values for 5s mid-run, verify firmware timeout behavior (does it stop? report error?) | Real-world: sensor can glitch |
| P2 | U29 | Invalid injection: inject NaN, Inf, negative pressure, verify firmware rejects gracefully | Defensive programming validation |
| P2 | U30 | Power-loss simulation: interrupt run mid-LOGGING (simulate via serial disconnect), verify flash state is consistent on reconnect | Critical for field reliability |

**Effort:** 3–5 days for all 11 tests (assuming R1 structured protocol is available; otherwise 1–2 days longer)

**Impact:** Closes the most critical coverage gaps. Brings coverage from ~53% to ~75% of automatable firmware functionality.

### R5: CI Strategy — MEDIUM

**Problem:** Tests require physical Nicla, no CI pipeline exists.

**Proposed fix:**

**Phase 1 — Manual CI (Day 1):**
- Dedicated Raspberry Pi or old laptop with Nicla permanently connected
- Cron job: flash latest firmware → run tests → post results to Slack/Discord
- Pre-commit hook: remind developer to run tests

**Phase 2 — Automated CI (Week 1–2):**
- GitHub Actions self-hosted runner on the test machine
- Workflow trigger: PR to `main` or `firmware/*` paths
- Steps: flash → test → report as PR check
- Hardware watchdog: external relay to power-cycle Nicla if unresponsive

**Phase 3 — Hardware-in-Loop Rig (Month 1–2):**
- Dedicated test jig with:
  - Nicla in socket (not soldered, for replacement)
  - Power relay for remote reset
  - USB hub with fixed port mapping (udev rules)
  - Optional: BLE sniffer for CI-visible BLE traffic validation
- Parallel testing: 2–3 Niclas running different test subsets

**Minimum viable CI (Phase 1):**
```bash
#!/bin/bash
# ci_test.sh
platformio run -t upload -e nicla
sleep 3
python run_all_tests.py --port auto --output results_ci.md
exit_code=$?
if [ $exit_code -ne 0 ]; then
    echo "TESTS FAILED" | mail -s "SGC CI Failure" -a results_ci.md dev@example.com
fi
exit $exit_code
```

**Effort:** Phase 1: 2 hours. Phase 2: 1 day. Phase 3: 1 week.

**Impact:** Prevents regressions from reaching `main`. Catches BLE interference flakiness early. Makes firmware changes safer.

### R6: Assertion Library — MEDIUM

**Problem:** Only 3 assertion types (contains, not-contains, implicit no-error). No numeric comparison, no tolerance, no structured validation.

**Proposed fix (builds on R1):**

```python
@dataclass
class JsonAssertion:
    """Structured assertion on parsed JSON output."""
    json_path: str            # e.g., "sensor.quat[0]"
    operator: str             # "eq", "near", "gt", "lt", "contains", "regex"
    expected: Any
    tolerance: float = 0.0    # for "near" operator
    message: str = ""         # custom failure message

class TestStep:
    # ... existing fields ...
    expect_json: list[JsonAssertion] | None = None
```

Usage:
```python
TestStep("verify ring full",
    command="?",
    expect_json=[
        JsonAssertion("ring_buffer.samples", "eq", 500),
        JsonAssertion("ring_buffer.capacity", "eq", 500),
    ]),
TestStep("verify quaternion",
    command="Z",
    expect_json=[
        JsonAssertion("sensor.quat", "near", [0.707, 0.0, 0.707, 0.0], tolerance=0.01),
        JsonAssertion("sensor.pressure_pa", "gt", 90000),
    ]),
```

**Effort:** 1–2 days (depends on R1 for JSON parsing infrastructure)

**Impact:** Makes assertions self-documenting. Eliminates the U18 partial-match hack. Provides clear failure messages: `Expected sensor.quat[0] near 0.707 (±0.01), got 0.650`.

### R7: Mock / Hardware-in-Loop Split — LOW (nice-to-have)

**Problem:** All tests require hardware. Can't run on developer laptop. Can't run in standard CI.

**Proposed fix:** Split into two layers:

**Layer 1 — Mock tests (no hardware):**
Tests that validate test harness logic, protocol parsing, and assertion logic. Use a `MockSerial` that replays captured serial logs. These run in < 1 second in any CI.

```python
class MockSerial:
    def __init__(self, replay_file: str):
        self.lines = open(replay_file).readlines()
    def readline(self):
        return self.lines.pop(0) if self.lines else ""
```

**Layer 2 — Hardware-in-loop tests (require Nicla):**
Run on the dedicated CI machine. These are the real tests but they're a subset — focused on hardware-specific behavior (BLE, flash, sensor timing).

**Split:**
- Mock-able: State machine transitions, protocol parsing, ring buffer logic (with captured serial replay), bit packer output validation, flash management logic
- Requires hardware: BLE characteristics, sensor fusion timing, flash read/write endurance, start/end detection with real timing

**Effort:** 2–3 days (capture serial logs from a clean test run, build MockSerial, create mock test variants)

**Impact:** Enables fast iteration on harness logic. Reduces hardware CI load. Makes pre-commit testing feasible.

### Recommendations Priority Matrix

| Rank | Rec | Effort | Impact | ROI |
|------|-----|--------|--------|-----|
| 1 | R1 — Structured Protocol | 3–5d | Critical | Removes biggest fragility |
| 2 | R3 — Test Isolation | 1–2d | High | Eliminates flakiness source |
| 3 | R2 — Timing Robustness | 2–3d | High | Fixes Run 1 failures |
| 4 | R4 — Expanded Coverage | 3–5d | Medium | Closes critical gaps |
| 5 | R6 — Assertion Library | 1–2d | Medium | Builds on R1 |
| 6 | R5 — CI Strategy | 1–7d | Medium | Catches regressions |
| 7 | R7 — Mock/HIL Split | 2–3d | Low | Nice-to-have for dev velocity |

---

## Section 7 — Overall Verdict

### 7.1 Is This Test Suite Sufficient to Ship v1 Firmware?

**Short answer: No, but it's close — with 2 weeks of focused work, yes.**

**What it does well:**
- Core state machine transitions are covered (U01–U03)
- Start detection with realistic pressure ramps is covered (U04–U06)
- Ring buffer fill and restart behavior is covered (U09–U11)
- A basic flash self-test exists (U12)
- A single full run cycle is tested (U13, S01)
- BLE connectivity and file transfer are verified (I01–I03)
- Acceptance tests map to formal requirements (A01–A04)

**What's missing for v1 ship readiness:**
1. **No end-to-end multi-run test**: What happens when a racer does 3 runs in a day? Are all 3 stored? Can all 3 be retrieved? (P0 gap)
2. **No flash boundary testing**: 256 bytes tested of 2MB. What happens when flash fills? What about corrupted sectors? (P0 gap)
3. **No ring buffer wraparound validation**: 500 samples continuously written — does it wrap correctly without corruption? (P0 gap, partially covered)
4. **No BLE concurrency testing**: The comment in U03 acknowledges this is a problem, but there's no test that intentionally provokes it. (P1 gap)
5. **No power-loss / interrupted-run testing**: Critical for a battery-powered field device. (P1 gap)
6. **Flaky test environment**: Run 1 had 2 failures. A shipping test suite should have 0 flaky tests. (P0 operational gap)
7. **No bit-packer correctness validation**: Compression is tested but not verified. (P1 gap)

### 7.2 Single Highest-Impact Improvement

**Implement a JSON-lines structured protocol (R1).**

This one change:
- Eliminates string-matching fragility (the U18 hack disappears)
- Enables structured assertions with tolerance (fixes all sensor injection tests)
- Makes test output self-documenting (reduces debugging time by 80%)
- Future-proofs against firmware format changes
- Enables better polling (check structured fields, not raw strings)
- Is a prerequisite for R2 (timing robustness) and R6 (assertion library)

**Cost**: 3–5 days of firmware + harness work.  
**Payoff**: Every subsequent test improvement is easier and more reliable.

### 7.3 Confidence Assessment

**Current confidence provided by test suite:**

| Area | Confidence | Notes |
|------|-----------|-------|
| State machine correctness | **85%** | Well-covered, deterministic |
| Start detection | **75%** | Good positive/negative tests, but no boundary precision test |
| End detection | **50%** | Flaky, only 10s flatline tested, no BLE-concurrent test |
| Ring buffer integrity | **60%** | Fill/restart tested, but no content validation or wraparound stress |
| Flash storage | **40%** | Only 256 bytes verified, 1-run storage tested, no multi-run, no full test |
| Bit packing / compression | **30%** | Pipeline exercised but output never validated |
| Sensor injection pathway | **45%** | Quaternion test is format-fragile, no fusion validation |
| BLE communication | **55%** | Read verified, write not tested, no concurrent stress |
| File transfer | **60%** | CRC32 verified for run #0 only |
| Overall firmware quality | **~55%** | Weighted by criticality of each area |

**What's acceptable for v1 ship:**
- **Target**: 75%+ confidence across all areas, 90%+ for critical paths (state machine, start/end detection, flash storage)
- **Current gap**: ~20 percentage points below target
- **Time to close gap**: 2 weeks with prioritized work

### 7.4 Two-Week Improvement Plan

**Week 1 — Foundation (these enable everything else):**

| Day | Work | Outcome |
|-----|------|---------|
| 1–2 | R1: JSON-lines protocol in firmware (TEST_MODE only) | Structured serial output |
| 3 | R1: Harness JSON parser + typed assertions | Structured test assertions |
| 3 | R6: Assertion library (numeric, tolerance, regex) | Clean assertions |
| 4 | R3: Test isolation (teardown, setup enforcement, serial drain) | Reliable test ordering |
| 5 | R2: Replace fixed waits with polling (ring buffer, flatline) | Timing-robust tests |
| 5 | Migrate all existing tests to JSON protocol | Full backward compatibility |

**Week 2 — Coverage + Hardening:**

| Day | Work | Outcome |
|-----|------|---------|
| 6–7 | R4: P0 tests (U20 ring wraparound, U21 multi-run flash, U22 zero-length run) | Critical gaps closed |
| 8 | R4: P1 tests (U23 flash-full, U25 BLE concurrency, U26 precision threshold) | Major gaps closed |
| 9 | R4: P2 tests (U27 rapid toggle, U28 sensor timeout, U29 invalid injection) | Edge cases covered |
| 9 | Flake hunting: Run full suite 10×, fix any failures | 0 flaky tests |
| 10 | R5: Phase 1 CI (script + cron on test machine) | Automated regression testing |
| 10 | Final review: Coverage report, known gaps documented, ship/no-ship decision | Informed decision |

**Week 2 deliverable:** A test suite with:
- 0 flaky tests (proven over 10 consecutive runs)
- 75%+ confidence across all areas
- JSON-lines structured protocol
- Automated CI running on every firmware push
- Documented known gaps (what's explicitly NOT tested)

### 7.5 Final Verdict

The SGC firmware test suite is a **solid v0.5 test suite** — well-structured, sensibly architected, and covering the happy paths. It's bettr than most embedded firmware test suites I've seen. But it's not yet a **v1 ship-quality** suite.

The gaps are real and actionable: the end detector is flaky under real-world conditions (BLE active), the flash is barely tested, the bit packer is untrusted, and the sensor injection tests can pass while verifying wrong values. The string-matching foundation is the root cause of most fragility — fix that, and everything else gets easier.

**With 2 weeks of focused work following the plan above, the suite can reach ship-quality.** Without that work, you're shipping firmware that's been tested against a test suite that can pass when the firmware is broken — which is worse than no test suite at all, because it creates false confidence.

The test harness architecture (dataclass scenarios, polling, auto-discovery, Tee logging) is genuinely good and deserves to be built upon. The problems are in the protocol layer (string matching) and coverage breadth — both fixable with the recommendations above.
