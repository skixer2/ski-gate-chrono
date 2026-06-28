"""
Unit test: Start Detector (v2.2 — drop-only)
    U04 — 2.5m descent triggers LOGGING
    U05 — 3m cumulative descent triggers LOGGING
    U06 — No false trigger on flat pressure
"""
from sgc_test_harness import (TestStep, TestScenario,
    force_state, enable_test_mode, inject_pressure_ramp, wait_for_ring_full)
import time

SCENARIOS = []

# ── U04: Descent detection (pressure rise ≈ 2.5m drop) ─────────────
SCENARIOS.append(TestScenario(
    name="U04 — Start detection: 2.5m descent",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", 'T', 200,
            expect_json={"ev": "cmd", "tm": True}),
        TestStep("Set sea-level baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Poll for ring_full event", None, 100,
            on_response=lambda h, _: wait_for_ring_full(h)),
        # 2.5m descent ≈ +30 Pa (30/12 = 2.5m > 2.0m threshold)
        TestStep("Simulate 2.5m descent over 1s",
            None, wait_ms=100,
            on_response=lambda h, _: inject_pressure_ramp(h, 101325, 101355, 10, 100)),
        TestStep("Wait for LOGGING transition",
            poll_state='LOGGING', timeout_ms=5000),
    ]
))

# ── U05: Cumulative descent (3m drop) ────────────────────────────
SCENARIOS.append(TestScenario(
    name="U05 — Start detection: 3m cumulative descent",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", 'T', 200,
            expect_json={"ev": "cmd", "tm": True}),
        TestStep("Set baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Poll for ring_full event", None, 100,
            on_response=lambda h, _: wait_for_ring_full(h)),
        # 3m cumulative drop ≈ +36 Pa
        TestStep("Simulate slow 3m descent over 3s",
            None, wait_ms=100,
            on_response=lambda h, _: inject_pressure_ramp(h, 101325, 101361, 30, 100)),
        TestStep("Wait for LOGGING transition",
            poll_state='LOGGING', timeout_ms=6000),
    ]
))

# ── U06: No trigger on flat pressure ─────────────────────────────
SCENARIOS.append(TestScenario(
    name="U06 — No false trigger on flat pressure",
    setup_commands=['i', 'T', 'B 101325'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode (accept either toggle direction)", 'T', 200,
            expect_json={"ev": "cmd", "cmd": "T"}),
        TestStep("Sync baseline with synthetic pressure", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Poll for ring_full event", None, 100,
            on_response=lambda h, _: wait_for_ring_full(h)),
        TestStep("Hold flat for 10s", 'B 101325', 10000),
        TestStep("Should still be ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
