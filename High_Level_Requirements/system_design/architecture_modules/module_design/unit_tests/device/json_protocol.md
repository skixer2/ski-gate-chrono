# SGC JSON-Lines Protocol v2

*Firmware Phase 9 — Always-JSON (ADR-001)*

## Format

One JSON object per line (JSONL). No pretty-printing. Numeric values use minimal precision.

**All builds, always.** No `#ifdef` on output. Production and bench test use identical binaries.

## Key Names

| Key | Type | Meaning |
|-----|------|---------|
| `ev` | string | Event type |
| `st` | string | Device state (SLEEP/IDLE/ARMED/LOGGING/POST_RUN) |
| `from` | string | Previous state in transition |
| `to` | string | New state in transition |
| `cmd` | string | Command acknowledged (test mode only) |
| `r` | int | Ring buffer current count |
| `rm` | int | Ring buffer max size (500) |
| `p` | float | Barometric pressure (Pa) |
| `bat` | int | Battery percentage (0–100) |
| `evc` | int | Meta-event count from BHY2 |
| `qi` | bool | Qi charging (0/1) |
| `runs` | int | Total runs stored |
| `q` | [float×4] | Quaternion [w,x,y,z] |
| `la` | [float×3] | Linear acceleration [x,y,z] (mm/s²) |
| `tm` | bool | Test mode active (0/1) |
| `fr` | int | Frame count |
| `sz` | int | Compressed data size (bytes) |
| `id` | int | Run ID number |
| `cal` | int | Calibration accuracy (0–3) |
| `pre` | int | Pre-trigger frame count |
| `ok` | bool | Operation success (0/1) |
| `mag` | float | Quaternion magnitude |
| `reason` | string | Error/refusal reason |
| `ver` | string | Firmware version |
| `sub` | string | Subsystem name (init events) |
| `next` | int | Next flash address |
| `delta` | int | Speed delta (start detector) |
| `mode` | string | start detector mode ("drop" or "speed") |
| `pa` | float | Pressure delta Pa (start detector) |
| `err_at` | int | Byte offset of flash mismatch |
| `crc` | int | CRC32 value (file transfer) |

## Events

### Boot sequence
```json
{"ev":"boot","ver":"2.3"}
{"ev":"init","sub":"flash","ok":1}
{"ev":"index","runs":37,"next":13946880}
{"ev":"init","sub":"bhy2","ok":1}
{"ev":"init","sub":"ble","ok":1}
{"ev":"ready","st":"IDLE","runs":37}
```

### Status query (`?`)
```json
{"ev":"status","st":"ARMED","r":500,"rm":500,"p":10132500,"bat":85,"evc":0,"qi":0,"runs":3}
```

### State transitions
```json
{"ev":"st","from":"IDLE","to":"ARMED"}
{"ev":"st","from":"ARMED","to":"LOGGING"}
```

### Timeouts and cooldown
```json
{"ev":"timeout","from":"ARMED","to":"IDLE"}
{"ev":"cooldown","from":"POST_RUN","to":"IDLE"}
```

### Test mode commands (`-DTEST_MODE` builds only)
```json
{"ev":"cmd","cmd":"T","tm":1,"p":101325.0,"q":[1.00,0.00,0.00,0.00],"la":[0.00,0.00,0.00]}
{"ev":"cmd","cmd":"B","p":95000.0}
{"ev":"cmd","cmd":"Q","q":[0.71,0.00,0.71,0.00]}
{"ev":"cmd","cmd":"L","la":[1000.00,0.00,0.00]}
{"ev":"echo","p":101325.0,"q":[1.00,0.00,0.00,0.00],"la":[0.00,0.00,0.00]}
```

### Run lifecycle
```json
{"ev":"ring_full","r":500}
{"ev":"start","mode":"drop","pa":1.5}
{"ev":"log_start","run":4,"pre":500}
{"ev":"end_detected","fr":500}
{"ev":"run_saved","id":4,"fr":500,"sz":4625,"cal":0}
```

### Errors and refusals
```json
{"ev":"arm_refused","reason":"quat_magnitude","mag":0.45}
{"ev":"arm_blocked","reason":"cooldown"}
{"ev":"state_blocked","reason":"not_armed","current":"IDLE"}
{"ev":"battery_low","bat":12}
```

### Flash
```json
{"ev":"flash","ok":1}
{"ev":"flash","ok":0,"err_at":127}
{"ev":"factory_reset"}
{"ev":"reboot"}
```

### BLE File Transfer
```json
{"ev":"ft_request","run":0}
{"ev":"ft_start","run":0,"sz":5120}
{"ev":"ft_done","crc":3735928559}
{"ev":"ft_error","reason":"invalid_run"}
{"ev":"ft_error","reason":"not_found"}
```

## Design Decision (ADR-001)

No `#ifdef` on output code. JSON-lines in every build.
- Test commands (`T`,`B`,`Q`,`L`,`Z`) and sensor injection gated behind `-DTEST_MODE`
- Serial commands (`?`,`a`,`l`,`p`,`s`,`i`,`f`,`R`) work identically always
- Production binary is physically sealed — USB inaccessible, test commands harmless
- Bench test = worst-case timing (UART TX blocks loop); production runs faster
- Rationale: single code path, always tested. See `adr_001_always_json.md`.
