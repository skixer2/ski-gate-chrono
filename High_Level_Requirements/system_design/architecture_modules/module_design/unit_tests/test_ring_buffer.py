"""
Unit test: Ring Buffer / linear pre-roll (v2.20 — Opt-A FW ≥4.80)
    U09 — Fills past UNIT_RING_READY in ARMED
    U10 — Continues filling toward ARM_FILL_CAP while ARMED
    U11 — Resets on re-arm

Contract (flash_ring.h):
    rm = ARM_FILL_CAP = 3000
    ring_full only at r==3000 (races 30 s ARM_TIMEOUT) — unit tests
    use wait_for_ring_count(), not ring_full, unless testing the cap.
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, ARM_FILL_CAP, UNIT_RING_READY, PREROLL_KEEP,
)

TEST_VERSION = "2.21.0"

SCENARIOS = []

# ── U09: Ring buffer fills in ARMED ──────────────────────────────
SCENARIOS.append(TestScenario(
    name="U09 — Ring buffer fills (pre-roll)",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode + reset defaults", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set sea-level baseline", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Poll until ring has samples", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Verify rm=ARM_FILL_CAP and r growing", '?', 300,
            expect_json=lambda d: (
                d.get("ev") == "status"
                and d.get("st") == "ARMED"
                and int(d.get("rm") or 0) == ARM_FILL_CAP
                and int(d.get("r") or 0) >= UNIT_RING_READY
            )),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── U10: Ring keeps filling while ARMED (no wrap at 500) ─────────
SCENARIOS.append(TestScenario(
    name="U10 — Ring keeps filling while ARMED",
    setup_commands=['i', 'T'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm device", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait for first fill milestone", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Snapshot r after first wait", '?', 300,
            expect_json=lambda d: d.get("st") == "ARMED"),
        TestStep("Wait further toward keep window", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=PREROLL_KEEP, timeout_ms=20000)),
        TestStep("Still ARMED, r >= PREROLL_KEEP, rm=3000", '?', 300,
            expect_json=lambda d: (
                d.get("st") == "ARMED"
                and int(d.get("rm") or 0) == ARM_FILL_CAP
                and int(d.get("r") or 0) >= PREROLL_KEEP
            )),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── U11: Ring buffer resets on new arm ───────────────────────────
SCENARIOS.append(TestScenario(
    name="U11 — Ring buffer resets on re-arm",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm first time", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Fill past ready", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Verify filled", '?', 300,
            expect_json=lambda d: (
                d.get("st") == "ARMED"
                and int(d.get("r") or 0) >= UNIT_RING_READY
            )),
        TestStep("Return to IDLE", 'i', 800,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
        # prepare_preroll erases on enter IDLE — next arm starts at r≈0
        TestStep("Arm second time", 'a', 400,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Ring should be below previous fill (reset)",
            '?', 200,
            expect_json=lambda d: (
                d.get("st") == "ARMED"
                and int(d.get("r") or 0) < UNIT_RING_READY
            )),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))
