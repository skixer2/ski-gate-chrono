#!/usr/bin/env python3
"""Build the device-test work list: the 37 uncovered REQUIREMENTS, ranked.

JP 2026-11-21: "maximum requirement coverage with minimal software changes."

That constraint is decisive for sequencing. A requirement whose only observable
surface is a console event or a GATT read needs NO firmware change to test.
One that needs a new telemetry line needs a firmware change. So rank by
(needs firmware change?) ASC, then by blast radius if it breaks.

This prints the work list with, for each requirement: what to assert, which
observable surface already exists, and whether firmware work is required.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REQ_PATH = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "..", "sgc_requirements.md"))
FW_SRC = os.path.normpath(os.path.join(
    HERE, "..", "implementation", "Firmware_Zephyr", "apps", "03_pull_proto", "src"))

# Observable surfaces that already exist in the firmware, with the source proof.
SURFACES = {
    '{"ev":"sm","from":X,"to":Y}': "recorder.c + power_state.c (state transitions)",
    '{"rec":"<STATE>"}': "serial 'D' extras (pull_proto.c) - state readout",
    '{"ev":"b1_rate",...}': "sensor_prod.c bench stats (rv/lacc/baro counters)",
    '{"ev":"baro_drop",...}': "start detector",
    '{"ev":"arm_timeout"}': "recorder.c ARMED timeout",
    '{"ev":"run_ready"}': "recorder.c POST_RUN -> SLEEP",
    '{"run":id,...}/{"d_end":n}': "pull_proto.c 'D' directory",
    '{"free":N}': "store_free_full_runs()",
    '{"ev":"flash","ok":N}': "flash self-test",
    '{"ev":"b1_cfg",...}': "sensor_prod config steps",
    'ABC6 GATT (fw version)': "ble_config.c read_fw_version",
    'ABC5 GATT (battery)': "ble_config.c",
    'ABC4 GATT (device state)': "ble_config.c state byte",
}

# Requirements that are cheap to test with what already exists (no FW change).
READY_NOW = {
    "F03": ("arm via piezo press", '{"ev":"sm","to":"ARMED"}', "serials bench: press -> ARMED"),
    "F05": ("pre-roll merged into run on start", "run_saved payload size vs pre-roll count",
            "record a run, compare fr/sz to the ARMED fill count"),
    "F06": ("flatline auto-terminate", '{"ev":"flatline_end"} + {"ev":"sm","to":"SLEEP"}',
            "inject still air, assert termination"),
    "F08": ("circular flash overwrite", '{"d_end":n} + {"run":id} rotation + {"free":N}',
            "fill past budget, assert oldest id disappears"),
    "F09": ("run metadata over GATT", '{"run":id,"ts","sz"} via the D listing',
            "read directory, assert fields"),
    "F11": ("config read/write (name, arm side, discipline)", "ABC1/ABC2/ABC3 GATT",
            "write, reboot, read back"),
    "F13": ("wake from System Off on button", '{"ev":"boot","rr":4}',
            "1 h test / forced poweroff then press"),
    "F37": ("epoch set on connect", '{"ev":"epoch_sync"}', "connect, assert line + ts sanity"),
    "F42": ("phone-only factory reset", "bond/name/PIN cleared, runs kept",
            "app-driven; device side asserts config cleared"),
    "F46": ("delete run on device", '"OK deleted N" / "OK already gone"',
            "d <id>, assert directory shrinks"),
}

READY_WITH_FW_NUDGE = {
    "F38": ("BLE MTU negotiation >=500 B", "ABC0 write + MTU report",
            "device does not currently log the negotiated MTU"),
    "F39": ("BLE bonding / LE Secure", "bond + reconnect without re-pair",
            "no event emitted on bond state"),
    "F40": ("OTA via MCUboot+MCUmgr", "SMP upload + reboot + ABC6 version",
            "long test; needs an SMP client path on the bench"),
}


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


def covered_ids():
    idx = set()
    for fn in sorted(os.listdir(HERE)):
        if not (fn.startswith("test_") and fn.endswith(".py")):
            continue
        txt = io.open(os.path.join(HERE, fn), encoding="utf-8",
                      errors="replace").read()
        idx.update(re.findall(r"\b([FIR]\d+)\b", txt))
    return idx


def is_device(body):
    b = body.lower()
    if "v2 only" in b or "not applicable (v1)" in b:
        return False
    if "the phone shall" in b:
        return False
    return True


def main():
    reqs = parse_requirements()
    have = covered_ids()

    device = [rid for rid, (b, _v) in reqs.items() if is_device(b)]
    uncovered = [rid for rid in device if rid not in have]

    print("DEVICE REQUIREMENTS: %d total, %d covered, %d UNCOVERED"
          % (len(device), len(device) - len(uncovered), len(uncovered)))
    print()

    print("=== TIER 1: testable NOW, no firmware change (%d) ===" % len(READY_NOW))
    for rid in sorted(READY_NOW):
        if rid not in uncovered:
            continue
        what, obs, how = READY_NOW[rid]
        print("  %-5s %s" % (rid, what))
        print("        assert : %s" % obs)
        print("        method : %s" % how)
    print()

    print("=== TIER 2: testable, needs a small firmware telemetry addition (%d) ==="
          % len(READY_WITH_FW_NUDGE))
    for rid in sorted(READY_WITH_FW_NUDGE):
        if rid not in uncovered:
            continue
        what, obs, gap = READY_WITH_FW_NUDGE[rid]
        print("  %-5s %s\n        gap: %s" % (rid, what, gap))
    print()

    rest = [r for r in uncovered
            if r not in READY_NOW and r not in READY_WITH_FW_NUDGE]
    print("=== TIER 3: remaining uncovered (%d) ===" % len(rest))
    for rid in sorted(rest):
        body, _ = reqs[rid]
        print("  %-5s %s" % (rid, re.sub(r"\s+", " ", body)[:100]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
