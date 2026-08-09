"""
Unit test: End Detector (v2.20 — natural LOGGING entry)

    U07 — Flat pressure → POST_RUN after 5 s window
    U08 — No premature stop during ascent (dp < 0), then end when flat

IMPORTANT: force-'l' sets g_force_logging and SKIPS end detector (S04).
These tests enter LOGGING via start detector so end det stays active.
"""
import time
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, inject_pressure_ramp, UNIT_RING_READY,
)

TEST_VERSION = "2.23.0"

SCENARIOS = []


def _arm_and_start_logging(h, p0=101000.0, drop_pa=36.0) -> bool:
    """ARMED → wait ring → pressure ramp → LOGGING (end det ON)."""
    if not wait_for_ring_count(h, min_r=UNIT_RING_READY, timeout_ms=12000):
        return False
    inject_pressure_ramp(h, p0, p0 + drop_pa, 12, 100)
    return h.wait_for_state('LOGGING', timeout_ms=8000)


def _keep_ascent(h, start_pa: float = 100900.0, steps: int = 8,
                 step_pa: float = -8.0, step_delay_ms: int = 400) -> bool:
    """Keep pressure falling so end-det window always has dp < 0.

    A single step then hold becomes flat after WINDOW fills (~5 s) and
    correctly ends the run (production). U08 must keep ascending.
    """
    pa = start_pa
    for _ in range(steps):
        h.send(f'B {pa}')
        time.sleep(step_delay_ms / 1000.0)
        pa += step_pa
    st = h.query_status_json() or {}
    return st.get('st') == 'LOGGING'


# ── U07: Constant pressure → dp = 0 → end after 10 samples ───────
SCENARIOS.append(TestScenario(
    name="U07 — End detection: flat (dp=0)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set pressure baseline", 'B 101000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Enter LOGGING via start detector", None, 100,
            on_response=lambda h, _: _arm_and_start_logging(h, 101000.0, 36.0)),
        # Hold flat: end det samples 0.5 Hz × 10 = 5 s window, dp=0 → end
        TestStep("Hold flat pressure", 'B 101036', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Wait for end_detected → POST_RUN",
            poll_state='POST_RUN', poll_interval_ms=300, timeout_ms=20000),
        TestStep("Wait cooldown → IDLE",
            poll_state='IDLE', timeout_ms=20000),
    ]
))

# ── U08: Ongoing ascent (falling P) → dp < 0 → no end ───────────
SCENARIOS.append(TestScenario(
    name="U08 — No false end during ascent (dp < 0)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set pressure baseline", 'B 101000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Enter LOGGING via start detector", None, 100,
            on_response=lambda h, _: _arm_and_start_logging(h, 101000.0, 36.0)),
        # Keep P falling for >5 s window so oldest > current (ascent).
        TestStep("Keep ascending (falling P)", None, 100,
            on_response=lambda h, _: _keep_ascent(h, 100900.0, 10, -6.0, 450)),
        TestStep("Still LOGGING under ascent", '?', 300,
            expect_json={"st": "LOGGING"}),
        # Flatten → dp→0 over 5 s window → end → cooldown → IDLE
        TestStep("Hold flat at ascent level", 'B 100840', 200),
        TestStep("Wait POST_RUN + cooldown → IDLE",
            poll_state='IDLE', timeout_ms=30000),
    ]
))
