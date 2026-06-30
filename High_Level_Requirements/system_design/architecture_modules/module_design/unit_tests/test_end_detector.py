"""
Unit test: End Detector (v4 — 0.5 Hz sampling, 10-sample ring = 5 s window)
    U07 — Flat / slow descent → POST_RUN after ring fills
    U08 — No premature stop during active descent (or ascent)

Logic: feed() is called at 100 Hz but only samples every 500 ms.
       10-sample ring covers 5 s. On each sample, peek oldest:
         dp = current_pa - oldest_pa.
         If dp ≥ 0 AND dp < 24 Pa (2 m × 12 Pa/m) → end.
       Independent of FlashRing — no drain interference.
"""
from sgc_test_harness import TestStep, TestScenario, force_state, enable_test_mode

SCENARIOS = []

# ── U07: Constant pressure → dp = 0 → end after 10 samples ───────
SCENARIOS.append(TestScenario(
    name="U07 — End detection: flat (dp=0)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set pressure baseline", 'B 101000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 5600),
        TestStep("Verify ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Force LOGGING", 'l', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        # 10 samples at 0.5 Hz = 5 s. Constant pressure → dp=0 → end.
        TestStep("Wait for end_detected → POST_RUN",
            poll_state='POST_RUN', poll_interval_ms=300, timeout_ms=15000),
    ]
))

# ── U08: Pressure decrease (ascent) → dp < 0 → no end ────────────
# Then stable → dp = 0 → end → POST_RUN → IDLE.
SCENARIOS.append(TestScenario(
    name="U08 — No false end during ascent (dp < 0)",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        TestStep("Enable test mode", None, 150,
            on_response=lambda h, _: enable_test_mode(h)),
        TestStep("Set pressure baseline", 'B 101000', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Arm device", 'a', 5600),
        TestStep("Verify ARMED", '?', 300,
            expect_json={"st": "ARMED"}),
        TestStep("Force LOGGING", 'l', 400,
            expect_json={"ev": "st", "from": "ARMED", "to": "LOGGING"}),
        # Drop pressure 100 Pa → ascent. dp < 0 → NOT triggered.
        TestStep("Simulate pressure drop (ascent)", 'B 100900', 300,
            expect_json={"ev": "cmd", "cmd": "B"}),
        TestStep("Wait 3 s (ring still has old pressure → dp<0)", None, wait_ms=3000),
        TestStep("Should still be LOGGING (dp < 0)", '?', 300,
            expect_json={"st": "LOGGING"}),
        # After ring turnover (all 100900) → dp = 0 → end → cooldown → IDLE.
        TestStep("Wait POST_RUN + cooldown → IDLE", None, 200,
            poll_state='IDLE', timeout_ms=30000),
    ]
))
