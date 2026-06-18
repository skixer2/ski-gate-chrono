# SGC — UWB Positioning & Slope Infrastructure Context

*2026-06-05 — UWB vs RFID analysis, TDoA architecture, IMU fusion, snow cannon anchor deployment, tourist density via RFID, SGC board integration options.*

---

## 1. RFID vs UWB: Fundamental Difference

| | RFID (current SGC) | UWB |
|---|---|---|
| **What it measures** | "Tag crossed a detection zone, yes/no" | "Tag is at X,Y,Z coordinates" |
| **Data type** | Discrete events (gate crossing times + tag ID) | Continuous trajectory |
| **Precision** | N/A for positioning; ±0.01s timing | **10-30 cm** (raw), **sub-10 cm** with IMU fusion |
| **Update rate** | 5-20 Hz inventory rounds (per discipline) | **20-100 Hz** per tag (theoretical up to 1000 Hz) |
| **Range** | 1-3m passive UHF | **120m** (standard), **500m** with power amps |
| **Physics** | Backscatter (reader powers tag, tag modulates reflection) | Time-of-flight of nanosecond pulses (3.1-10.6 GHz) |
| **Tags** | Passive, no battery (~€0.20 each) | Active, battery-powered (~€15-25 module) |
| **Anchors** | UHF RFID reader on athlete (SGC board) | Fixed, wired-powered, synchronized |

**Key insight:** RFID gives gate crossing events. UWB gives the athlete's continuous position in space.

---

## 2. RFID Timing Precision: How ±0.01s Works Without High Refresh

RFID timing is fundamentally an EVENT, not a STREAM:

- Antennas at start/finish/splits create a well-defined electromagnetic detection zone
- The moment a tag enters that zone, the reader timestamps the detection against a GPS-synchronized master clock with microsecond resolution
- The zone boundary is sharp → "entering the zone" is a clean, repeatable event
- Only ONE successful read is needed — not continuous tracking
- Reader refresh rate matters for handling 100+ athletes crossing simultaneously, not per-athlete precision

**The catch for SGC:** Intermediate gates use pole-mounted passive UHF tags detected by the forearm device. The timing comes from the SGC's RTC, not from pole-mounted readers. This is fundamentally different from traditional race timing where antennas are at fixed timing points.

---

## 3. GS/SG/DH Geometry: Athlete Doesn't Pass Between Poles

In GS, SG, and DH, the athlete passes on the **same side** of both poles — the outside of the turn. The cloth flag connects them. The athlete does not go through the gate.

**Consequence for RFID pole tags:** The "plane between two poles" model is wrong. The SGC RFID reader on the athlete's forearm detects the nearest pole tag as they pass close to it. RSSI-based nearest-tag selection (F54) is the right approach.

**Consequence for UWB positioning:** With 2 anchors + terrain constraint (Z = f(X,Y), ski on snow), the 3D problem reduces to 2D. Two range circles from two UWB anchors intersect at two points in 3D space — the terrain surface picks the one on the snow. So 2 anchors can give a 2D fix IF you have an accurate digital terrain model.

---

## 4. UWB Architecture: TDoA ("Inverted GPS")

### Why TDoA Wins for Ski Racing

| Factor | TDoA | Two-Way Ranging (TWR) |
|--------|------|----------------------|
| Tag power draw | µA sleep, µs blinks → **months on coin cell** | Must receive + transmit → hours-days |
| Tag complexity | Pulse gen only → simple, cheap | MCU + protocol → complex |
| Scalability | 1 tag or 100 tags → same airtime per anchor | Tags compete for airtime → degrades with N |
| Position known by | Server (good: coach sees it) | Tag (needs radio to forward data) |
| Anchor sync required | **Yes — nanosecond precision** | No |
| Update rate | 50-100 Hz comfortable | 10-30 Hz with multiple anchors |

### How TDoA Works

```
TAG (athlete) ─── BLINK! (nanosecond pulse + IMU data payload) ───►
                   ╱          │           ╲
             Anchor1      Anchor2      Anchor3
             t₁=3.2ns     t₂=5.7ns     t₃=2.1ns
                   ╲          │           ╱
                    Central Server (synced clocks)
                         │
              Position fix from time differences
```

- **Tag** just transmits a single short pulse — doesn't receive, doesn't compute
- **Anchors** are synchronized to each other via PTP (IEEE 1588v2) over Ethernet backhaul (snow cannon power cables double as network)
- **Server** receives timestamps from all anchors, computes TDoA, trilaterates position
- The tag has **zero knowledge** of its own position — doesn't need to know

**NOT like GPS.** In GPS: satellites transmit → phone receives → phone computes.  
In UWB TDoA: tag transmits → anchors receive → server computes.

### IMU Data Piggybacks on the Blink

The synchronization problem is solved by putting IMU data IN the UWB blink packet:

