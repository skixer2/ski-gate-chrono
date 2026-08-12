"""
S02 — Factory reset via serial R (destructive).

Picked up by Get-ChildItem test_*.py full loop.
Not in smoke/core (wipes runs). Run last if ordering matters.

Pass: factory_reset + reboot events, then IDLE with runs=0.
"""
from sgc_test_harness import TestStep, TestScenario

TEST_VERSION = "2.26.0"

SCENARIOS = [
    TestScenario(
        name="S02 — Factory reset (serial R)",
        setup_commands=['i'],
        teardown_commands=[],
        steps=[
            TestStep("Verify IDLE", '?', 400,
                expect_json={"ev": "status", "st": "IDLE"}),
            TestStep("Trigger factory reset", 'R', 300,
                expect_json={"ev": "factory_reset"}),
            TestStep("Wait reboot event", None, 200,
                on_response=lambda h, _: h.wait_for_json_event(
                    "reboot", timeout_ms=8000) is not None),
            # Boot + full-slot prepare can take several seconds
            TestStep("Wait post-reboot settle", None, 6000),
            TestStep("Verify IDLE after reboot", '?', 2000,
                expect_json=lambda d: (
                    d.get("ev") == "status"
                    and d.get("st") == "IDLE"
                    and int(d.get("runs") or 0) == 0
                )),
        ],
    ),
]
