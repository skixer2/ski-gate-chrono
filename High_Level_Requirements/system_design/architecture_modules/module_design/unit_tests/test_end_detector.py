"""
Unit test: End Detector (v2.20 — natural LOGGING entry)

    U07 — Flat pressure → POST_RUN after 5 s window
    U08 — No premature stop during ascent (dp < 0), then end when flat

IMPORTANT: force-'l' sets g_force_logging and SKIPS end detector (S04).
These tests enter LOGGING via start detector so end det stays active.
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, inject_pressure_ramp, UNIT_RING_READY,
)

TEST_VERSION = "2.21.0"

SCENARIOS = []


def _arm_and_start_logging(h, p0=101000.0, drop_pa=36.0) -> bool:
    """ARMED → wait ring → pressure ramp → LOGGING (end det ON)."""
    if not wait_for_ring_count(h, min_r=UNIT_RING_READY, timeout_ms=12000):
        return False
    inject_pressure_ramp(h, p0, p0 + drop_pa, 12, 100)
    return h.wait_for_state('LOGGING', timeout_ms=8000)


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

# ── U08: Pressure decrease (ascent) → dp < 0 → no end ────────────
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
        # Drop pressure 100 Pa → ascent. dp < 0 → NOT triggered.
        TestStep("Simulate pressure drop (ascent)", 'B 100900', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Wait 3 s (window still has higher P → dp<0)", None, wait_ms=3000),
        TestStep("Should still be LOGGING (dp < 0)", '?', 300,
            expect_json={"st": "LOGGING"}),
        # Stabilize at low pressure → dp→0 → end → cooldown → IDLE
        TestStep("Hold flat at ascent level", 'B 100900', 200),
        TestStep("Wait POST_RUN + cooldown → IDLE",
            poll_state='IDLE', timeout_ms=30000),
    ]
))
