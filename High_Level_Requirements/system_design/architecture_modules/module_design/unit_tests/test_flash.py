"""
Unit test: Flash Storage (v2.20 — Opt-A RawRunStore)
    U12 — Flash self-test (reserved sector 0x1FE000)
    U13 — Full run cycle → flash storage (run count increments)
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, UNIT_RING_READY,
)

TEST_VERSION = "2.24.0"

SCENARIOS = []

# ── U12: Flash self-test ─────────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U12 — Flash self-test",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        # Two sector erases + program can take seconds. Use expect_json so the
        # harness keeps the read window open (special-cased for cmd 'f') instead
        # of sleeping wait_ms then starting a late poll that can miss the event.
        # Accept ok as True/1 (json_kv_bool emits 0/1).
        TestStep(
            "Run flash self-test",
            'f',
            0,
            timeout_ms=15000,
            expect_json=lambda d: (
                d.get('ev') == 'flash' and bool(d.get('ok'))
            ),
        ),
    ]
))

# ── U13: Full run cycle → flash storage ──────────────────────────
SCENARIOS.append(TestScenario(
    name="U13 — Full run cycle → flash storage",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Record initial runs", '?', 300,
            expect_json={"ev": "status"}),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait pre-roll samples", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Start logging (force)", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("End run (force POST_RUN) + wait run_saved", 'p', 600,
            on_response=lambda h, _: h.wait_for_json_event(
                "run_saved", timeout_ms=10000) is not None),
        TestStep("Wait cooldown → IDLE",
            poll_state='IDLE', timeout_ms=20000),
        TestStep("Verify back to IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Check runs incremented", '?', 300,
            expect_json=lambda d: d.get("runs", 0) >= 1),
    ]
))
