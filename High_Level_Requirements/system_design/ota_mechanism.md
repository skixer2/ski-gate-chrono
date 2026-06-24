# SGC OTA Firmware Update Design

## Context

Once the SGC device is potted in waterproof glue, the USB port is permanently inaccessible. The **only** update path is BLE. The device uses an nRF52832 (512KB internal flash) + 2MB external SPI flash (MX25R1635F).

**Good news:** The Nicla Sense ME stock bootloader already supports Nordic DFU over BLE. Arduino ships a `dfu.html` WebBLE tool for it. We don't need a custom OTA mechanism — we just need to trigger DFU mode from the app and use existing Nordic DFU protocol on the phone side.

## JTAG Recovery Access

Before potting, leave a **small sealed connector** accessible for SWD/JTAG recovery:

### Option A: Tag-Connect TC2050 (recommended)
- No soldered connector on the PCB — uses spring-loaded pogo pins against test pads
- Add the TC2050 footprint to the SGC PCB
- Leave a small silicone plug-covered hole in the enclosure
- Cable stays external; pogo pins press through the hole during recovery

### Option B: Sealed Micro Connector
- Use a waterproof micro JST or Molex connector (IP67-rated)
- 4 wires: SWDIO, SWCLK, GND, VCC (3.3V)
- Leave connector flush with enclosure surface, capped with waterproof plug
- Use CMSIS-DAP or J-Link to reflash if bricked

### Recovery Procedure
1. Remove waterproof cap from JTAG port
2. Connect CMSIS-DAP debug probe (or J-Link)
3. Flash known-good firmware via PlatformIO: `pio run -t upload -e nicla`
4. Re-cap the port

**Note for custom PCB:** The nRF52832 JTAG pins are shared with GPIOs (SWDIO=P0.18/nRESET, SWDCLK=P0.20). Ensure the PCB routes these to the JTAG connector AND that the application doesn't reconfigure them in a way that locks out the debugger. The `Nicla_System` library leaves them as SWD by default — don't use `pinMode()` on P0.18 or P0.20.

## OTA Strategy — Nordic DFU (Simplified)

### Why not custom OTA?
The Nicla bootloader **already supports BLE DFU** via the Nordic DFU protocol. Arduino ships `nicla-sense-me-fw/bootloader` with this built in. There's a Flutter package `nordic_dfu` that handles the protocol. We just need to:
1. Trigger DFU mode from our app
2. Send the firmware .zip via `nordic_dfu`

### Architecture

```
Phone App                         BLE                         SGC Device
┌──────────────────┐    ┌──────────────────────┐    ┌────────────────────┐
│ nordic_dfu       │    │ Device in DFU mode    │    │ Bootloader (24KB)  │
│ package          │───▶│ Name: "DfuTarg"       │───▶│ Nordic DFU Service  │
│ .zip file        │    │ UUID: 00001530-...    │    │ → flash new image  │
│ progress callback│    │                      │    │ → reboot           │
└──────────────────┘    └──────────────────────┘    └────────────────────┘
```

### How It Works

1. **Phone sends DFU trigger** → our app writes a command to `OTAControl` characteristic on the SGC service
2. **Firmware sets GPREGRET + resets** → enters bootloader DFU mode
3. **Device appears as "DfuTarg"** with Nordic DFU service (`00001530-1212-EFDE-1523-785FEABCD123`)
4. **Phone uses `nordic_dfu` package** → sends the .zip firmware package
5. **Bootloader flashes new image** → verifies → reboots into new firmware

### Nordic DFU Package

The .zip file is created using Nordic's `nrfutil` tool:

```bash
# Generate private key (once)
nrfutil keys generate private.pem

# Build firmware, then package it
nrfutil pkg generate \
  --application sgc_firmware.bin \
  --application-version 1 \
  --sd-req 0xB6 \
  --key-file private.pem \
  sgc_firmware_v2.1.zip
```

- `sd-req 0xB6` = SoftDevice S140 v6.1.1 (confirmed for Nicla Sense ME)
- The .zip goes in the Flutter app's `assets/` folder or downloaded from cloud

## Firmware Side — Triggering DFU Mode

### New Characteristic: OTAControl

Add to existing SGC BLE service:

| UUID | Name | Props | Purpose |
|------|------|-------|---------|
| `5347ABCF` | OTAControl | Write | `0x01` = Enter DFU mode |

### Implementation (in `sgc_service.cpp`)

```cpp
#include "nrf_power.h"  // for NRF_POWER->GPREGRET

// Standard magic value for Nordic buttonless DFU entry
#define DFU_MAGIC_BOOTLOADER_ENTER  0xB1

void sgc_ble_ota_enter_dfu() {
    // 1. Disconnect BLE cleanly
    // 2. Set GPREGRET to tell bootloader to enter DFU
    NRF_POWER->GPREGRET = DFU_MAGIC_BOOTLOADER_ENTER;
    // 3. Soft reset — bootloader sees GPREGRET, enters DFU mode
    NVIC_SystemReset();
}
```

