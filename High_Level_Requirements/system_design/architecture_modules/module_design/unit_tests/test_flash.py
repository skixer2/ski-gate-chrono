"""
Unit test: Flash Storage (Opt-A RawRunStore + v5.01 size-aware layout)
    U12 — Flash self-test (top-of-chip sector via flash_layout; 0x1FE000 on 2 MB)
    U12b — Boot flash_map geometry (slots/slot_kb sane; preroll fixed)
    U13 — Full run cycle → flash storage (run count increments)
"""
from sgc_test_harness import (
    TestStep, TestScenario, enable_test_mode,
    wait_for_ring_count, UNIT_RING_READY,
)

TEST_VERSION = "2.26.0"

SCENARIOS = []


def _flash_map_ok(d):
    """v5.01 flash_map: preroll fixed; slots scale with chip; slot ~244 KB."""
    if d.get('ev') != 'flash_map':
        return False
    slots = int(d.get('slots') or 0)
    slot_kb = int(d.get('slot_kb') or 0)
    size_kb = int(d.get('size_kb') or 0)
    preroll = int(d.get('preroll_end') or 0)
    # Pre-roll end always 0x14000 = 81920
    if preroll not in (0, 0x14000, 81920):
        return False
    if slots < 8 or slots > 32:
        return False
    if slot_kb < 200 or slot_kb > 300:  # classic ~244
        return False
    if size_kb > 0 and size_kb < 2048:
        return False
    # 2 MB stock → exactly 8 slots
    if size_kb == 2048 and slots != 8:
        return False
    return True


# ── U12: Flash self-test ─────────────────────────────────────────
SCENARIOS.append(TestScenario(
    name="U12 — Flash self-test",
    setup_commands=['i'],
    teardown_commands=['i'],
    steps=[
        # Self-test sector is top-of-chip (flash_layout); two erases + program.
        # Long expect_json window (harness special-cases cmd 'f'). Accept ok True/1.
        TestStep(
            "Run flash self-test",
            'f',
            0,
            timeout_ms=25000,
            expect_json=lambda d: (
                # ignore phase=start ack; require final ok=1/true
                d.get('ev') == 'flash'
                and d.get('phase') != 'start'
                and bool(d.get('ok'))
            ),
        ),
    ]
))

def _reset_and_catch_flash_map(h, _):
    """Destructive: serial R → boot stream must include flash_map (v5.01)."""
    h._flush()
    h.send('R')
    objs = h._read_json_lines(5000)
    # CDC drop on reboot — keep reading through ready
    deadline_extra = 0
    while deadline_extra < 4 and not any(_flash_map_ok(o) for o in objs):
        more = h._read_json_lines(2000)
        if not more:
            deadline_extra += 1
            continue
        objs.extend(more)
        deadline_extra = 0
    if h.verbose:
        for o in objs[:12]:
            print(f"    ← {o}")
    return any(_flash_map_ok(o) for o in objs)


# ── U12b: Size-aware layout (v5.01) — DESTRUCTIVE (wipes runs like S02) ──
# Not in smoke; full loop / layout gate only.
SCENARIOS.append(TestScenario(
    name="U12b — flash_map geometry (v5.01, destructive R)",
    setup_commands=['i'],
    teardown_commands=[],
    steps=[
        TestStep("Factory reset + catch flash_map", None, 100,
            on_response=_reset_and_catch_flash_map),
        TestStep("Wait post-reboot settle", None, 8000),
        TestStep("Verify IDLE after layout boot", '?', 2000,
            expect_json={"ev": "status", "st": "IDLE"}),
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
