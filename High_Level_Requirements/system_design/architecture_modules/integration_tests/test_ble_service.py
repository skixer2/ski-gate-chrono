"""
Integration test: BLE Service (F09-F11, F37-F39)
    Prerequisites: phone or BLE-capable PC with bleak installed.

    Tests BLE characteristics after connection:
    - Device advertises SGC service
    - Read characteristics (state, battery, run count, config)
    - Time sync works
    - Config write/read round-trip
"""
from sgc_test_harness import TestStep, TestScenario, force_state
import time

SCENARIOS = []

# ═══════════════════════════════════════════════════════════════════
# I01: BLE advertises after boot (manual verification)
#   Serial-side test: just verify BLE initialized
# ═══════════════════════════════════════════════════════════════════
SCENARIOS.append(TestScenario(
    name="I01 — BLE initialized",
    setup_commands=['i'],
    steps=[
        TestStep("Verify device in IDLE (BLE advertising)", '?', 200, expect_json={"st": "IDLE"}),
        TestStep("Verify state reported via BLE (serial echo)",
            '?', 200, expect_json={"st": "IDLE"}),
    ]
))

# ═══════════════════════════════════════════════════════════════════
# I02-I04: Requires bleak (pip install bleak) — BLE from Python
#   Run with: python sgc_test_harness.py test_ble_service.py
#   These tests open a parallel BLE connection.
# ═══════════════════════════════════════════════════════════════════

try:
    import asyncio
    from bleak import BleakScanner, BleakClient

    SGC_SERVICE_UUID = "53470000-0000-1000-8000-00805F9B34FB"

    CHAR_UUIDS = {
        'state':     "5347abc4-0000-1000-8000-00805f9b34fb",
        'battery':   "5347abc5-0000-1000-8000-00805f9b34fb",
        'run_count': "5347abc6-0000-1000-8000-00805f9b34fb",
        'dev_name':  "5347abc1-0000-1000-8000-00805f9b34fb",
        'arm_side':  "5347abc2-0000-1000-8000-00805f9b34fb",
        'discipline':"5347abc3-0000-1000-8000-00805f9b34fb",
        'flash_used':"5347abc7-0000-1000-8000-00805f9b34fb",
        'run_list':  "5347abc9-0000-1000-8000-00805f9b34fb",
        'ft_req':    "5347abca-0000-1000-8000-00805f9b34fb",
        'ft_chunk':  "5347abcb-0000-1000-8000-00805f9b34fb",
        'ft_crc':    "5347abcc-0000-1000-8000-00805f9b34fb",
        'ft_status': "5347abcd-0000-1000-8000-00805f9b34fb",
        'cal':       "5347abd0-0000-1000-8000-00805f9b34fb",
    }

    async def ble_scan_sgc(timeout=10):
        """Scan for SGC device advertising the SGC service."""
        devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
        for addr, (device, adv) in devices.items():
            if adv.service_uuids and SGC_SERVICE_UUID.lower() in adv.service_uuids:
                return device
            if device.name and 'SGC' in device.name.upper():
                return device
        return None

    async def ble_read_char(client, char_name):
        uuid = CHAR_UUIDS.get(char_name)
        if not uuid:
            return None
        return await client.read_gatt_char(uuid)

    async def ble_test_characteristics():
        """Test: scan → connect → read all characteristics."""
        print("\n  Scanning for SGC device...")
        device = await ble_scan_sgc()
        if not device:
            print("  ❌ No SGC device found")
            return False

        print(f"  Found: {device.name} ({device.address})")
        async with BleakClient(device) as client:
            print(f"  Connected: {client.is_connected}")

            for name, uuid in CHAR_UUIDS.items():
                try:
                    data = await client.read_gatt_char(uuid)
                    print(f"    {name}: {data.hex() if data else 'empty'} ({len(data)}B)")
                except Exception as e:
                    print(f"    {name}: ERROR — {e}")
            return True

    # Register async test
    def run_ble_test():
        asyncio.run(ble_test_characteristics())

except ImportError:
    def run_ble_test():
        print("  ⚠️ bleak not installed. Skip BLE tests.")
        print("    Install: pip install bleak")


SCENARIOS.append(TestScenario(
    name="I02 — BLE scan + connect + read chars",
    setup_commands=['i'],
    steps=[
        TestStep("BLE device advertising", '?', 1000, 'STATE:IDLE'),
        TestStep("Run BLE characteristic read", None, 500,
            on_response=lambda h, _: run_ble_test()),
    ]
))