When the phone writes `0x01` to `5347ABCF`, call `sgc_ble_ota_enter_dfu()`.

> **Note:** The exact magic value depends on the Nicla bootloader. If `0xB1` doesn't work, check the bootloader source in `nicla-sense-me-fw/bootloader/examples/` for the correct `DFU_MAGIC` constant. The standard values are:
> - `0xB1` — Nordic Buttonless DFU
> - `0x4C` — Some Arduino Mbed bootloaders
> - `0x07738135` — Older Mbed pattern

## Phone App Side

### Flutter Package: `nordic_dfu`

```yaml
# pubspec.yaml
dependencies:
  nordic_dfu: ^7.0.0
```

### OTA Screen

New screen in Settings tab: "Firmware Update"

### Trigger + DFU Flow

```dart
// Step 1: Tell SGC device to enter DFU mode
await sgcService.writeOTAControl(0x01);
await Future.delayed(Duration(seconds: 3)); // wait for device to reboot

// Step 2: Device is now "DfuTarg" — use nordic_dfu to flash
await NordicDfu().startDfu(
  deviceMacAddress,        // e.g. "EB:75:AD:E3:CA:CF"
  'assets/sgc_firmware_v2.1.zip',
  fileInAsset: true,
  onProgressChanged: (address, percent, speed, avgSpeed, currentPart, partsTotal) {
    setState(() => _progress = percent / 100.0);
  },
);

// Step 3: On completion, device reboots into new firmware
// Reconnect to SGC service automatically
```

### Important: MAC Address Mapping

After entering DFU mode, the device's MAC address may change (increase by 1). The `nordic_dfu` package v7+ supports address mapping:

```dart
NordicDfu().startDfu(
  deviceMacAddress,
  'assets/firmware.zip',
  fileInAsset: true,
  forceScanningForNewAddressInDfu: true, // handles MAC change
);
```

## Build & Deploy Workflow

```
PlatformIO build           nrfutil package           Flutter app
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│ pio run      │ ──.bin──▶│ pkg generate │ ──.zip──▶│ assets/      │
│ -e nicla     │         │              │         │              │
└──────────────┘         └──────────────┘         │ nordic_dfu   │
                                                  │ startDfu()   │
                                                  └──────────────┘
                                                         │
                                                         ▼
                                                  SGC Device (DfuTarg)
```

## What Already Exists vs What We Need to Build

| Component | Status | Work Needed |
|-----------|--------|-------------|
| Nicla bootloader with BLE DFU | ✅ Exists | Nothing — already on chip |
| Nordic DFU protocol | ✅ Standard | Nothing — `nordic_dfu` handles it |
| Firmware trigger (GPREGRET) | ❌ | Add `OTAControl` char + reset code |
| Phone OTA screen | ❌ | New screen + `nordic_dfu` integration |
| .zip packaging | ❌ | Script to run `nrfutil` after build |
| JTAG recovery connector | ❌ | Add TC2050 footprint to custom PCB |

## Implementation Order

### Phase 1: Firmware — DFU Trigger
- [ ] Add `OTAControl` (5347ABCF) characteristic to SGC service
- [ ] Implement `sgc_ble_ota_enter_dfu()` with GPREGRET + NVIC_SystemReset
- [ ] Test: write 0x01, verify device appears as "DfuTarg" in nRF Connect

### Phase 2: Phone App — OTA Screen
- [ ] Add `nordic_dfu` dependency
- [ ] Create OTA screen UI (firmware version, .zip picker, progress bar)
- [ ] Wire up: write OTAControl → wait 3s → startDfu()

### Phase 3: Build Automation
- [ ] Script: `pio run → nrfutil pkg generate → copy .zip to flutter assets`
- [ ] Version tracking (read SGC_VERSION from firmware)
- [ ] Cloud download (future)

### Phase 4: JTAG Recovery (PCB Design)
- [ ] Add TC2050 footprint to custom SGC PCB
- [ ] Route SWDIO (P0.18), SWDCLK (P0.20), GND, VCC to connector
- [ ] Enclosure: silicone plug over connector access hole

## Known Unknowns

1. **Exact GPREGRET magic value** — need to verify against Nicla bootloader. Test with nRF Connect: enter DFU via double-tap reset, check what GPREGRET value the bootloader expects. Or just try `0xB1` first (Nordic standard).
2. **SoftDevice version** — `sd-req 0xB6` = S140 v6.1.1. Verify with `nrfutil` or read from device.
3. **DFU MAC address offset** — some bootloaders use MAC+1 in DFU mode. The `nordic_dfu` package's `forceScanningForNewAddressInDfu` handles this.
