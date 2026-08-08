"""
Unit test: State Machine (v2.20 — Opt-A)
    U01 — SLEEP ↔ IDLE
    U02 — IDLE → ARMED → IDLE
    U03 — Full state cycle via natural start + end detectors

force-'l' skips end det (S04). U03 uses start/end detectors so the
full production path IDLE→ARMED→LOGGING→POST_RUN→IDLE is covered.
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, inject_pressure_ramp, UNIT_RING_READY,
)

TEST_VERSION = "2.21.0"

SCENARIOS = []

# ── U01: SLEEP → IDLE → SLEEP ───────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U01 — SLEEP ↔ IDLE",
    setup_commands=['s'],
    teardown_commands=['i'],
    steps=[
        TestStep("Verify SLEEP", '?', 300,
            expect_json={"st": "SLEEP"}),
        TestStep("Wake to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "SLEEP", "to": "IDLE"}),
        TestStep("Verify IDLE via status", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Back to SLEEP", 's', 400,
            expect_json={"ev": "st", "from": "IDLE", "to": "SLEEP"}),
        TestStep("Verify SLEEP again", '?', 300,
            expect_json={"st": "SLEEP"}),
    ]
))

# ── U02: IDLE → ARMED → IDLE ────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U02 — IDLE → ARMED → IDLE",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Verify IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Verify ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
        TestStep("Verify IDLE restored", '?', 300,
            expect_json={"st": "IDLE"}),
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
        TestStep("Verify IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Arm → wait for state transition", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
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
        TestStep("Wait cooldown → IDLE",
            poll_state='IDLE', poll_interval_ms=300, timeout_ms=20000),
        TestStep("Verify back to IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
    ]
))
