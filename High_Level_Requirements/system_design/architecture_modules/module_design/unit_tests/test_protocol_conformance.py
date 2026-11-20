#!/usr/bin/env python3
"""S08 - protocol conformance: do the DOCS and the TESTS speak the firmware's language?

No hardware. Runs in well under a second, so it belongs in every commit.

WHY THIS EXISTS
---------------
On 2026-11-20 we discovered that the entire device test suite and the protocol
document were written against the OLD Arduino firmware (Firmware_implementation/),
not the Zephyr product firmware (Firmware_Zephyr/apps/03_pull_proto). Three
concrete, provable drifts:

  1. State transitions.  52 assertions expect {"ev":"st",...}; the Zephyr firmware
     emits {"ev":"sm",...}. The requirements doc (F12) also says "sm".
  2. The '?' command.    
test_s07_sensor_liveness.py and many others send '?' expecting
     {"ev":"status","st":...}; the Zephyr console has no '?' branch and answers
     "E 4" (unknown command). The firmware README documents this explicitly:
     "? is not a command - there is no status query".
  3. The state readout.  The Zephyr build exposes state as {"rec":"SLEEP"}, which
     appears in NO test and NO doc line.

This is the same class of bug as the ARM-window drift: a CONSTANT that silently
diverged, invisible to every device test because the device tests never ran
against this firmware. Documents -> tests -> user is the truth order; this test
mechanises the first link.

WHAT IT DOES
------------
Parses the event names actually emitted by the Zephyr source (the ground truth
here is the CODE, since the requirement doc does not enumerate wire event names),
then checks the test suite and protocol doc against it. Reported as:

  FAIL  a test/doc asserts an event the firmware cannot emit   (real drift)
  WARN  the firmware emits an event no test/doc mentions       (coverage gap)

Exit 0 only when there are no FAILs.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW_SRC = os.path.normpath(os.path.join(
    HERE, "..", "implementation", "Firmware_Zephyr",
    "apps", "03_pull_proto", "src"))

# Events the Zephyr firmware emits, as  printk("{\"ev\":\"NAME\""  patterns.
EV_PAT = re.compile(r'\\"ev\\":\\"([a-z_0-9]+)\\"')

# Commands the console actually implements: cmd == 'X' branches.
CMD_PAT = re.compile(r"cmd\s*==\s*'([A-Za-z0-9?])'")


def scan_firmware():
    """Return (set of event names, set of command chars) from the Zephyr source."""
    if not os.path.isdir(FW_SRC):
        return None, None
    events, cmds = set(), set()
    for root, _dirs, files in os.walk(FW_SRC):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fn)
            try:
                txt = io.open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            events.update(EV_PAT.findall(txt))
            cmds.update(CMD_PAT.findall(txt))
    return events, cmds


def _test_files():
    """Only the actual test modules.

    Deliberately excludes sgc_stream_simulator.py / sgc_mock_runner.py: those
    model the OLD Arduino firmware's breadcrumbs ('bc', 'cbc') and are mocks,
    not assertions about the device under test.
    """
    me = os.path.basename(__file__)
    return [f for f in sorted(os.listdir(HERE))
            if f.startswith("test_") and f.endswith(".py") and f != me]


def scan_tests(events):
    """Count test references to '"ev": "X"' per event name."""
    refs = {}
    pat = re.compile(r'["\']ev["\']\s*:\s*["\']([a-z_0-9]+)["\']')
    for fn in _test_files():
        if not fn.endswith(".py") or fn == os.path.basename(__file__):
            continue
        txt = io.open(os.path.join(HERE, fn), encoding="utf-8",
                      errors="replace").read()
        for name in pat.findall(txt):
            refs.setdefault(name, set()).add(fn)
    return refs


def main():
    fw_events, fw_cmds = scan_firmware()
    if fw_events is None:
        print("FW source not found: %s" % FW_SRC)
        return 2

    print("Zephyr firmware events (%d): %s"
          % (len(fw_events), ", ".join(sorted(fw_events))))
    print("Zephyr console commands: %s" % "".join(sorted(fw_cmds)))
    print()

    refs = scan_tests(fw_events)
    test_events = set(refs)

    fails, warns = [], []

    # --- FAIL: a test asserts an event this firmware cannot emit -----------
    for name in sorted(test_events - fw_events):
        files = ", ".join(sorted(refs[name]))
        n = sum(1 for _ in files.split(","))
        fails.append(
            'test asserts {"ev":"%s"} but the Zephyr firmware never emits it '
            '(%s)' % (name, files))

    # --- FAIL: any command character the harness/test suite sends that the
    #          Zephyr console does not implement. This is what caught the
    #          harness sending '?' (the Arduino firmware's status query) to a
    #          build whose own README says "? is not a command".
    # Two forms carry a command character: an explicit .send("X ...") call and
    # a TestStep(command='X') / setup_commands=['X'] literal. Both must be
    # covered - the 'i' command hid in the TestStep form and slipped past an
    # earlier send()-only scan.
    send_pat = re.compile(r'\.send\(\s*["\']([^"\']+)["\']')
    step_pat = re.compile(r'command\s*=\s*["\']([^"\']+)["\']')
    list_pat = re.compile(r'(?:setup_commands|teardown_commands)\s*=\s*\[([^\]]*)\]')
    used = {}
    files = _test_files() + ["sgc_test_harness.py"]
    for fn in files:
        path = os.path.join(HERE, fn)
        if not os.path.isfile(path):
            continue
        txt = io.open(path, encoding="utf-8", errors="replace").read()
        cands = list(send_pat.findall(txt)) + list(step_pat.findall(txt))
        for lst in list_pat.findall(txt):
            for q in re.findall(r'["\']([^"\']+)["\']', lst):
                cands.append(q)
        for sent in cands:
            head = sent.strip()[:1]
            if head:
                used.setdefault(head, set()).add(fn)
    for ch in sorted(used):
        if ch not in fw_cmds:
            fails.append(
                "harness/tests send command %r but the Zephyr console has no "
                "'%s' branch (it answers 'E 4' unknown) -- used in %s"
                % (ch, ch, ", ".join(sorted(used[ch]))))

    # --- WARN: firmware events nobody tests or documents -------------------
    for name in sorted(fw_events - test_events):
        warns.append('firmware emits {"ev":"%s"} but no test references it'
                     % name)

    print("FAILURES (%d)" % len(fails))
    for f in fails:
        print("  FAIL  %s" % f)
    print()
    print("COVERAGE GAPS (%d)" % len(warns))
    for w in warns:
        print("  WARN  %s" % w)

    print()
    if fails:
        print("PROTOCOL_CONFORMANCE_FAIL  (%d drift errors)" % len(fails))
        return 1
    print("PROTOCOL_CONFORMANCE_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
