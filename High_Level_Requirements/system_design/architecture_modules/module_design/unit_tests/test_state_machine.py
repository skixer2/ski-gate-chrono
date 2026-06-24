"""
Unit test: State Machine (v2 — JSON protocol)
    U01 — SLEEP ↔ IDLE
    U02 — IDLE → ARMED → IDLE
    U03 — Full state cycle: IDLE→ARMED→LOGGING→POST_RUN→IDLE
"""
from sgc_test_harness import TestStep, TestScenario, force_state, enable_test_mode

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

# ── U03: Full state cycle ────────────────────────────────────────
# Polls for state transitions instead of fixed waits.
# Postsynthetic (0,0,0) LA + test mode → end detector fires ~10s.
# POST_RUN → IDLE: auto after cooldown.
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
        TestStep("Force LOGGING", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("Verify LOGGING", '?', 300,
            expect_json={"st": "LOGGING"}),
        # Wait for end detector (10s stillness) → POST_RUN
        TestStep("Wait stillness → POST_RUN",
            poll_state='POST_RUN', poll_interval_ms=300, timeout_ms=25000),
        TestStep("Verify POST_RUN", '?', 200,
            expect_json={"st": "POST_RUN"}),
        # Wait cooldown → IDLE
        TestStep("Wait cooldown → IDLE",
            poll_state='IDLE', poll_interval_ms=300, timeout_ms=25000),
        TestStep("Verify back to IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
    ]
))