```
┌─────────────────────────────────┐
│  UWB BLINK PACKET               │
│  ├─ Preamble (TDoA detection)   │
│  ├─ Tag ID                      │
│  ├─ Sequence number             │
│  ├─ IMU data: ax,ay,az,gx,gy,gz │  ← Rides in the same packet
│  └─ CRC                         │
└─────────────────────────────────┘
```

The IMU sample and the TDoA position fix refer to the **exact same physical instant**. No clock sync needed between tag and server. Qorvo DW3000 payload: 127 bytes max. IMU data needs ~19-31 bytes → well within budget. At 100 Hz this is ~3 KB/s — negligible.

### Clock Architecture

| Clock | Synced? | How? |
|-------|---------|------|
| Anchors | Each other, <1ns | PTP (IEEE 1588v2) over Ethernet in cannon conduit |
| Server | Anchor network | Same PTP domain |
| Tag (athlete) | **Nothing. Free-running.** | The blink IS the time reference |

---

## 5. Snow Cannon Infrastructure

### Why Snow Cannons Are the Key

Snow cannons are pre-deployed, powered, fixed-position infrastructure along training slopes:

- **Power:** 400V AC at every cannon → tap off with rectifier/conditioner
- **Position:** Fixed concrete pad → survey once with RTK GNSS (±2cm)
- **Distribution:** Every 40-60m along the slope → dense enough for continuous UWB coverage
- **Network backhaul:** Power cables can carry Ethernet (powerline networking) — no new trenching
- **Permanent:** Year-round installation, no setup/teardown

### Anchor Node Design

Each snow cannon hosts an anchor node:

```
400V cannon power ──► Power supply ──┬──► UWB anchor (Qorvo DW3000-based)
                                     ├──► Optional: RFID reader (tourist density)
                                     ├──► Ethernet-over-powerline modem
                                     └──► Environmental enclosure (IP67, heated)
```

### Per-Slope Economics (Pilot)

| Item | Unit Cost | × Qty | Total |
|------|-----------|-------|-------|
| UWB anchor module (DW3000-based) | €80-150 | 25 | €2,000-3,750 |
| IP67 enclosure + power supply | €200-300 | 25 | €5,000-7,500 |
| Powerline Ethernet modem | €50-80 | 25 | €1,250-2,000 |
| RTK GNSS survey (one-time) | €500/slope | 1 | €500 |
| Server + fusion software | €5,000-10,000 | 1 | €7,500 |
| Installation labor | €2,000-5,000 | 1 | €3,500 |
| **Total CapEx per slope** | | | **~€20,000-25,000** |

### Athlete Tag Cost

| Component | BOM Cost |
|-----------|----------|
| Qorvo DWM3000 module | €18-22 |
| IMU (ICM-20948 or built-in) | €8-15 |
| MCU (ESP32-C3 or nRF52) | €3-5 |
| LiPo battery 200mAh | €4-6 |
| Enclosure + mounting | €10-20 |
| **Total per athlete tag** | **~€45-70** |

---

## 6. UWB + IMU Sensor Fusion

### Why Fusion?

- UWB alone: 10-30 cm in outdoor snow conditions (multipath, body blockage, metal edges)
- IMU alone: drifts 1-2m per 30 seconds (integration error)
- **Fused:** sub-10 cm with Kalman filter

### Fusion Architecture

```
IMU (200 Hz) ──────► Predict step (dead reckoning) ──► State estimate (100 Hz)
                           │         ▲
UWB (50-100 Hz) ──────────┘         │
                           Update step (position fix)
```

With anchors every 40-60m and 120m+ UWB range, the athlete is always within range of 3-6 anchors → continuous, redundant position fixes.

### Update Rates for Skiing

At 25 m/s (GS turn), 50 Hz UWB → position fix every 0.5m of travel. 100 Hz → every 25 cm. More than enough for turn shape reconstruction.

---

## 7. Tourist Density Monitoring (Bonus Feature)

The same snow cannon infrastructure can host RFID readers for passive lift-pass detection:

### Option: Hybrid Anchor Node

```
Snow cannon ──► Power supply ──┬──► UWB anchor (racers, cm precision)
                               └──► UHF RFID reader (tourists, ~50m zone)
```

Tourists already wear passive RFID ski passes for lift access. These are read at lift gates only — **nobody reads them on the actual slopes.**

Adding a UHF RFID reader to each anchor node (~€100-200/unit) creates a slope-wide density monitoring mesh at near-zero marginal cost:
- No additional tags needed (uses existing lift passes)
- ~50m detection zone per reader (with directional antenna)
- Cannons at 40-60m spacing → near-continuous coverage
- **Live dashboard:** hot zones, bottlenecks, stopped-skier alerts, flow patterns
- **Revenue insight:** "2,300 skiers passed your terrace today"

---

## 8. Business Model

### Revenue Streams

