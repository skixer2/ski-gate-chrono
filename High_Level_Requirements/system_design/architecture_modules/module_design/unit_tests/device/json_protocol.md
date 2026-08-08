# SGC JSON-Lines Protocol v2.20

*Firmware Phase 9 — Always-JSON (ADR-001)*  
*Aligned with FW ≥4.80 / Opt-A linear pre-roll (`v4.79-best-s03` + unit-test fixes)*

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
| `r` | int | Pre-roll current count (ARMED fill) |
| `rm` | int | Pre-roll arm cap = **3000** (`ARM_FILL_CAP`) |
| `rh` | int | Pre-roll head index |
| `p` | float/int | Barometric pressure (**Pascals**) — status and echo |
| `bat` | int | Battery percentage (0–100) |
| `evc` | int | Meta-event count from BHY2 |
| `qi` | bool | Qi charging (0/1) |
| `runs` | int | Runs currently stored |
| `total_runs` | int | Lifetime run counter |
| `q` | [float×4] | Quaternion [w,x,y,z] |
| `la` | [float×3] | Linear acceleration [x,y,z] (mm/s²) |
| `tm` | bool | Test mode active (0/1) |
| `fr` | int | Frame count |
| `sz` | int | Compressed / stored data size (bytes) |
| `id` | int | Run ID number |
| `store` | string | `"raw"` (Opt-A RawRunStore) |
| `ok` | bool | Operation success (0/1) |
| `ver` | string | Firmware version |
| `cap_s` | int | Arm fill capacity in seconds (30) |
| `arm_cap` | int | Same as rm (preroll_prep event) |
| `keep` | int | `PREROLL_KEEP` = 1000 |
| `total` | int | Total pre-roll slots = 4000 |

## Events

### Boot sequence
```json
{"ev":"boot","ver":"4.80"}
{"ev":"init","sub":"flash","ok":1}
{"ev":"preroll_prep","arm_cap":3000,"total":4000,"keep":1000,"why":"boot"}
{"ev":"init","sub":"bhy2","ok":1}
{"ev":"init","sub":"ble","ok":1}
{"ev":"ready","st":"IDLE","runs":0}
```

### Status query (`?`)
```json
{"ev":"status","st":"ARMED","r":1285,"rm":3000,"rh":1285,"p":89721,"bat":90,"evc":0,"qi":0,"runs":1,"total_runs":1,"ver":"4.80","tm":0}
```

`p` is **Pascals** (`hPa * 100` from BHY2, or injected Pa in test mode).

### State transitions
```json
{"ev":"st","from":"IDLE","to":"ARMED"}
{"ev":"st","from":"ARMED","to":"LOGGING"}
{"ev":"timeout","from":"ARMED","to":"IDLE"}
{"ev":"cooldown","from":"POST_RUN","to":"IDLE"}
{"ev":"preroll_prep","arm_cap":3000,"total":4000,"keep":1000}
```

### Pre-roll full
Emitted once when ARMED fill reaches `ARM_FILL_CAP` (3000). This races the 30 s ARM timeout at 100 Hz — unit tests should prefer `r >= N` polling, not only `ring_full`.

```json
{"ev":"ring_full","r":3000,"cap_s":30}
```

### Test mode commands

Test mode (`T`) enables simulated sensor injection.

| Cmd | Meaning |
|-----|---------|
| `T` | Toggle test mode |
| `B <Pa>` | Set pressure in **Pascals** (e.g. `B 101325`) — marks **manual** frame |
| `Q w x y z` | Set quaternion — marks **manual** frame |
| `L x y z` | Set lin-acc (mm/s²) when tm=1 — marks **manual** frame |
| `Z` | Echo injected values (`p` in **Pa**) |
| `S` | Enter stream pull mode (0x3F / 16-byte RawFrame) |

**Manual vs stream**

- After any `B`/`Q`/`L`, `g_manual_frame=true` → ARM does **not** open stream. Unit tests inject this way.
- Stream path (S03): test mode ON **without** manual inject, or `S` — ARM pulls frames via `0x3F`.
- When tm=0, bare `L` is the S06 drain-LOGGING command (not lin-acc).

```json
{"ev":"cmd","cmd":"T","tm":1}
{"ev":"cmd","cmd":"B","p":101325.0}
{"ev":"echo","p":101325.0,"q":[1.0,0.0,0.0,0.0],"la":[0.0,0.0,0.0]}
```

**Stream test (S03):** ARM alone triggers pull-model streaming when tm ON and no manual frame.

```json
{"ev":"st","from":"IDLE","to":"ARMED"}
// Firmware sends 0x3F; PC responds with 16-byte RawFrame
{"ev":"sd","p0":79725.0,"pa":79745.0,"drp":0.21}
{"ev":"start","mode":"drop","m":2.3}
{"ev":"st","from":"ARMED","to":"LOGGING"}
```

### Force LOGGING variants

| Cmd | Flag | End detector | Ring |
|-----|------|--------------|------|
| `l` | `g_force_logging` | **skipped** (S04 rate) | cleared |
| `L` (tm=0) | `g_bench_drain` | **skipped** (S06) | drain path |
| start det | natural | **active** | trim keep 1000 |

Unit tests that need end detection must enter LOGGING via start detector, not `l`.

### Run lifecycle
```json
{"ev":"run_created","ok":1,"id":1,"store":"raw"}
{"ev":"run_saved","id":1,"fr":162,"sz":1188,"dur_ms":1686,"fps10":960,"ok":1,"store":"raw","we":0,"runs":2,"total":2}
```

### Flash self-test (`f`)
Uses reserved sector `0x1FE000` (does **not** erase pre-roll at 0x0000).

```json
{"ev":"flash","ok":1}
{"ev":"flash","ok":0,"err_at":127}
```

### Errors and refusals
```json
{"ev":"arm_refused","reason":"quat_magnitude","mag":0.45}
{"ev":"arm_blocked","reason":"cooldown"}
{"ev":"state_blocked","reason":"not_armed","current":"IDLE"}
{"ev":"state_blocked","reason":"not_logging","current":"ARMED"}
```

## Design Decision (ADR-001)

No `#ifdef` on output code. JSON-lines in every build.
- Test commands (`T`,`B`,`Q`,`L`,`Z`,`S`) always compiled; gated by runtime tm / manual flags
- Serial commands (`?`,`a`,`l`,`L`,`p`,`s`,`i`,`f`,`R`) work identically always
- Bench test = worst-case timing (UART TX blocks loop); production runs faster
- Rationale: single code path, always tested. See `adr_001_always_json.md`.
