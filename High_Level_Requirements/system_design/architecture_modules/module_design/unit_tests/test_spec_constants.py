#!/usr/bin/env python3
"""
test_spec_constants.py — SPEC ↔ CODE constant drift guard  (2026-11-20)

WHY THIS EXISTS
Two regressions shipped in two days because a constant drifted away from the
value its own requirements document states, and nothing compared the two:

  1. ARM window: requirements v6.0 / F02 say 3000 slots = 30 s.
     Commit ddd8ef3 changed the cap to 20 s; it later read 120 s.
     Nobody noticed because U20's assertion only checks "SLEEP within 40 s",
     which is true at 20 s, 30 s — and false only at 120 s.

  2. Baro scale: datasheet says the BMP390 uint24 count is raw/128 hPa.
     Code used *39/100. A 1.41 hPa bias, invisible to any rate test.

This test reads the REQUIREMENTS and the SOURCE and asserts they agree.
It needs NO hardware and NO build — it is pure text/introspection, so it can
run in under a second on every commit. That is the point: drift fails fast.

USAGE
    python3 test_spec_constants.py            # run all checks
    python3 test_spec_constants.py -v         # show each mapping

Exit code 0 = aligned, 1 = drift detected.

Add a new check whenever a requirement states a numeric value that the code
also encodes. Keep each check to one requirement id and one assertion.
"""
from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# HERE = .../system_design/architecture_modules/module_design/unit_tests
# SRC  = .../system_design/architecture_modules/module_design/implementation/
#          Firmware_Zephyr/apps/03_pull_proto/src
SRC = os.path.normpath(os.path.join(
    HERE, "..", "implementation", "Firmware_Zephyr", "apps",
    "03_pull_proto", "src"))
# REQ  = High_Level_Requirements/sgc_requirements.md  (4 levels up)
REQ = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "..", "sgc_requirements.md"))

VERBOSE = "-v" in sys.argv


def read(path: str) -> str:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def define_int(text: str, name: str) -> int | None:
    """Find `#define NAME 123u` / `#define NAME 0x98u` / NAME = 123."""
    m = re.search(
        r"#define\s+%s\s+\(?\s*(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*\)?"
        % re.escape(name), text)
    if m:
        return int(m.group(1), 0)
    return None


def req_mentions(req_text: str, pattern: str) -> bool:
    return re.search(pattern, req_text, re.IGNORECASE) is not None


class Check:
    def __init__(self, rid: str, what: str):
        self.rid, self.what = rid, what
        self.failures: list[str] = []
        self.notes: list[str] = []

    def eq(self, label: str, spec: object, code: object, origin: str = ""):
        ok = spec == code
        line = "  %-46s spec=%-6s code=%-6s %s" % (
            label, spec, code, "OK" if ok else "*** DRIFT ***")
        (self.notes if ok else self.failures).append(line)
        if ok and VERBOSE:
            print(line)
        if not ok and origin:
            self.failures.append("      origin: %s" % origin)
        return ok

    def truth(self, label: str, cond: bool, detail: str = ""):
        line = "  %-46s %s" % (label, "OK" if cond else "*** FAIL ***")
        if detail:
            line += "  (%s)" % detail
        (self.notes if cond else self.failures).append(line)
        if cond and VERBOSE:
            print(line)
        return cond


