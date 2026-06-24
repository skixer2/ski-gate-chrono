"""
Unit test: Bit Packer (v2 — JSON protocol)
    U14 — Compression exercised during run
    U15 — All packet types exercised (T1 coasting, T3 anchor)
"""
from sgc_test_harness import TestStep, TestScenario, force_state, enable_test_mode

SCENARIOS = []

# ── U14: Bit packer exercised via full run ───────────────────────
SCENARIOS.append(TestScenario(
    name="U14 — Compression exercised during run",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Set identity quat + zero accel", 'Q 1 0 0 0', 200,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Set zero linear accel", 'L 0 0 0', 200,
            expect_json={"ev": "cmd", "cmd": "L"}),
        TestStep("Set sea-level pressure", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm + poll ring_full", 'a', 500,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Verify ring full", '?', 300,
            expect_json={"r": 500}),
        TestStep("Force LOGGING", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("Force POST_RUN", 'p', 500),
        TestStep("Verify POST_RUN", '?', 300,
            expect_json={"st": "POST_RUN"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "POST_RUN", "to": "IDLE"}),
    ]
))

# ── U15: Different sensor values exercise all packet types ───────
SCENARIOS.append(TestScenario(
    name="U15 — All packet types exercised",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        # Type 1: small deltas (coasting)
        TestStep("Set gentle rotation (T1 packets)", 'Q 0.9 0.1 0.0 0.4', 200,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Arm", 'a', 2500),
        TestStep("Check ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("→ IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
        # Type 3: large jump (impact anchor)
        TestStep("Set large rotation change (T3 anchor)", 'Q 0.5 0.5 0.5 0.5', 200,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Arm again", 'a', 2500),
        TestStep("Check ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
