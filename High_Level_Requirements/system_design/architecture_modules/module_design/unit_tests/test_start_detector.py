"""
Unit test: Start Detector (v2 — JSON protocol)
    U04 — 2m descent triggers LOGGING
    U05 — 3m cumulative descent triggers LOGGING
    U06 — No false trigger on flat pressure
"""
from sgc_test_harness import (TestStep, TestScenario,
    force_state, enable_test_mode, inject_pressure_ramp, wait_for_ring_full)
import time

SCENARIOS = []

# ── U04: Descent detection (pressure rise ≈ 2m drop) ─────────────
SCENARIOS.append(TestScenario(
    name="U04 — Start detection: 2m descent",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", 'T', 200,
            expect_json={"ev": "cmd", "tm": True}),
        TestStep("Set sea-level baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        # Poll ring_full event instead of fixed 5.6s wait.
        TestStep("Poll for ring_full event", None, 100,
            on_response=lambda h, _: wait_for_ring_full(h)),
        # 2m descent ≈ +24 Pa: INCREASE pressure
        TestStep("Simulate 2m descent over 1s",
            None, wait_ms=100,
            on_response=lambda h, _: inject_pressure_ramp(h, 101325, 101349, 10, 100)),
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
        # Poll ring_full event instead of fixed 5.6s wait.
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
# IMPORTANT: B 101325 BEFORE arming is essential — it syncs the start
# detector baseline with synthetic pressure.  Without this, the ~21 kPa
# gap between room pressure (~89 kPa at 1000m altitude) and synthetic
# (101.3 kPa) looks like a ~1800m descent → immediate false trigger.
SCENARIOS.append(TestScenario(
    name="U06 — No false trigger on flat pressure",
    setup_commands=['i', 'T', 'B 101325'],  # B sync in setup guards against stale synthetic P
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode (accept either toggle direction)", 'T', 200,
            expect_json={"ev": "cmd", "cmd": "T"}),
        TestStep("Sync baseline with synthetic pressure", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        # Poll the ring_full event instead of fixed-wait status check.
        # Ring fills at 100 Hz → 500 samples takes ~5 seconds.
        TestStep("Poll for ring_full event", None, 100,
            on_response=lambda h, _: wait_for_ring_full(h)),
        TestStep("Hold flat for 10s", 'B 101325', 10000),
        TestStep("Should still be ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
