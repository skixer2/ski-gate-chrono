# T10: BHI260AP Wake-Up Sensor Investigation Findings

**Date:** 2026-08-19
**Status:** Complete (coordinator research — subagent failed)

## 1. Arduino_BHY2 Wake-Up Sensor Support

**Supported — via the C API layer.** The Arduino_BHY2 library wraps the
Bosch BHy2 SensorAPI (`bhy2.c` / `bhy2.h`) and exposes:

- `BHY2.configureSensor(sensorId, sampleRate, latency)` — enables any
  virtual sensor by ID, including wake-up sensors
- `BHY2.configureSensorRange(id, range)` — sets dynamic range
- `BoschSensortec::bhy2_setParameter(param, buffer, length)` — raw
  parameter write (for threshold)
- `BoschSensortec::bhy2_getParameter(param, buffer, length, &actual_len)`
  — raw parameter read

**Relevant wake-up sensor IDs** (from `bosch/bhy2_defs.h`):

| ID | Name | Notes |
|----|------|-------|
| 6 | `BHY2_SENSOR_ID_ACC_WU` | Acc corrected wake-up |
| 55 | `BHY2_SENSOR_ID_SIG` | Significant motion (SW) |
| 57 | `BHY2_SENSOR_ID_WAKE_GESTURE` | Wake gesture |
| 77 | `BHY2_SENSOR_ID_MOTION_DET` | Motion detect |
| 138 | `BHY2_SENSOR_ID_SIG_HW` | HW significant motion |
| 141 | `BHY2_SENSOR_ID_SIG_HW_WU` | **HW significant motion wake-up** ← best candidate |
| 142 | `BHY2_SENSOR_ID_ANY_MOTION` | Any motion |
| 143 | `BHY2_SENSOR_ID_ANY_MOTION_WU` | **Any motion wake-up** ← alternative |

**Recommendation:** Use **`BHY2_SENSOR_ID_ANY_MOTION_WU` (143)** — simplest,
triggers on any motion above threshold. If false wakes are excessive even at
high threshold, try `BHY2_SENSOR_ID_SIG_HW_WU` (141) which requires sustained
motion.

The library has a `SensorActivity` class but it's for activity classification
(walking/running/etc), not for wake-up. No special wrapper needed — just use
`configureSensor()` with the wake-up sensor ID.

## 2. BHI260AP Autonomous Operation in System Off

**Yes, sensors stay active without host polling.**

The BHI260AP is a smart sensor hub with its own 32-bit MCU. Once a virtual
sensor is configured (via SPI `bhy2_set_virt_sensor_cfg`), the BHI260AP:
- Runs the sensor firmware autonomously
- Monitors the IMU at the configured sample rate
- Generates a host interrupt (INT pin) when the wake-up condition is met
- Does NOT require the host to call `BHY2.update()` — that only reads the FIFO

