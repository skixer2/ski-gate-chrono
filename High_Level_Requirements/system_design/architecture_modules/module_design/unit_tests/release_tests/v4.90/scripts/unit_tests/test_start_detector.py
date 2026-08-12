"""
Unit test: Start Detector (v2.20 — drop-only, Opt-A pre-roll)
    U04 — 2.5m descent triggers LOGGING
    U05 — 3m cumulative descent triggers LOGGING
    U06 — No false trigger on flat pressure

Does NOT wait for ring_full (3000). Uses wait_for_ring_count so P0 is
latched from injected Pa before the pressure ramp.
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    inject_pressure_ramp, wait_for_ring_count, UNIT_RING_READY,
)

TEST_VERSION = "2.22.0"

SCENARIOS = []

# ── U04: Descent detection (pressure rise ≈ 2.5m drop) ─────────────
SCENARIOS.append(TestScenario(
    name="U04 — Start detection: 2.5m descent",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode + defaults", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set sea-level baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait for ring samples (P0 latch)", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        # 2.5m descent ≈ +30 Pa (30/12 = 2.5m > 2.0m threshold)
        TestStep("Simulate 2.5m descent over 1s",
            None, wait_ms=100,
            on_response=lambda h, _: inject_pressure_ramp(
                h, 101325, 101355, 10, 100)),
        TestStep("Wait for LOGGING transition",
            poll_state='LOGGING', timeout_ms=8000),
        # Drain LOGGING → POST_RUN → IDLE so next scenario starts clean
        TestStep("Force POST_RUN cleanup", 'p', 500,
            expect_json={"ev": "st", "from": "LOGGING", "to": "POST_RUN"}),
        TestStep("Wait IDLE after cooldown",
            poll_state='IDLE', timeout_ms=20000),
    ]
))

# ── U05: Cumulative descent (3m drop) ────────────────────────────
SCENARIOS.append(TestScenario(
    name="U05 — Start detection: 3m cumulative descent",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode + defaults", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait for ring samples (P0 latch)", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        # 3m cumulative drop ≈ +36 Pa
        TestStep("Simulate slow 3m descent over 3s",
            None, wait_ms=100,
            on_response=lambda h, _: inject_pressure_ramp(
                h, 101325, 101361, 30, 100)),
        TestStep("Wait for LOGGING transition",
            poll_state='LOGGING', timeout_ms=8000),
        TestStep("Force POST_RUN cleanup", 'p', 500,
            expect_json={"ev": "st", "from": "LOGGING", "to": "POST_RUN"}),
        TestStep("Wait IDLE after cooldown",
            poll_state='IDLE', timeout_ms=20000),
    ]
))

# ── U06: No trigger on flat pressure ─────────────────────────────
SCENARIOS.append(TestScenario(
    name="U06 — No false trigger on flat pressure",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode + defaults", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Sync baseline with synthetic pressure", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait for ring samples", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Hold flat for 5s", 'B 101325', 5000),
        TestStep("Should still be ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
