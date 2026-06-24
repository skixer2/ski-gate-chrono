"""
Integration test: File Transfer (F10)
    Prerequisites: BLE-capable PC with bleak installed.
    Device must have at least one run stored.

    Tests:
    - Read run list
    - Request file transfer
    - Receive chunks
    - Verify CRC32
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'module_design', 'unit_tests'))
from sgc_test_harness import TestStep, TestScenario, force_state
import time

SCENARIOS = []

# ═══════════════════════════════════════════════════════════════════
# I03: File transfer (requires bleak + at least 1 run on device)
# ═══════════════════════════════════════════════════════════════════

try:
    import asyncio
    from bleak import BleakScanner, BleakClient

    SGC_SERVICE = "53470000-0000-1000-8000-00805F9B34FB"
    CHAR_RUN_LIST   = "5347abc9-0000-1000-8000-00805f9b34fb"
    CHAR_FT_REQUEST = "5347abca-0000-1000-8000-00805f9b34fb"
    CHAR_FT_CHUNK   = "5347abcb-0000-1000-8000-00805f9b34fb"
    CHAR_FT_CRC     = "5347abcc-0000-1000-8000-00805f9b34fb"
    CHAR_FT_STATUS  = "5347abcd-0000-1000-8000-00805f9b34fb"

    async def scan_sgc(timeout=10):
        devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
        for addr, (device, adv) in devices.items():
            if device.name and 'SGC' in device.name.upper():
                return device
        return None

    CRC32_TABLE = []
    for i in range(256):
        crc = i
        for j in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if (crc & 1) else crc >> 1
        CRC32_TABLE.append(crc)

    def crc32(data):
        crc = 0xFFFFFFFF
        for b in data:
            crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ b) & 0xFF]
        return crc ^ 0xFFFFFFFF

    async def ble_file_transfer():
        print("\n  Scanning for SGC device...")
        device = await scan_sgc()
        if not device:
            print("  ❌ No SGC device found")
            return

        print(f"  Found: {device.name} ({device.address})")
        async with BleakClient(device) as client:
            # Read run list
            try:
                rl = await client.read_gatt_char(CHAR_RUN_LIST)
                print(f"  Run list: {rl.decode('utf-8', errors='replace')[:200]}")
            except Exception as e:
                print(f"  Run list error: {e}")
                return

            # Request file transfer (run_id=0)
            import struct
            req = struct.pack('<H', 0)
            await client.write_gatt_char(CHAR_FT_REQUEST, req, response=True)
            print("  Requested run #0")

            # Collect chunks
            chunks = []
            def chunk_handler(sender, data):
                chunks.append(data)
                print(f"    Chunk: {len(data)}B (total {sum(len(c) for c in chunks)}B)")

            await client.start_notify(CHAR_FT_CHUNK, chunk_handler)

            # Wait for transfer complete (status = 2) or timeout
            for i in range(300):  # 30s timeout
                await asyncio.sleep(0.1)
                try:
                    status = await client.read_gatt_char(CHAR_FT_STATUS)
                    if status and status[0] == 2:
                        print(f"  Transfer complete: {sum(len(c) for c in chunks)} bytes")
                        break
                    elif status and status[0] == 3:
                        print("  Transfer error")
                        break
                except:
                    pass

            await client.stop_notify(CHAR_FT_CHUNK)

            # Verify CRC
            try:
                crc_data = await client.read_gatt_char(CHAR_FT_CRC)
                if crc_data and len(crc_data) >= 4:
                    device_crc = crc_data[0] | (crc_data[1] << 8) | (crc_data[2] << 16) | (crc_data[3] << 24)
                    all_data = b''.join(chunks)
                    local_crc = crc32(all_data)
                    if device_crc == local_crc:
                        print(f"  ✅ CRC match: 0x{device_crc:08X}")
                    else:
                        print(f"  ❌ CRC mismatch: device=0x{device_crc:08X} local=0x{local_crc:08X}")
            except Exception as e:
                print(f"  CRC read error: {e}")

    def run_ft_test():
        asyncio.run(ble_file_transfer())

except ImportError:
    def run_ft_test():
        print("  ⚠️ bleak not installed. Skip BLE file transfer test.")
        print("    Install: pip install bleak")


SCENARIOS.append(TestScenario(
    name="I03 — File transfer",
    setup_commands=['i'],
    steps=[
        TestStep("Device in IDLE", '?', 500, 'STATE:IDLE'),
        TestStep("Run file transfer test", None, 500,
            on_response=lambda h, _: run_ft_test()),
    ]
))