**Critical:** The current firmware never calls `BHY2.end()`. We simply stop
calling `BHY2.update()` before entering System Off. The BHI260AP stays
powered (VCC doesn't drop in System Off) and continues running the configured
wake-up sensor.

**No workaround needed.** Configure the wake-up sensor, stop polling, enter
System Off. BHI260AP will INT the nRF52 on motion.

## 3. INT Pin Configuration

From `bosch/bhy2_defs.h`:

```c
#define BHY2_REG_HOST_INTERRUPT_CTRL  0x07
#define BHY2_ICTL_ACTIVE_LOW          0x20   /* INT active low */
#define BHY2_ICTL_EDGE                0x40   /* Edge triggered */
#define BHY2_ICTL_OPEN_DRAIN          0x80   /* Open drain */
```

**Default:** INT is **active-low, edge-triggered**.

**nRF52 P0.14 GPIO SENSE configuration for System Off wake:**
```c
nrf_gpio_cfg_sense_input(14, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
```
- Pull-up (INT pin idles high, goes low on interrupt)
- Sense low (falling edge → DETECT signal → wake from System Off)

**Same for LDC INTB (P0.02):** already configured as `INPUT_PULLUP` in
`LDC1612::enable_interrupt()`. Needs `nrf_gpio_cfg_sense_input()` before
System Off entry.

**Note:** The BHY2 library may configure the INT pin register at `begin()`
time. We should verify the current `BHY2_REG_HOST_INTERRUPT_CTRL` value
after `BHY2.begin(NICLA_STANDALONE)` and ensure edge mode is set.

## 4. Motion Threshold Configuration

**The `bhy2_virt_sensor_conf` struct has a `sensitivity` field:**
```c
struct bhy2_virt_sensor_conf {
    uint16_t sensitivity;   /* ← threshold for wake-up sensors */
    uint16_t range;
    uint32_t latency;
    bhy2_float sample_rate;
};
```

**But `bhy2_set_virt_sensor_cfg()` only takes `sample_rate` and `latency`** —
it does NOT set sensitivity or range. The Arduino_BHY2 wrapper
(`configureSensor(id, rate, latency)`) similarly doesn't expose sensitivity.

**To set the threshold, two paths:**

### Path A: `bhy2_get_parameter()` / `bhy2_set_parameter()`
Each virtual sensor has a parameter page for configuration. The parameter
ID for any-motion threshold is sensor-specific and documented in the
BHI260AP firmware reference (not in the public datasheet). The Bosch C API
source (`bhy2.c`) may have helper functions.

### Path B: Direct `bhy2_virt_sensor_conf` manipulation
1. `bhy2_get_virt_sensor_cfg(sensor_id, &conf, &dev)` — read current config
2. Modify `conf.sensitivity`
3. Write back via `bhy2_set_parameter()` with the sensor's parameter page

**Evidence from the library:**
```cpp
// BoschSensortec.cpp:
int8_t BoschSensortec::bhy2_setParameter(uint16_t param, const uint8_t *buffer, uint32_t length) {
    return bhy2_set_parameter(param, buffer, length, &_bhy2);
}
```

**Range and units:** Not documented in the public headers. The BHI260AP
datasheet (BST-BHI260AP-DS000) and the firmware reference would specify the
threshold range. Given that `sensitivity` is `uint16_t`, it likely maps to
acceleration in mg (milli-g) or a fixed-point format.

**For T11 (user-settable threshold):** Map the BLE `wake_threshold` char
(0–255) to the BHI260AP `sensitivity` field. A simple linear mapping:
- 0 = most sensitive (lowest threshold)
- 255 = least sensitive (highest threshold)
- Default ~128 (medium)

The exact scaling needs bench testing — start with a conservative default
and let the athlete adjust via the app slider.

## 5. Direct Register Path (if library insufficient)

The Arduino_BHY2 library is sufficient for:
- Enabling the wake-up sensor (`configureSensor`)
- Setting sample rate and latency
- Reading/writing parameters (`bhy2_setParameter`/`bhy2_getParameter`)

It is NOT sufficient for:
- Setting the `sensitivity` field directly (no API exposed)

**Required direct calls:**
```c
// Read current config
struct bhy2_virt_sensor_conf conf;
bhy2_get_virt_sensor_cfg(BHY2_SENSOR_ID_ANY_MOTION_WU, &conf, &dev);

// Modify threshold
conf.sensitivity = user_threshold;

// Write back — need to find the parameter page for this sensor
// Option 1: bhy2_set_parameter() with sensor-specific parameter ID
// Option 2: bhy2_set_virt_sensor_cfg() then a separate sensitivity write
```

**No firmware patch needed.** The BHI260AP ships with built-in virtual
sensors for any-motion and significant-motion. No custom firmware loading
required.

## 6. Recommendation

**Use the BHY2 library for sensor enablement. Use direct parameter writes
for threshold.**

### Implementation approach for T9 (configure sensor before System Off):

```cpp
// Before entering System Off:
// 1. Configure any-motion wake-up sensor
BHY2.configureSensor(BHY2_SENSOR_ID_ANY_MOTION_WU, 1.0f, 1000);
//   ^ sample_rate=1 Hz (low power), latency=1000 ms

// 2. Set threshold (via parameter write — needs parameter ID)
//    TODO: find parameter ID from BHI260AP firmware reference
//    BoschSensortec::bhy2_setParameter(param_id, threshold_bytes, 2);

// 3. Configure INT pin for GPIO SENSE
nrf_gpio_cfg_sense_input(14, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

// 4. Also configure LDC INTB for SENSE
nrf_gpio_cfg_sense_input(2, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

// 5. Enter System Off
sd_power_system_off();
```

### Open item: parameter ID for any-motion threshold
The exact parameter ID for the any-motion threshold is NOT in the public
headers. Sources to check:
- BHI260AP datasheet (BST-BHI260AP-DS000) — may not include firmware params
- BHI260AP firmware reference / application note
- Bosch community forum (community.bosch-sensortec.com)
- Reverse-engineer: `bhy2_get_virt_sensor_cfg()` returns `sensitivity`, so
  there must be a way to write it. Check `bhy2.c` source for the set path.

**Workaround if parameter ID can't be found:** Use the `range` field instead
(`bhy2_set_virt_sensor_range(id, range)`) — higher range = less sensitive.
This is already exposed by the library (`configureSensorRange`). Map the
user threshold to the range value. This may be sufficient for our needs.

## 7. References

- Library source: `.pio/libdeps/nicla/Arduino_BHY2/src/`
  - `bosch/bhy2_defs.h` — sensor IDs, INT config bits, struct definitions
  - `bosch/bhy2.h` — C API function declarations
  - `BoschSensortec.cpp` — wrapper functions (`configureSensor`,
    `bhy2_setParameter`, etc.)
  - `Arduino_BHY2.cpp` — Arduino wrapper (`configureSensor`, etc.)
- BHI260AP product: https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ap/
- BHy2 SensorAPI: https://github.com/boschsensortec/BHI2xy_SensorAPI
- SGC hardware doc: `sgc_architecture_hardware.md` — P0.14 INT, P0.02 INTB