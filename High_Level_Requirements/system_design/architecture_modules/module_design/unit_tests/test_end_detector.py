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
        # Poll for log_start (flash drain complete), then drain+breathe before sending commands.
        TestStep("Wait for log_start (flash drain done)", None, 300,
            on_response=lambda h, _: (
                h.wait_for_json_event("log_start", timeout_ms=15000),
                h.drain_serial(200),
                True
            )),
        # Send L and B with generous timeouts — firmware may still be busy after log_start.
        TestStep("Inject non-zero accel (simulate skiing)", 'L 500 0 0', 500,
            expect_json={"ev": "cmd", "cmd": "L"},
            timeout_ms=10000),
        TestStep("Change pressure (descending)", 'B 100900', 300,
            expect_json={"ev": "cmd", "cmd": "B"},
            timeout_ms=5000),
        TestStep("Wait 5s", None, wait_ms=5000),
        TestStep("Should still be LOGGING", '?', 300,
            expect_json={"st": "LOGGING"}),
        # Device may have auto-transitioned LOGGING→POST_RUN during the wait.
        # Use poll_state to handle either case gracefully.
        TestStep("Return to IDLE", None, 200,
            poll_state='IDLE', timeout_ms=15000),
    ]
))
