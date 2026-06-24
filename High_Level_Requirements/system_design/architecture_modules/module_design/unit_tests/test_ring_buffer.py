"""
Unit test: Ring Buffer (v2 — JSON protocol)
    U09 — Fills to 500 in ARMED
    U10 — Stays at 500 while ARMED
    U11 — Resets on re-arm
"""
from sgc_test_harness import TestStep, TestScenario, force_state, enable_test_mode

SCENARIOS = []

# ── U09: Ring buffer fills in ARMED ──────────────────────────────
# Polls for ring_full event instead of fixed 6s wait.
SCENARIOS.append(TestScenario(
    name="U09 — Ring buffer fills to 500",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode + reset defaults", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set sea-level baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 500),
        # Poll for ring_full event (no fixed wait)
        TestStep("Poll for ring_full event", None, 100,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Verify ring full via status", '?', 300,
            expect_json={"r": 500, "rm": 500}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── U10: Ring buffer stays at 500 while ARMED ────────────────────
SCENARIOS.append(TestScenario(
    name="U10 — Ring buffer stays at 500",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Arm device", 'a', 500),
        TestStep("Poll ring_full + verify r=rm", None, 200,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Verify ring full", '?', 300,
            expect_json={"r": 500, "rm": 500}),
        TestStep("Wait 3s more", None, wait_ms=3000),
        TestStep("Still 500/500", '?', 300,
            expect_json={"r": 500, "rm": 500}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── U11: Ring buffer resets on new arm ───────────────────────────
SCENARIOS.append(TestScenario(
    name="U11 — Ring buffer resets on re-arm",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Arm first time", 'a', 6000),
        TestStep("Verify full", '?', 300,
            expect_json={"r": 500}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
        TestStep("Arm second time", 'a', 2500),
        TestStep("Ring should be < 500 (still filling)",
            '?', 300,
            expect_json=lambda d: d.get("r", 0) < 500),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
