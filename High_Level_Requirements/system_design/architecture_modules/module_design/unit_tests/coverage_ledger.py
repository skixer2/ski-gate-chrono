#!/usr/bin/env python3
"""Generate an honest requirement -> automated-test coverage ledger.

JP 2026-11-20: "The goal is to cover as many requirements as possible with
automated tests, and to have a smoke test to execute every day."

This reads sgc_requirements.md (the top of the truth order) and, for each
requirement, reports how it can be verified automatically:

  SPEC     - statically, no hardware (constants, protocol, docs conformance)
  SERIAL   - automated on the bench over the serial console
  BLE      - automated on the bench over GATT
  HOST     - pure host unit test (parsers, packers, simulators)
  MANUAL   - genuinely not automatable (visual LED, physical IP68, RF field...)
  NONE     - automatable in principle but NO test exists yet  <-- the work list

It does not guess: a requirement is only credited to a test file when that
file mentions the requirement id in its text. Anything else is NONE.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REQ_PATH = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "..", "sgc_requirements.md"))

# Keyword heuristics for "not automatable by us".
MANUAL_HINTS = (
    ("visual", "visual inspection"),
    ("ip6", "IP rating - physical"),
    ("ip 6", "IP rating - physical"),
    ("drop test", "physical drop"),
    ("3 m", "physical impact"),
    ("-20", "thermal chamber"),
    ("temperature", "thermal"),
    ("erp", "radiated RF / EMC"),
    ("battery life", "long-run drain"),
    ("8 h", "long-run drain"),
    ("12 h", "long-run drain"),
)


def parse_requirements():
    """Return [(id, text, verification_text)] from the requirement tables."""
    txt = io.open(REQ_PATH, encoding="utf-8").read()
    out = []
    row = re.compile(r"^\| \*\*([FIR]\d+)\*\* \|(.*)$")
    for line in txt.splitlines():
        m = row.match(line.strip())
        if not m:
            continue
        rid = m.group(1)
        rest = m.group(2)
        cells = [c.strip() for c in rest.split("|")]
        body = cells[0] if cells else ""
        verify = cells[1] if len(cells) > 1 else ""
        out.append((rid, body, verify))
    return out


def test_index():
    """Map requirement id -> [test files that mention it]."""
    idx = {}
    for fn in sorted(os.listdir(HERE)):
        if not (fn.endswith(".py") and (fn.startswith("test_"))):
            continue
        txt = io.open(os.path.join(HERE, fn), encoding="utf-8",
                      errors="replace").read()
        for rid in re.findall(r"\b([FIR]\d+)\b", txt):
            idx.setdefault(rid, set()).add(fn)
    return idx


def classify(rid, body, verify, files):
    """Bucket one requirement.

    Device vs phone matters: JP's ask was automated tests for the device. Phone
    requirements are verified by the Dart suite (`flutter test`), which is a
    different lane and is reported separately rather than counted as a device
    gap. Scope out v2-only rows and non-applicable rows so the denominator is
    honest.
    """
    blob = (body + " " + verify).lower()
    if "v2 only" in blob or "v2-only" in blob or "not applicable (v1)" in blob:
        return "OUT_OF_SCOPE", {"v2 / not applicable in v1"}
    if "the phone shall" in blob or blob.startswith("phone"):
        return "PHONE", {"Dart app suite"}
    if files:
        names = " ".join(files)
        if "spec_constants" in names or "protocol_conformance" in names:
            if len(files) == 1:
                return "SPEC", files
        return "SERIAL", files
    for kw, why in MANUAL_HINTS:
        if kw in blob:
            return "MANUAL", {why}
    return "NONE", set()


def main():
    reqs = parse_requirements()
    idx = test_index()
    buckets = {"SERIAL": [], "SPEC": [], "NONE": [], "MANUAL": [],
               "PHONE": [], "OUT_OF_SCOPE": []}
    for rid, body, verify in reqs:
        kind, files = classify(rid, body, verify, idx.get(rid, set()))
        buckets[kind].append((rid, body, files))

    total = len(reqs)
    print("SGC requirement coverage — %d requirements parsed from sgc_requirements.md"
          % total)
    print()
    for k in ("SERIAL", "SPEC", "NONE", "MANUAL", "PHONE", "OUT_OF_SCOPE"):
        n = len(buckets[k])
        print("  %-7s %3d  (%4.1f%%)" % (k, n, 100.0 * n / total if total else 0))
    print()

    if buckets["NONE"]:
        print("=== AUTOMATABLE BUT UNCOVERED (%d) ===" % len(buckets["NONE"]))
        for rid, body, _ in buckets["NONE"]:
            print("  %-5s %s" % (rid, re.sub(r"\s+", " ", body)[:110]))
        print()

    if buckets["MANUAL"]:
        print("=== NOT AUTOMATABLE (%d) ===" % len(buckets["MANUAL"]))
        for rid, body, why in buckets["MANUAL"]:
            print("  %-5s %-22s %s" % (rid, ", ".join(sorted(why)) if why else "",
                                       re.sub(r"\s+", " ", body)[:80]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
