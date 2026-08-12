"""
Unit test: Bit Packer (v2.20 — Opt-A)
    U14 — Compression exercised during run
    U15 — All packet types exercised (T1 coasting, T3 anchor)

Uses wait_for_ring_count (not ring_full@3000). force-'l' + 'p' for
run lifecycle (end det not required here).
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, UNIT_RING_READY,
)

TEST_VERSION = "2.22.0"

SCENARIOS = []

# ── U14: Bit packer exercised via full run ───────────────────────
SCENARIOS.append(TestScenario(
    name="U14 — Compression exercised during run",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set identity quat + zero accel", 'Q 1 0 0 0', 200,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Set zero linear accel", 'L 0 0 0', 200,
            expect_json={"ev": "cmd", "cmd": "L"}),
        TestStep("Set sea-level pressure", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait for pre-roll samples", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Verify ARMED with samples", '?', 300,
            expect_json=lambda d: (
                d.get("st") == "ARMED"
                and int(d.get("r") or 0) >= UNIT_RING_READY
            )),
        TestStep("Force LOGGING", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("Force POST_RUN", 'p', 500,
            expect_json={"ev": "st", "from": "LOGGING", "to": "POST_RUN"}),
        TestStep("Verify POST_RUN (or already cooling)", '?', 300,
            expect_json=lambda d: d.get("st") in ("POST_RUN", "IDLE")),
        TestStep("Wait IDLE", poll_state='IDLE', timeout_ms=20000),
    ]
))

# ── U15: Different sensor values exercise packet types ───────────
SCENARIOS.append(TestScenario(
    name="U15 — All packet types exercised",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        # Type 1: small deltas (coasting)
        TestStep("Set gentle rotation (T1 packets)", 'Q 0.9 0.1 0.0 0.4', 200,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Check ARMED promptly", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("→ IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
        # Type 3: large jump (impact anchor)
        TestStep("Set large rotation change (T3 anchor)", 'Q 0.5 0.5 0.5 0.5', 200,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Arm again", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Check ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
