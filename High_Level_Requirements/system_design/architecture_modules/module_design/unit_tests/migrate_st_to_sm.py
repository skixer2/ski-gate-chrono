#!/usr/bin/env python3
"""Migrate {"ev":"st"} -> {"ev":"sm"} in the device test suite.

Direction of the fix is proven by the truth order:
  requirements F12 says  {"ev":"sm","from":"SLEEP","to":"SYSTEM_OFF"}
  Zephyr firmware emits  5x {"ev":"sm"} (recorder.c 4, power_state.c 1)
  Arduino-era firmware   emits {"ev":"st"}  (Firmware_implementation/src/test_json.h)
The suite was written against the Arduino firmware; it must follow the product one.

Idempotent: a file with no '"ev": "st"' is skipped unchanged.
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OLD = '"ev": "st"'
NEW = '"ev": "sm"'

changed = []
for fn in sorted(os.listdir(HERE)):
    if not (fn.startswith("test_") and fn.endswith(".py")):
        continue
    path = os.path.join(HERE, fn)
    s = io.open(path, encoding="utf-8").read()
    n = s.count(OLD)
    if n == 0:
        continue
    before = s
    s = s.replace(OLD, NEW)
    assert s != before, "replace was a no-op for %s" % fn
    io.open(path, "w", encoding="utf-8").write(s)
    changed.append((fn, n))

if not changed:
    print("NOTHING_TO_DO (already migrated)")
    sys.exit(0)

total = 0
for fn, n in changed:
    print("  %-32s %3d occurrences" % (fn, n))
    total += n
print("MIGRATED %d occurrences across %d files" % (total, len(changed)))