def main() -> int:
    for p, label in ((SRC, "firmware src"), (REQ, "requirements")):
        if not os.path.exists(p):
            print("SKIP: %s not found at %s" % (label, p))
            print("TRIVIAL PASS (fixture absent) — not a real verification")
            return 0

    rec = read(os.path.join(SRC, "recorder.c"))
    ring_h = read(os.path.join(SRC, "ring.h"))
    prod = read(os.path.join(SRC, "sensor_prod.c"))
    regh = read(REQ)

    fails: list[str] = []
    print("SGC spec↔code constant drift guard")
    print("requirements: %s" % REQ)
    print("firmware src: %s\n" % SRC)

    # ── F02: ARM slot cap = 3000 (30 s at 100 Hz) ────────────────────
    c = Check("F02", "ARM fill cap = 3000 slots = 30 s")
    if req_mentions(regh, r"3000 slots") or req_mentions(regh, r"30 s cap"):
        c.notes.append("  F02 requires 3000 slots (30 s cap)")
    else:
        c.failures.append("  F02 no longer states '3000 slots' / '30 s cap' — "
                          "requirements changed; update this test")

    TICK_HZ = 100
    SPEC_SLOTS = 3000
    SPEC_ARM_S = SPEC_SLOTS // TICK_HZ   # 30 s, per R02/F02

    # The cap is expressed as SD_ARM_CAP_S (symbol), falling back to the
    # legacy literal `ticks >= 100 * N` so an old tree still reports drift.
    arm_cap_s = define_int(rec, "SD_ARM_CAP_S")
    if arm_cap_s is None:
        m = re.search(r"ticks\s*>=\s*100\s*\*\s*(\d+)", rec)
        arm_cap_s = int(m.group(1)) if m else None
    c.eq("ARM cap (s)", SPEC_ARM_S, arm_cap_s,
         "recorder.c SD_ARM_CAP_S (R02: 30 s / F02: 3000 slots)")

    slots = define_int(ring_h, "RING_SLOTS")
    cap_slots = define_int(ring_h, "SGC_ARM_CAP_SLOTS")
    # SGC_ARM_CAP_SLOTS is the compile-time guard value; it must equal the
    # documented 3000 and agree with SD_ARM_CAP_S * 100.
    c.eq("SGC_ARM_CAP_SLOTS (ring.h guard)", SPEC_SLOTS, cap_slots,
         "ring.h BUILD_ASSERT value vs F02 '3000 slots'")
    if arm_cap_s is not None and cap_slots is not None:
        c.eq("cap seconds x 100 == cap slots", arm_cap_s * TICK_HZ, cap_slots,
             "SD_ARM_CAP_S and SGC_ARM_CAP_SLOTS must not diverge")

    # RING_SLOTS is NOT required to equal 3000: it is the physical region
    # size (4096 slots = 0x14000 = LOG_START). The invariant that matters is
    # that the ARM fill cap fits inside it, because the ring is LINEAR and
    # a larger cap silently drops frames (ring_push -> false -> st_skipped).
    if slots and arm_cap_s:
        c.truth("ARM fill cap fits the linear reserve",
                arm_cap_s * TICK_HZ <= slots,
                "cap %d slots (%.0f s) vs reserve %d slots (%.1f s)"
                % (arm_cap_s * TICK_HZ, arm_cap_s,
                   slots, slots / TICK_HZ))
        c.truth("reserve region exactly abuts LOG_START",
                define_int(ring_h, "RING_SLOTS") is not None
                and slots * define_int(ring_h, "RING_SLOT_BYTES") == 0x14000,
                "RING_SLOTS * RING_SLOT_BYTES must equal 0x14000 (LOG_START)")

    # ── F04: 2.00 m drop trigger ─────────────────────────────────────
    c2 = Check("F04", "2.00 m drop trigger")
    c2.truth("F04 states 2.0 m", req_mentions(regh, r"2\.0 ?m"))
    c2.eq("SD_DROP_X100", 200, define_int(rec, "SD_DROP_X100"),
          "recorder.c SD_DROP_X100, x100 fixed point")
    c2.eq("SD_P0_MIN_PA", 50000, define_int(rec, "SD_P0_MIN_PA"))
    c2.eq("SD_P0_MAX_PA", 110000, define_int(rec, "SD_P0_MAX_PA"))

    # ── baro scale: datasheet-exact 1/128 hPa ────────────────────────
    # BMP390 uint24 -> hPa is raw/128. Carried as Pa/2 (baro_pa_div2),
    # so div2 = raw*100/128/2 = raw*25/64. The *39/100 form is the bug.
    c3 = Check("HW/DS", "baro raw -> Pa/2 uses exact 1/128 hPa")
    c3.truth("uses raw*25/64 (exact)", "25u) / 64u" in prod.replace(" ", "")
             or "25u)/64u" in prod.replace(" ", ""),
             "sensor_prod.c baro_pa_div2 expression")
    c3.truth("does NOT use the 39/100 approximation",
             "39u) / 100u" not in prod and "39u)/100u" not in prod.replace(" ", ""),
             "sensor_prod.c old *39/100 form")

    # ── charger: 0.2C for the 500 mAh Renata HPST ────────────────────
    bc = read(os.path.join(SRC, "ble_config.c"))
    c4 = Check("REQ-HW", "charger current = 0.2C of the fitted cell")
    # 500 mAh * 0.2C = 100 mA -> ICHRG code 6 -> reg 0x98
    c4.eq("BQ_WANT_FASTCHG (100 mA = 0.2C)", 0x98,
          define_int(bc, "BQ_WANT_FASTCHG"),
          "ble_config.c; 500 mAh * 0.2C = 100 mA -> code 6 -> 0x98")
    c4.eq("BQ_WANT_VBATREG (4.35 V)", 0x96,
          define_int(bc, "BQ_WANT_VBATREG"),
          "Renata ICP632136HPST CV = 4.35 V -> code 75 -> 0x96")
    c4.truth("registers re-asserted at boot",
             "bq_charger_assert()" in bc,
             "battery-only cold boot must reprogram the BQ25120A")
    c4.truth("registers re-asserted on poll",
             bc.count("bq_regs_assert()") >= 2,
             "a reset part must be fixed within one poll interval")

    for chk in (c, c2, c3, c4):
        if chk.failures:
            print("[%s] %s" % (chk.rid, chk.what))
            for f in chk.failures:
                print(f)
            fails.extend(chk.failures)

    print()
    if fails:
        print("DRIFT DETECTED — %d failed assertion(s)" % len(fails))
        return 1
    print("ALL SPEC CONSTANTS ALIGNED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
