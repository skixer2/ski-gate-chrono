"""
Unit test: State Machine (v2.20 — Opt-A)
    U01 — SLEEP ↔ SLEEP
    U02 — SLEEP → ARMED → SLEEP
    U03 — Full state cycle via natural start + end detectors
    U20 — ARM_TIMEOUT → SLEEP (~30 s, no start)  [full tier only]

force-'l' skips end det (S04). U03 uses start/end detectors so the
full production path SLEEP→ARMED→LOGGING→POST_RUN→SLEEP is covered.
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, inject_pressure_ramp, UNIT_RING_READY,
)

TEST_VERSION = "2.26.0"

SCENARIOS = []

# ── U01: SLEEP → SLEEP → SLEEP ───────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U01 — SLEEP ↔ SLEEP",
    setup_commands=['s'],
    teardown_commands=['i'],
    steps=[
        TestStep("Verify SLEEP", '?', 300,
            expect_json={"st": "SLEEP"}),
        TestStep("Wake to SLEEP", 'i', 400,
            expect_json={"ev": "st", "from": "SLEEP", "to": "SLEEP"}),
        TestStep("Verify SLEEP via status", '?', 300,
            expect_json={"st": "SLEEP"}),
        TestStep("Back to SLEEP", 's', 400,
            expect_json={"ev": "st", "from": "SLEEP", "to": "SLEEP"}),
        TestStep("Verify SLEEP again", '?', 300,
            expect_json={"st": "SLEEP"}),
    ]
))

# ── U02: SLEEP → ARMED → SLEEP ────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U02 — SLEEP → ARMED → SLEEP",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Verify SLEEP", '?', 300,
            expect_json={"st": "SLEEP"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "SLEEP", "to": "ARMED"}),
        TestStep("Verify ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to SLEEP", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "SLEEP"}),
        TestStep("Verify SLEEP restored", '?', 300,
            expect_json={"st": "SLEEP"}),
    ]
))

# ── U03: Full state cycle (natural detectors) ────────────────────
SCENARIOS.append(TestScenario(
    name="U03 — Full state cycle",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Verify test mode ON (echo JSON)", 'Z', 300,
            expect_json={"ev": "echo"}),
        TestStep("Verify SLEEP", '?', 300,
            expect_json={"st": "SLEEP"}),
        TestStep("Arm → wait for state transition", 'a', 500,
            expect_json={"ev": "st", "from": "SLEEP", "to": "ARMED"}),
        TestStep("Wait ring samples for P0", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Descent → LOGGING via start det", None, 100,
            on_response=lambda h, _: (
                inject_pressure_ramp(h, 101325, 101361, 12, 100)
                and h.wait_for_state('LOGGING', timeout_ms=8000)
            )),
        TestStep("Verify LOGGING", '?', 300,
            expect_json={"st": "LOGGING"}),
        # Flat pressure → end detector (~5 s window) → POST_RUN
        TestStep("Hold flat for end detector", 'B 101361', 200),
        TestStep("Wait stillness → POST_RUN",
            poll_state='POST_RUN', poll_interval_ms=300, timeout_ms=25000),
        TestStep("Verify POST_RUN", '?', 200,
            expect_json={"st": "POST_RUN"}),
        TestStep("Wait cooldown → SLEEP",
            poll_state='SLEEP', poll_interval_ms=300, timeout_ms=20000),
        TestStep("Verify back to SLEEP", '?', 300,
            expect_json={"st": "SLEEP"}),
    ]
))

# ── U20: Aborted start — ARM_TIMEOUT (~30 s) → SLEEP (R02) ────────
# Long test (~35 s). Not in smoke/core; full tier only.
SCENARIOS.append(TestScenario(
    name="U20 — ARM_TIMEOUT → SLEEP (no start)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode (manual frame, no stream)", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "SLEEP", "to": "ARMED"}),
        # Flat inject at sea level — start_det must not fire
        TestStep("Hold flat (no descent)", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Wait ARM_TIMEOUT → SLEEP (~30 s)",
            poll_state='SLEEP', poll_interval_ms=500, timeout_ms=40000),
        TestStep("Verify SLEEP after timeout", '?', 400,
            expect_json={"st": "SLEEP"}),
    ]
))
