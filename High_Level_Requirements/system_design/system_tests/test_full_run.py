"""
System test: End-to-End Run Cycle (v2 — JSON protocol)
    S01 — Complete ARMED → LOGGING → POST_RUN simulation
    S02 — Factory reset (⚠️ DESTRUCTIVE)
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'architecture_modules', 'module_design', 'unit_tests'))
from sgc_test_harness import (TestStep, TestScenario,
    force_state, enable_test_mode, inject_pressure_ramp)

SCENARIOS = []

# ═══════════════════════════════════════════════════════════════════
# S01: Complete run simulation (v2 — JSON events + polling)
# ═══════════════════════════════════════════════════════════════════
SCENARIOS.append(TestScenario(
    name="S01 — Complete run: ARMED → LOGGING → POST_RUN",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        # 1. Setup
        TestStep("Enable test mode", 'T', 300,
            expect_json={"ev": "cmd", "cmd": "T", "tm": True}),
        TestStep("Set sea-level pressure", 'B 101325', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Check initial state", '?', 300,
            expect_json={"st": "IDLE"}),

        # 2. Arm and poll for ring full
        TestStep("Arm device, poll ring_full", 'a', 500,
            on_response=lambda h, _: h.wait_for_json_event("ring_full", timeout_ms=12000)),
        TestStep("Verify ring buffer full", '?', 300,
            expect_json={"r": 500}),

        # 3. Simulate descent (2m over 1 second)
        TestStep("Simulate 2m descent", None, 100,
            on_response=lambda h, _: inject_pressure_ramp(h, 101325, 101288, 10, 100)),
        TestStep("Wait for LOGGING transition", poll_state='LOGGING', timeout_ms=5000),

        # 4. Log data (simulate 5 seconds of skiing)
        TestStep("Log 5s flat pressure", 'B 101288', 5000),

        # 5. Poll for end_detected event
        TestStep("Wait for end_detected → POST_RUN",
            poll_state='POST_RUN', poll_interval_ms=300, timeout_ms=20000),

        # 6. Wait for run_saved event + cooldown
        TestStep("Wait for run_saved event", None, 200,
            on_response=lambda h, _: h.wait_for_json_event("run_saved", timeout_ms=5000)),
        TestStep("Wait cooldown → IDLE", poll_state='IDLE', timeout_ms=15000),

        # 7. Flash integrity check
        TestStep("Flash self-test", 'f', 500,
            expect_json={"ev": "flash", "ok": True}),
    ]
))

# ═══════════════════════════════════════════════════════════════════
# S02: Factory reset (⚠️ DESTRUCTIVE — run last)
# ═══════════════════════════════════════════════════════════════════
SCENARIOS.append(TestScenario(
    name="S02 — Factory reset (⚠️ DESTRUCTIVE)",
    setup_commands=['i'],
    teardown_commands=[],
    steps=[
        TestStep("Verify in IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Trigger factory reset", 'R', 500,
            expect_json={"ev": "factory_reset"}),
        TestStep("Wait for reboot event", None, 500,
            on_response=lambda h, _: h.wait_for_json_event("reboot", timeout_ms=5000)),
        TestStep("Device reboots — wait", None, wait_ms=5000),
        TestStep("Verify rebooted to IDLE", '?', 3000,
            expect_json={"st": "IDLE"}),
    ]
))
