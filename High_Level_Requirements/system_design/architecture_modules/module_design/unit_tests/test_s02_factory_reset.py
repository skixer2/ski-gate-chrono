"""
S02 — Factory reset via serial R (destructive).

Picked up by Get-ChildItem test_*.py full loop.
Not in smoke/core (wipes runs).

Pass criteria (device):
  - factory_reset (+ optional reboot) ACK
  - after settle: IDLE, runs=0, total_runs=0

Note: FW emits factory_reset then reboot in one burst; do not require a
second wait for reboot after the command step already drained it.
"""
from sgc_test_harness import TestStep, TestScenario

TEST_VERSION = "2.26.1"


def _reset_and_catch_events(h, _):
    """Send R; accept factory_reset and/or reboot in the same burst."""
    h._flush()
    h.send('R')
    objs = h._read_json_lines(3000)
    # Reboot may cut USB briefly; keep a short extra window
    if not any(o.get('ev') == 'factory_reset' for o in objs):
        objs.extend(h._read_json_lines(2000))
    evs = {o.get('ev') for o in objs}
    if h.verbose:
        for o in objs[:6]:
            print(f"    ← {o}")
    # factory_reset is required; reboot is best-effort (may be lost on CDC reset)
    return 'factory_reset' in evs


SCENARIOS = [
    TestScenario(
        name="S02 — Factory reset (serial R)",
        setup_commands=['i'],
        teardown_commands=[],
        steps=[
            TestStep("Verify IDLE", '?', 400,
                expect_json={"ev": "status", "st": "IDLE"}),
            TestStep("Trigger factory reset", None, 100,
                on_response=_reset_and_catch_events),
            # Boot + full-slot prepare can take several seconds
            TestStep("Wait post-reboot settle", None, 8000),
            TestStep("Verify wiped + IDLE", '?', 2000,
                expect_json=lambda d: (
                    d.get("ev") == "status"
                    and d.get("st") == "IDLE"
                    and int(d.get("runs") or 0) == 0
                    and int(d.get("total_runs") or 0) == 0
                )),
        ],
    ),
]
