# Gate detection & estimation — algorithm reference

*Written 2026-10-11 from JP's review questions. Code: `lib/processing/impact_detector.dart`, `lib/processing/gate_time_estimator.dart`, invoked in `lib/ui/screens/run_detail_screen.dart` `_processFrames` (recomputed every time a run is opened).*

## Pipeline

```
frames (100 Hz, decoded on phone)
  → ImpactDetector.detect()          → impacts (ms, force)
  → GateTimeEstimator.estimate()
       ω(t) → EMA filter → quiet-window centers → turn windows
       impacts × turn windows → association
  → gates (confirmed + fallback '*') → UI
```

## 1. Impact detection (`impact_detector.dart`)

- Signal: `|la|` in g. Wire units mm/s² (int16); `|g| = 9810 mm/s²`.
- Baseline: rolling **median** over 50 samples (0.5 s).
- Threshold: **max(baseline × 2.5, 1.8 g)**. The absolute floor exists
  because on quiet sections the relative threshold collapses toward
  noise and every TURN EDGE (~1.1 g) fired as an impact (S-bench run
  25: "17 gates, 0 estimated"). Pole contacts ≥ 2.2 g; recalibrate the
  floor on real slope data.
- Cooldown 200 ms; event = local 7-sample peak.

## 2. Turn windows (`gate_time_estimator.dart`)

- `_computeOmega`: ω(t) = 2·acos(|qᵢ·qᵢ₋₁|)/dt — total angular rate.
- Filter: EMA α = 0.1 (`_butterworthFiltfilt` — placeholder name).
- `_findZeros`: 20-sample moving average of ω < **0.3 rad/s** for a
  stretch → quiet period; its center = turn boundary. **NOT a
  mathematical zero crossing** — a quiet-period center between turns.
- Consecutive boundaries pair into **turn windows** (one candidate
  turn each).

## 3. Association — impact-first (JP directive 2026-10-10)

`_estimateBronze`:

1. Every impact → **confirmed gate at the impact time**.
2. Each impact **claims** the turn window containing it (±200 ms),
   else the nearest window center within **1.5 s**. Side inherited
   from the window (local-Y half-plane mean).
3. `claimed[]` = the **detection/estimation exclusion**: a claimed
   window cannot produce a fallback gate. One gate per turn; the
   impact always wins.
4. Unclaimed windows → **fallback gates** (`isEstimated = true`,
   shown with `*`), timestamped at the window apex (see §5).
5. Orphan impacts (no window) → confirmed gates; side from ±1 s
   local-Y mean.
6. Output sorted by time; gates numbered sequentially.

## 4. Side classification (`_classifySide`)

Mean of local-frame laY across the turn window: negative → right gate,
positive → left. Discipline-agnostic.

## 5. GS timing model (JP, 2026-10-11)

- Gate-to-gate: **1.4–1.5 s average**, 1.2 s flat, 2.0 s steep.
  Master race reference: **35 gates ≈ 50 s**.
- **A well-performed turn puts the gate 0.1–0.2 s AFTER the turn
  apex** (apex = max angular rate / tightest line).
- Applied: fallback gate timestamp = **apex + 150 ms** (apex =
  argmax ω within the window), not the window midpoint.
- Bench standard scenario: `S 52 1.5` (synth) ≈ the master-race
  rhythm; synth turn cadence matches (1.5 s period, 0.9 s turn).

## 6. Verification doctrine

Synthetic runs (`S` on the device, FW `sensor_synth.c`) produce
byte-exact ground truth: impacts at known ms with known amplitudes.
The phone's gate list must match truth exactly (confirmed at impact
times) + fallbacks at un-impacted turns (apex + 150 ms). Host port of
this estimator (tools side) predicts the app result offline when no
phone is on the bench.