| Customer | Product | Price |
|----------|---------|-------|
| FIS/national teams | Training slope access (UWB + analytics) | €500-1,000/day/slope |
| National federations | Annual subscription per slope | €5,000-10,000/yr |
| Ski clubs/schools | Hourly/daily training access | €100-300/session |
| Resort management | Tourist density dashboard | €5,000-10,000/yr |
| Other resorts | Infrastructure franchise/license | Revenue share or license fee |

### Payback (Single Slope)

- CapEx: ~€25,000
- 15-20 training days/season at full rate → **break-even in 1-2 seasons**
- Summer: maintenance only, no recurring cost (anchors draw milliwatts)

### Competitive Moat

The capital cost is in **physical infrastructure you own.** A competitor would need to build their own physical network on a different slope. It's concrete and copper, not software.

### Scaling

1. Pilot: 1 slope at Pila (CapEx ~€25K)
2. Validate with 1-2 national teams
3. Expand to 2-3 slopes at Pila
4. Package as turnkey franchise for other resorts
5. Recurring revenue from license fees + cloud analytics

---

## 9. SGC Board Integration: To Integrate UWB or Not?

### Current SGC Board Spec (Nicla → Custom PCB)

| Component | Function |
|-----------|----------|
| nRF52832 | BLE MCU, state machine |
| BHI260AP | 9-axis IMU (accel, gyro, mag, quaternion fusion) |
| BMP390 | Barometer (altitude, vertical speed) |
| LDC1612 | Inductive proximity (arming trigger) |
| UHF RFID reader | Passive pole tag detection (ST25RU3993 or Impinj E310) |
| SPI Flash 2MB | Circular run buffer |
| Qi receiver | Wireless charging |
| RGB LED + surface transducer | Status + audio feedback |

### Adding UWB Tag: Pro vs Con

| Pro | Con |
|-----|-----|
| SGC already has IMU → no additional sensor cost | Additional RF subsystem (antenna, layout, shielding) |
| SGC already has MCU → can drive DW3000 via SPI | MCU load: driving 100 Hz UWB blink + 100 Hz sensor logging simultaneously |
| SGC already has battery + charging → shared power | Battery impact: +5-10 mA pushing budget tight (43→50 mA worst case) |
| Single device on athlete → simpler | PCB space: Nicla form factor already dense |
| Future-proof: UWB infrastructure comes later | UWB tag is dead weight until snow cannon anchors are deployed |
| DW3000 module: ~€20, small (5×5mm QFN) | RF coexistence: UHF 900MHz + UWB 3.5-6.5GHz → careful layout needed |
| Can be powered down when no anchors present | Development complexity: firmware support for dormant subsystem |

### Recommendation: **Don't integrate into SGC v1. Build a separate UWB tag.**

**Rationale:**

1. **SGC is a gate-timing device.** Its value proposition is gate crossing detection via RFID + impact + barometry + IMU. Adding UWB complicates a focused, shippable product.

2. **UWB is a positioning device.** Different use case, different customers (coaches wanting full trajectory vs athletes wanting gate times).

3. **The infrastructure doesn't exist yet.** SGC works standalone TODAY. UWB tag is useless without anchors. Putting UWB on every SGC board means every athlete pays for hardware they can't use until/unless someone builds the snow cannon network.

4. **Separate development cycles.** SGC can ship in 2026. UWB infrastructure is 2027+ given the capital requirements. Decoupling them means neither blocks the other.

5. **A UWB-only tag is simpler and cheaper.** No BHI260AP, no BMP390, no LDC1612, no UHF RFID, no SPI Flash, no Qi. Just DW3000 + IMU + MCU + coin cell. BOM ~€35-45. Can be a belt clip or pocket puck.

6. **SGC can add UWB in v2.** If the infrastructure takes off, SGC v2 can integrate UWB when there's proven demand and the board is already being redesigned.

### Alternative: UWB-Ready Footprint on SGC v1 PCB

**Cost: ~€0.** Reserve the DW3000 footprint, antenna keepout zone, and SPI routing on the SGC custom PCB. Leave unpopulated. If UWB infrastructure materializes in 2027, populate the module on future production runs or retrofit existing boards (if enclosure allows access).

This gives optionality at zero incremental cost while keeping the v1 product focused.

---

## 10. References

- Qorvo DW3000: IEEE 802.15.4z, channels 5 (6.5 GHz) and 9 (8 GHz), 127-byte payload, 6.8 Mbps data rate
- KINEXON Sports: UWB + IMU fusion, sub-10 cm accuracy, used by NFL, NBA, FIFA, ice hockey
- Ski jumping research: UWB at 20 Hz + IMU, real-time trajectory tracking validated
- FIS timing standard: ±0.01s via RFID at start/finish/splits, not at every gate
- PTP (IEEE 1588v2): nanosecond-level clock synchronization over standard Ethernet
- SGC v1: nRF52832, BHI260AP, UHF RFID, Qi charging — arm-mounted gate timing device

---

*Next: snow cannon site survey at Pila, anchor node BOM + schematic, UWB tag prototype design.*
