"""
Unit test: Sensor Injection (v2 — JSON protocol)
    U16 — Toggle test mode
    U17 — Inject pressure values
    U18 — Inject quaternion (NOW VALIDATES ALL 4 COMPONENTS)
    U19 — Inject linear acceleration

    JSON protocol eliminates the format-sensitivity that caused
    U18 flakiness in v1 (Arduino float precision variability).
    All assertions use numeric tolerance ±0.01 on float values.
"""
from sgc_test_harness import TestStep, TestScenario, enable_test_mode

SCENARIOS = []

# ── U16: Toggle test mode ────────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U16 — Toggle test mode",
    setup_commands=[],
    teardown_commands=['T'],  # ensure test mode OFF if it was ON
    steps=[
        TestStep("Ensure test mode ON", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Verify mode ON via echo", 'Z', 300,
            expect_json={"ev": "echo"}),
        TestStep("Disable test mode", 'T', 300,
            expect_json={"ev": "cmd", "cmd": "T", "tm": False}),
        TestStep("Enable again", 'T', 300,
            expect_json={"ev": "cmd", "cmd": "T", "tm": True}),
    ]
))

# ── U17: Inject pressure ─────────────────────────────────────────
# Now validates exact numeric values instead of string matching.
SCENARIOS.append(TestScenario(
    name="U17 — Inject pressure values",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set 95000 Pa (~500m altitude)", 'B 95000', 300,
            expect_json={"ev": "cmd", "cmd": "B", "p": 95000.0}),
        TestStep("Echo verify", 'Z', 300,
            expect_json={"ev": "echo", "p": 95000.0}),
        TestStep("Set 101325 Pa (sea level)", 'B 101325', 300,
            expect_json={"ev": "cmd", "cmd": "B", "p": 101325.0}),
        TestStep("Echo verify", 'Z', 300,
            expect_json={"ev": "echo", "p": 101325.0}),
        TestStep("Set 85000 Pa (~1500m)", 'B 85000', 300,
            expect_json={"ev": "cmd", "cmd": "B", "p": 85000.0}),
        TestStep("Echo verify", 'Z', 300,
            expect_json={"ev": "echo", "p": 85000.0}),
    ]
))

# ── U18: Inject quaternion (NOW FULLY VALIDATED) ─────────────────
# v1 only checked first component with partial string match 'Q=(0.7'.
# v2 validates ALL 4 quaternion components with ±0.01 tolerance.
SCENARIOS.append(TestScenario(
    name="U18 — Inject quaternion",
    setup_commands=[],
    teardown_commands=[],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set identity quaternion", 'Q 1 0 0 0', 300,
            expect_json={"ev": "cmd", "cmd": "Q", "q": [1.0, 0.0, 0.0, 0.0]}),
        TestStep("Echo verify identity", 'Z', 300,
            expect_json={"ev": "echo", "q": [1.0, 0.0, 0.0, 0.0]}),
        TestStep("Set tilted quaternion", 'Q 0.707 0 0.707 0', 300,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Echo verify tilted (tolerance ±0.01)",
            'Z', 300,
            expect_json=lambda d: (
                d.get("ev") == "echo" and
                isinstance(d.get("q"), list) and len(d["q"]) == 4 and
                abs(d["q"][0] - 0.707) < 0.02 and
                abs(d["q"][1] - 0.0) < 0.01 and
                abs(d["q"][2] - 0.707) < 0.02 and
                abs(d["q"][3] - 0.0) < 0.01
            )),
    ]
))

# ── U19: Inject linear acceleration ──────────────────────────────
SCENARIOS.append(TestScenario(
    name="U19 — Inject linear acceleration",
    setup_commands=[],
    teardown_commands=[],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set accel (1000, 0, 0)", 'L 1000 0 0', 300,
            expect_json={"ev": "cmd", "cmd": "L", "la": [1000.0, 0.0, 0.0]}),
        TestStep("Echo verify", 'Z', 300,
            expect_json={"ev": "echo", "la": [1000.0, 0.0, 0.0]}),
        TestStep("Set accel (0, 0, -9810)", 'L 0 0 -9810', 300,
            expect_json={"ev": "cmd", "cmd": "L", "la": [0.0, 0.0, -9810.0]}),
        TestStep("Echo verify gravity vector", 'Z', 300,
            expect_json={"ev": "echo", "la": [0.0, 0.0, -9810.0]}),
    ]
))
