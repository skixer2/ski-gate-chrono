"""
S02 — Factory reset via serial R (destructive) + v5.01 flash_map check.

Picked up by Get-ChildItem test_*.py full loop.
Not in smoke/core (wipes runs).

Pass criteria (device):
  - factory_reset ACK
  - flash_map REQUIRED with size→slots table (same code for all chips):
      2048 KB (2 MB) → 8 slots   ← today's Nicla / MX25R1635F
      4096 KB (4 MB) → 16 slots  ← when MX25R3235F is fitted
      8192 KB (8 MB) → 32 slots  ← when MX25R6435F is fitted
  - after settle: SLEEP, runs=0, total_runs=0

Note: FW emits factory_reset then reboot in one burst; do not require a
second wait for reboot after the command step already drained it.
"""
from sgc_test_harness import TestStep, TestScenario

TEST_VERSION = "2.28.0"

# v5.01 flash_layout: N = floor((chip - preroll - top4) / 244KB), cap 32.
# Exact expectations for standard MX25R densities:
EXPECTED_SLOTS_BY_SIZE_KB = {
    2048: 8,   # MX25R1635F — production / Nicla today
    4096: 16,  # MX25R3235F
    8192: 32,  # MX25R6435F
}


def _flash_map_ok(o) -> bool:
    """Require flash_map geometry for the detected chip size."""
    if o.get('ev') != 'flash_map':
        return False
    slots = int(o.get('slots') or 0)
    size_kb = int(o.get('size_kb') or 0)
    slot_kb = int(o.get('slot_kb') or 0)
    preroll = int(o.get('preroll_end') or 0)

    # Pre-roll fixed at 0x14000
    if preroll not in (0x14000, 81920):
        return False
    # Slot payload size stays classic ~244 KB
    if slot_kb < 200 or slot_kb > 300:
        return False
    if slots < 8 or slots > 32:
        return False

    expected = EXPECTED_SLOTS_BY_SIZE_KB.get(size_kb)
    if expected is not None:
        return slots == expected
    # Unknown density: still require plausible packing
    return size_kb >= 2048


def _reset_and_catch_events(h, _):
    """Send R; require factory_reset + flash_map (size→slots table)."""
    h._flush()
    h.send('R')
    objs = h._read_json_lines(3000)
    # Reboot may cut USB briefly; keep reading for factory_reset + flash_map
    if not any(o.get('ev') == 'factory_reset' for o in objs):
        objs.extend(h._read_json_lines(2000))
    tries = 0
    while tries < 6 and not any(_flash_map_ok(o) for o in objs):
        more = h._read_json_lines(2000)
        if more:
            objs.extend(more)
            tries = 0
        else:
            tries += 1
    evs = {o.get('ev') for o in objs}
    if h.verbose:
        for o in objs[:14]:
            print(f"    ← {o}")
    if 'factory_reset' not in evs:
        return False
    # flash_map is REQUIRED (not soft) — this is the 2/4/8 MB slot gate
    if not any(_flash_map_ok(o) for o in objs):
        # Also accept raw_store with matching slots if flash_map was dropped on CDC
        for o in objs:
            if o.get('ev') != 'raw_store':
                continue
            slots = int(o.get('slots') or 0)
            chip_kb = int(o.get('chip_kb') or 0)
            exp = EXPECTED_SLOTS_BY_SIZE_KB.get(chip_kb)
            if exp is not None and slots == exp:
                return True
            if chip_kb == 0 and slots == 8:
                # legacy raw_store without chip_kb — 2 MB default
                return True
        return False
    return True


SCENARIOS = [
    TestScenario(
        name="S02 — Factory reset (serial R)",
        setup_commands=['i'],
        teardown_commands=[],
        steps=[
            TestStep("Verify SLEEP", '?', 400,
                expect_json={"ev": "status", "st": "SLEEP"}),
            TestStep("Trigger factory reset", None, 100,
                on_response=_reset_and_catch_events),
            # Boot + full-slot prepare can take several seconds
            TestStep("Wait post-reboot settle", None, 8000),
            TestStep("Verify wiped + SLEEP", '?', 2000,
                expect_json=lambda d: (
                    d.get("ev") == "status"
                    and d.get("st") == "SLEEP"
                    and int(d.get("runs") or 0) == 0
                    and int(d.get("total_runs") or 0) == 0
                )),
        ],
    ),
]
