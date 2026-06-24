"""
Acceptance test: Requirements Validation (v2 — JSON protocol)

    Maps REQ-FUNC requirements to test cases.
    A01 — F01/F02: Sensor acquisition + ring buffer
    A02 — F04/F06: Start + end detection
    A03 — F07: Bit-packing + flash storage
    A04 — F12/F13: Sleep/wake (serial commands only)
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'system_design',
    'architecture_modules', 'module_design', 'unit_tests'))
from sgc_test_harness import (TestStep, TestScenario,
    force_state, enable_test_mode, inject_pressure_ramp)

SCENARIOS = []

# A01: F01-F02 — Sensor acquisition + ring buffer
SCENARIOS.append(TestScenario(
    name="A01 — F01/F02: Sensor acquisition + ring buffer",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Arm + poll ring_full", 'a', 500,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Verify 500 samples (F01: 100 Hz × 5s)", '?', 300,
            expect_json={"r": 500, "rm": 500}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# A02: F04-F06 — Start/end detection
SCENARIOS.append(TestScenario(
    name="A02 — F04/F06: Start + end detection",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Arm", 'a', 5600),
        TestStep("Simulate descent (F04)", None, 100,
            on_response=lambda h, _: inject_pressure_ramp(h, 101325, 101288, 10, 100)),
        TestStep("Wait for LOGGING", poll_state='LOGGING', timeout_ms=5000),
        TestStep("End via POST_RUN (F06)", 'p', 600),
        TestStep("Verify POST_RUN", '?', 300,
            expect_json={"st": "POST_RUN"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "POST_RUN", "to": "IDLE"}),
    ]
))

# A03: F07 — Bit-packing + flash storage
SCENARIOS.append(TestScenario(
    name="A03 — F07: Bit-packing + flash storage",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Arm + poll ring_full", 'a', 500,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Force logging", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("End run", 'p', 600),
        TestStep("Poll for run_saved (F07: data compressed + stored)", None, 200,
            on_response=lambda h, _: h.wait_for_json_event("run_saved", timeout_ms=5000)),
        TestStep("Flash self-test", 'f', 500,
            expect_json={"ev": "flash", "ok": True}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "POST_RUN", "to": "IDLE"}),
    ]
))

# A04: F12-F13 — Sleep/wake
SCENARIOS.append(TestScenario(
    name="A04 — F12/F13: Sleep/wake (⚠️ LDC1612 hardware needed for F13)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Force SLEEP", 's', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "SLEEP"}),
        TestStep("Verify SLEEP", '?', 300,
            expect_json={"st": "SLEEP"}),
        TestStep("Wake to IDLE", 'i', 600,
            expect_json={"ev": "st", "from": "SLEEP", "to": "IDLE"}),
        TestStep("Verify awake", '?', 300,
            expect_json={"st": "IDLE"}),
    ]
))
