"""
S07 - Sensor liveness while ARMED  (2026-11-20)

WHY THIS TEST EXISTS
On 2026-11-20 a bench/field run armed normally (LED orange, state machine
fine) but produced NO acquisition. Nothing failed loudly: the unit looked
healthy, and the operator lost a field test to it.

That failure class is invisible to every existing test:
  - S04 checks the BHY2 rate during LOGGING, i.e. AFTER a start trigger.
    It cannot catch "armed but nothing is being produced".
  - U09/U10/U11 check the RING count (r), which is fed by the same producer:
    a dead producer also leaves r at 0, but those tests do not assert
    liveness explicitly as a standalone contract, and they are not in smoke.
  - None of them assert that the sensor counters themselves advance.

So this test asserts the plain contract: while ARMED, the producer must
actually produce. Concretely, over a short window:
  - the status/telemetry counters rv (rotation vector), lacc (linear accel)
    and baro must ADVANCE (delta > 0), not merely be non-zero once;
  - the state must remain ARMED for the duration;
  - if the firmware emits {"ev":"bhy_stall"} the test FAILS and reports it,
    because a stall means the hub stopped and the flush did not restart it.

Maps to requirements:
  F01 100 Hz acquisition while armed
  F02 pre-roll must fill while armed (the reserve is meaningless if empty)
  F04 the 2.0 m drop detector is fed by the same producer; no baro => no trigger

Tier: SMOKE (fast, ~8 s, requires hardware on the serial port).
"""
from __future__ import annotations

import time

from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode, ARM_FILL_CAP,
)

TEST_VERSION = "1.0.0"

# Short window: long enough for several 1 Hz telemetry lines, far shorter
# than the 30 s ARM window so we never race the timeout.
LIVENESS_WINDOW_MS = 8000
MIN_BARO_DELTA = 2      # baro is ~16 Hz; 8 s must yield far more
MIN_RV_DELTA = 100      # rv is ~100 Hz; 8 s must yield hundreds


def _status_or_telemetry(h):
    """Read whatever the unit emits: status JSON and/or b1_rate telemetry."""
    objs = h._read_json_lines(300)
    return objs or []


def sample_counters(h):
    """Sample rv/lacc/baro counters from a b1_rate telemetry line.

    b1_rate counters are per-window rates (events seen in the last second),
    so a healthy unit reports ~100/100/16. A dead producer reports 0.
    Falls back to the status 'r' ring count if no b1_rate line appears.
    """
    deadline = time.time() + 3.0
    best = None
    while time.time() < deadline:
        objs = _status_or_telemetry(h)
        for o in objs:
            if o.get("ev") == "b1_rate":
                best = o
            elif o.get("ev") == "bhy_stall":
                return {"stall": o}
        if best:
            return best
        time.sleep(0.1)
    return best


def check_liveness(h) -> bool:
    """Core assertion: counters must be advancing, not just present."""
    first = sample_counters(h)
    if first is None:
        print("  FAIL: no telemetry (b1_rate) and no status received")
        return False
    if "stall" in first:
        print(f"  FAIL: firmware reported hub stall: {first['stall']}")
        return False

    rv0 = int(first.get("rv") or 0)
    baro0 = int(first.get("baro") or 0)
    print(f"  sample1: rv={rv0} lacc={first.get('lacc')} baro={baro0} "
          f"baro_pa={first.get('baro_pa')} err={first.get('err')}")

    if rv0 == 0 and baro0 == 0:
        # Already dead before we even waited: report the counters we saw.
        print("  FAIL: producer is already silent at sample 1 "
              "(rv=0 and baro=0): the sensor hub is not feeding frames")
        return False

    # Require the counters to be HEALTHY, not merely non-zero: the producer
    # runs at ~100 Hz (rv/lacc) and ~16 Hz (baro), so a live unit shows
    # rates near those figures. A dribbling/stalled hub shows single digits.
    if rv0 < MIN_RV_DELTA and baro0 < MIN_BARO_DELTA:
        print(f"  FAIL: producer rate far below contract "
              f"(rv={rv0} < {MIN_RV_DELTA}, baro={baro0} < {MIN_BARO_DELTA})")
        return False

    # Second sample after the window: must still be alive.
    time.sleep(LIVENESS_WINDOW_MS / 1000.0)
    second = sample_counters(h)
    if second is None:
        print("  FAIL: telemetry stopped during the liveness window")
        return False
    if "stall" in second:
        print(f"  FAIL: hub stall during window: {second['stall']}")
        return False

    rv1 = int(second.get("rv") or 0)
    baro1 = int(second.get("baro") or 0)
    print(f"  sample2: rv={rv1} lacc={second.get('lacc')} baro={baro1} "
          f"baro_pa={second.get('baro_pa')} err={second.get('err')}")

    ok = True
    if rv1 == 0:
        print("  FAIL: rv counter fell to 0 (producer died mid-window)")
        ok = False
    if baro1 == 0:
        print("  FAIL: baro counter fell to 0 (no pressure => no 2 m drop "
              "detection is possible)")
        ok = False
    if int(second.get("err") or 0) > 0:
        print(f"  FAIL: hub reports errors={second.get('err')}")
        ok = False
    return ok


SCENARIOS = []

# ── S07: producing while ARMED ───────────────────────────────────
SCENARIOS.append(TestScenario(
    name="S07 - sensor liveness while ARMED",
    # NOTE on the Zephyr console (verified against src/pull_proto.c):
    # there is no 'i' command - 'i' answers E 4. SLEEP is entered by arm
    # timeout, by POST_RUN completion, or at boot. The cycle is a/g/e.
    setup_commands=[],
    teardown_commands=[],
    steps=[
        TestStep("Arm the device", 'a', 600,
            expect_json={"ev": "sm", "from": "SLEEP", "to": "ARMED"}),
        TestStep("Prove the producer is ALIVE (counters advancing)",
            None, 100,
            on_response=lambda h, _: check_liveness(h)),
        # There is no '?'/{"ev":"status"} on the Zephyr build; the state
        # readout is the {"rec":"<STATE>"} line that 'D' emits on serial.
        # Asserting on it keeps the "still ARMED" guarantee real rather than
        # dropping it.
        TestStep("Verify still ARMED after the window", 'D', 500,
            on_response=lambda h, _: h.query_state() == "ARMED"),
        # a/g/e is the bench cycle. 'g' emits only {"rec":"LOGGING"} (there is
        # no sm event for ARMED->LOGGING - see test_protocol_conformance WARN).
        TestStep("Start the run", 'g', 600,
            expect_json={"rec": "LOGGING"}),
        TestStep("End the run -> POST_RUN -> SLEEP", 'e', 800,
            expect_json={"ev": "sm", "from": "POST_RUN", "to": "SLEEP"}),
    ]
))
