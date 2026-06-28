"""
Unit test: End Detector (v2 — JSON protocol)
    U07 — 10s flatline → POST_RUN
    U08 — No premature stop during movement
"""
from sgc_test_harness import TestStep, TestScenario, force_state, enable_test_mode

SCENARIOS = []

# ── U07: End detection (flatline) ────────────────────────────────
# Polls for end_detected JSON event instead of fixed 12s wait.
SCENARIOS.append(TestScenario(
    name="U07 — End detection: 10s flatline",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set pressure baseline", 'B 101000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 5600),
        TestStep("Verify ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Force LOGGING", 'l', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        # log_start processing (flash erase+write) can take >500ms —
        # skip ? verification here; state transition already confirmed above
        # Wait for end detector (10s stillness + poll margin)
        TestStep("Wait for end_detected event → POST_RUN",
            poll_state='POST_RUN', poll_interval_ms=300, timeout_ms=20000),
    ]
))

# ── U08: No premature stop during movement ───────────────────────
SCENARIOS.append(TestScenario(
    name="U08 — No false end during movement",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set pressure baseline", 'B 101000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 5600),
        TestStep("Verify ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Force LOGGING", 'l', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("Inject non-zero accel (simulate skiing)", 'L 500 0 0', 500,
            expect_json={"ev": "cmd", "cmd": "L"}),
        TestStep("Change pressure (descending)", 'B 100900', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Wait 5s", None, wait_ms=5000),
        TestStep("Should still be LOGGING", '?', 300,
            expect_json={"st": "LOGGING"}),
        # Reset accel to 1g → end detector should complete → POST_RUN → IDLE
        TestStep("Reset accel to 1g (stillness)", 'L 0 0 -9810', 300,
            expect_json={"ev": "cmd", "cmd": "L"}),
        TestStep("Wait POST_RUN + cooldown → IDLE", None, 200,
            poll_state='IDLE', timeout_ms=30000),
    ]
))
