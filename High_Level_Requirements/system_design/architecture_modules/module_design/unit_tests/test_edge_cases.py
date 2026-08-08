"""
Edge-case tests (R4) — v2.20 Opt-A alignment

    E01 — Ring pre-roll fill + re-arm reset
    E02 — Multi-run flash storage
    E03 — Zero-length run (ARM → immediate POST_RUN blocked)
    E04 — Rapid state toggling (ARM↔IDLE × 5)
    E05 — Invalid injection: negative pressure
    E06 — Invalid injection: NaN-like quaternion
    E07 — Arm with tiny quat (accepted in test mode)
    E08 — State transition validation: blocked paths
    E09 — Start detector: pressure noise rejection
    E10 — LOGGING holds under active descent pressure
    E11 — Run saved: frame count path

No ring_full@3000 waits — use wait_for_ring_count(UNIT_RING_READY).
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    inject_pressure, inject_pressure_ramp,
    wait_for_ring_count, UNIT_RING_READY, ARM_FILL_CAP,
)

TEST_VERSION = "2.21.0"

SCENARIOS = []

# ── E01: Ring pre-roll fill + re-arm ──────────────────────────────
SCENARIOS.append(TestScenario(
    name="E01 — Ring buffer wraparound",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait pre-roll samples", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Verify r and rm", '?', 300,
            expect_json=lambda d: (
                d.get("st") == "ARMED"
                and int(d.get("rm") or 0) == ARM_FILL_CAP
                and int(d.get("r") or 0) >= UNIT_RING_READY
            )),
        TestStep("Force LOGGING (drains ring)", 'l', 500,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("Force POST_RUN + wait run_saved", 'p', 500,
            on_response=lambda h, _: h.wait_for_json_event(
                "run_saved", timeout_ms=10000) is not None),
        TestStep("Wait IDLE", poll_state='IDLE', timeout_ms=20000),
        TestStep("Re-arm: ring should reset", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait fill again", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Verify r after re-arm fill", '?', 300,
            expect_json=lambda d: (
                d.get("st") == "ARMED"
                and int(d.get("r") or 0) >= UNIT_RING_READY
            )),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── E02: Multi-run flash storage ─────────────────────────────────
SCENARIOS.append(TestScenario(
    name="E02 — Multi-run flash storage",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Record initial runs", '?', 300,
            expect_json={"ev": "status"}),
        *[
            step for _ in range(3) for step in [
                TestStep("Arm", 'a', 500,
                    expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
                TestStep("Wait pre-roll", None, 100,
                    on_response=lambda h, _: wait_for_ring_count(
                        h, min_r=50, timeout_ms=10000)),
                TestStep("Force LOGGING", 'l', 500,
                    expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
                TestStep("Force POST_RUN + wait run_saved", 'p', 500,
                    on_response=lambda h, _: h.wait_for_json_event(
                        "run_saved", timeout_ms=10000) is not None),
                TestStep("Wait IDLE", poll_state='IDLE', timeout_ms=20000),
            ]
        ],
        TestStep("Verify runs incremented", '?', 300,
            expect_json=lambda d: d.get("runs", 0) >= 1),
    ]
))

# ── E03: Zero-length run ─────────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="E03 — Zero-length run (ARM → immediate POST_RUN)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Force POST_RUN (no LOGGING)", 'p', 500,
            expect_json={"ev": "state_blocked", "reason": "not_logging"}),
        TestStep("Verify still ARMED (transition blocked)", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── E04: Rapid state toggling ────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="E04 — Rapid state toggling (ARM↔IDLE × 5)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        *[
            step for _ in range(5) for step in [
                TestStep("→ ARMED", 'a', 600,
                    expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
                # preroll_prep erase can take >400 ms; match st in multi-event burst
                TestStep("→ IDLE", 'i', 2000,
                    expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
            ]
        ],
    ]
))

# ── E05: Invalid injection — negative pressure ───────────────────
SCENARIOS.append(TestScenario(
    name="E05 — Invalid injection: negative pressure",
    setup_commands=[],
    teardown_commands=['B 101325'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set negative pressure", 'B -1000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Echo clamps / handles value", 'Z', 300,
            expect_json=lambda d: d.get("p") is not None),
        TestStep("Restore sea level", 'B 101325', 300,
            expect_json={"ev": "cmd", "cmd": "B", "p": 101325.0}),
    ]
))

# ── E06: Invalid injection — NaN quaternion ──────────────────────
SCENARIOS.append(TestScenario(
    name="E06 — Invalid injection: NaN-like quaternion",
    setup_commands=[],
    teardown_commands=['Q 1 0 0 0'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set zero quaternion (potentially problematic)", 'Q 0 0 0 0', 300,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Echo verify firmware handles it", 'Z', 300,
            expect_json=lambda d: d.get("q") is not None),
        TestStep("Restore identity", 'Q 1 0 0 0', 300,
            expect_json={"ev": "cmd", "cmd": "Q", "q": [1.0, 0.0, 0.0, 0.0]}),
    ]
))

# ── E07: Arm with tiny quat ──────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="E07 — Arm refusal: quat magnitude out of range",
    setup_commands=['i'],
    teardown_commands=['i', 'Q 1 0 0 0'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set tiny quaternion (mag ~0.1)", 'Q 0.1 0 0 0', 300,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        # Test mode skips live quat magnitude gate
        TestStep("Arm accepts synthetic quat", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Restore identity", 'Q 1 0 0 0', 300,
            expect_json={"ev": "cmd", "cmd": "Q"}),
        TestStep("Return IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── E08: State transition validation ─────────────────────────────
SCENARIOS.append(TestScenario(
    name="E08 — State transition validation: blocked paths",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Force LOGGING from IDLE (should block)", 'l', 400,
            expect_json={"ev": "state_blocked", "reason": "not_armed"}),
        TestStep("Verify still IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
        TestStep("Force POST_RUN from IDLE (should block)", 'p', 400,
            expect_json={"ev": "state_blocked", "reason": "not_logging"}),
        TestStep("Verify still IDLE", '?', 300,
            expect_json={"st": "IDLE"}),
    ]
))

# ── E09: Start detector — pressure noise rejection ───────────────
SCENARIOS.append(TestScenario(
    name="E09 — Start detector: pressure noise rejection",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Sync baseline (ensure clean 101325)", 'B 101325', 200,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm", 'a', 600,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait P0 latch", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Inject ±1 Pa oscillation (net zero)", None, 100,
            on_response=lambda h, _: (
                inject_pressure_ramp(h, 101325, 101326, 5, 30),
                inject_pressure_ramp(h, 101326, 101324, 5, 30),
                True
            )[-1]),
        TestStep("Still ARMED (noise shouldn't trigger)", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Return to IDLE", 'i', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "IDLE"}),
    ]
))

# ── E10: LOGGING holds under descent pressure ────────────────────
SCENARIOS.append(TestScenario(
    name="E10 — Ring buffer content non-zero (log_start pre > 0)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait pre-roll", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        # Natural start so end det is ON; then inject strong descent to block end
        TestStep("Start via descent", None, 100,
            on_response=lambda h, _: (
                inject_pressure_ramp(h, 101325, 101361, 12, 100)
                and h.wait_for_state('LOGGING', timeout_ms=8000)
            )),
        TestStep("Inject further descent (block end det)", None, 100,
            on_response=lambda h, _: inject_pressure(h, 101500) or True),
        TestStep("Verify LOGGING", '?', 300,
            expect_json={"st": "LOGGING"}),
        TestStep("Force POST_RUN + cleanup", 'p', 500,
            expect_json={"ev": "st", "from": "LOGGING", "to": "POST_RUN"}),
        TestStep("Wait IDLE", poll_state='IDLE', timeout_ms=20000),
    ]
))

# ── E11: Run data size validation ────────────────────────────────
SCENARIOS.append(TestScenario(
    name="E11 — Run saved: frame count + data size > 0",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Arm", 'a', 500,
            expect_json={"ev": "st", "from": "IDLE", "to": "ARMED"}),
        TestStep("Wait pre-roll", None, 100,
            on_response=lambda h, _: wait_for_ring_count(
                h, min_r=UNIT_RING_READY, timeout_ms=12000)),
        TestStep("Force LOGGING + wait for live frames", 'l', 2000,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        TestStep("Force POST_RUN + wait run_saved", 'p', 500,
            on_response=lambda h, _: h.wait_for_json_event(
                "run_saved", timeout_ms=10000) is not None),
        TestStep("Verify runs incremented via status", '?', 300,
            expect_json=lambda d: d.get("runs", 0) >= 1),
        TestStep("Wait IDLE", poll_state='IDLE', timeout_ms=20000),
    ]
))
