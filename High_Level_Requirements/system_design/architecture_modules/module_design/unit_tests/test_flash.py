"""
Unit test: Flash Storage (v2 — JSON protocol)
    U12 — Flash self-test
    U13 — Full run cycle → flash storage (run count increments)
"""
from sgc_test_harness import TestStep, TestScenario, force_state

SCENARIOS = []

# ── U12: Flash self-test ─────────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U12 — Flash self-test",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Run flash self-test", 'f', 500,
            expect_json={"ev": "flash", "ok": True}),
    ]
))

# ── U13: Full run cycle → flash storage ──────────────────────────
SCENARIOS.append(TestScenario(
    name="U13 — Full run cycle → flash storage",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Record initial runs", '?', 300),
        TestStep("Arm + poll ring_full event", 'a', 500,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Start logging (force)", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("End run (force POST_RUN) + wait run_saved", 'p', 600,
            on_response=lambda h, _: h.wait_for_json_event("run_saved", timeout_ms=5000)),
        TestStep("Wait cooldown → IDLE",
            poll_state='IDLE', timeout_ms=15000),
        TestStep("Verify back to IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Check runs incremented", '?', 300,
            expect_json=lambda d: d.get("runs", 0) >= 1),
    ]
))
