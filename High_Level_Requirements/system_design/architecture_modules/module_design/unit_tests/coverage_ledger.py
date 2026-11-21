#!/usr/bin/env python3
"""Honest requirement -> automated-test coverage ledger.

JP 2026-11-20/21: "cover as many requirements as possible with automated tests,
and have a smoke test to execute every day", "maximum requirement coverage with
minimal software changes".

WHY THIS IS NOT A GREP FOR 'F04'
--------------------------------
The device tests are labelled by their own scheme (U01-U20, S02-S07), not by
requirement id. The authoritative link between the two is
`device/requirements_traceability.md`, which this reads: a requirement counts as
covered only if that matrix attributes a U/S test to it AND a test file really
implements that U/S id. Grepping test files for "F04" would be wrong in both
directions (it would credit a file that merely mentions the id in a comment,
and miss every real test).

Buckets
  COVERED      traceability maps it to a test that exists in this directory
  STALE        traceability maps it to a test that does NOT exist (drift)
  NONE         device requirement, no automated test  <-- the work list
  MANUAL       device requirement, not automatable (visual/IP/thermal/RF)
  PHONE        phone requirement -> Dart suite lane
  OUT_OF_SCOPE v2 only / not applicable in v1 / removed
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REQ_PATH = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "..", "sgc_requirements.md"))
TRACE = os.path.join(HERE, "device", "requirements_traceability.md")

MANUAL_HINTS = (
    "visual", "ip6", "ip 6", "drop test", "-20", "thermal", "erp",
    "battery life ", "8 h", "12 h", "24 h", "approtect", "nfc coil",
)


def parse_requirements():
    txt = io.open(REQ_PATH, encoding="utf-8").read()
    out = {}
    row = re.compile(r"^\| \*\*([FIR]\d+)\*\* \|(.*)$")
    for line in txt.splitlines():
        m = row.match(line.strip())
        if not m:
            continue
        cells = [c.strip() for c in m.group(2).split("|")]
        out[m.group(1)] = (cells[0] if cells else "",
                           cells[1] if len(cells) > 1 else "")
    return out


def parse_traceability():
    """requirement id -> set of U/S ids the matrix attributes to it."""
    if not os.path.isfile(TRACE):
        return {}
    txt = io.open(TRACE, encoding="utf-8", errors="replace").read()
    out = {}
    for line in txt.splitlines():
        m = re.match(r"^\|\s*\*\*([FIR]\d+)\*\*\s*\|", line)
        if not m:
            continue
        cells = [c.strip() for c in line.split("|")]
        blob = " ".join(cells[2:])
        ids = set(re.findall(r"\b([US]\d\d)\b", blob))
        if ids:
            out[m.group(1)] = ids
    return out


def implemented_test_ids():
    """U/S ids that actually have a test in this directory."""
    have = {}
    for fn in sorted(os.listdir(HERE)):
        if not (fn.startswith("test_") and fn.endswith(".py")):
            continue
        txt = io.open(os.path.join(HERE, fn), encoding="utf-8",
                      errors="replace").read()
        if "SCENARIOS" not in txt and "TestScenario" not in txt:
            continue          # meta tool, not a device test
        for tid in re.findall(r"\b([US]\d\d)\b", txt):
            have.setdefault(tid, set()).add(fn)
    return have


def classify(rid, body, traced, impl):
    b = body.lower()
    if ("v2 only" in b or "v2/" in b or "not applicable (v1)" in b
            or "dnp footprint only" in b or "reserved for a future" in b):
        return "OUT_OF_SCOPE", set()
    if "the phone shall" in b or "the app shall" in b or "phone <-> cloud" in b:
        return "PHONE", set()
    if "removed" in b[:60]:
        return "OUT_OF_SCOPE", set()
    if rid in traced:
        real = {t for t in traced[rid] if t in impl}
        if real:
            return "COVERED", real
        return "STALE", traced[rid]
    for kw in MANUAL_HINTS:
        if kw in b:
            return "MANUAL", {kw}
    return "NONE", set()


def main():
    reqs = parse_requirements()
    traced = parse_traceability()
    impl = implemented_test_ids()

    buckets = {}
    for rid, (body, _v) in sorted(reqs.items()):
        kind, detail = classify(rid, body, traced, impl)
        buckets.setdefault(kind, []).append((rid, body, detail))

    total = len(reqs)
    print("SGC requirement coverage — %d requirements in sgc_requirements.md" % total)
    print("traceability matrix contributes %d requirement->test links" % len(traced))
    print()
    for k in ("COVERED", "NONE", "MANUAL", "PHONE", "OUT_OF_SCOPE", "STALE"):
        n = len(buckets.get(k, []))
        print("  %-12s %3d  (%4.1f%%)" % (k, n, 100.0 * n / total if total else 0))
    print()

    if buckets.get("STALE"):
        print("=== STALE: matrix points at a test that no longer exists (%d) ==="
              % len(buckets["STALE"]))
        for rid, _b, d in buckets["STALE"]:
            print("  %-5s -> %s" % (rid, ", ".join(sorted(d))))
        print()

    if buckets.get("NONE"):
        print("=== DEVICE REQUIREMENTS WITH NO AUTOMATED TEST (%d) — the work list ==="
              % len(buckets["NONE"]))
        for rid, body, _d in buckets["NONE"]:
            print("  %-5s %s" % (rid, re.sub(r"\s+", " ", body)[:104]))
        print()

    if buckets.get("COVERED"):
        print("=== COVERED (%d) ===" % len(buckets["COVERED"]))
        for rid, _b, d in buckets["COVERED"]:
            print("  %-5s %s" % (rid, ", ".join(sorted(d))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
