# Gate-side determination & two-pole blend — v0.3 (JP physics pinned)

*2026-10-12. Supersedes v0.1 (legs — wrong) and v0.2 (gate poles —
wrong). v0.3 = ski poles in the skier's hands + JP's stated physics.*

## 1. FINAL model (GS and all disciplines except slalom)

- **Left pole impacts the gate on a left turn; right pole on a right
  turn.** (JP 07:55, stated as fact — inside arm/pole reaches the
  gate; NOT cross-blocking.)
- **Pole plants:** slalom only, not every athlete; in GS almost
  nobody plants. → Plants are NOISE to tolerate, never signal. No
  plant model in the estimator.
- Gate estimation = acceleration + gyro turn analysis (as documented,
  and as implemented in the app since 1.52/1.54: ω turn windows +
  impacts; lateral la supports classification).
- **Slalom:** device-side definition becomes necessary (deferred).

## 2. Implementation decision (JP): device-agnostic sides

**Neglect left/right classification of devices/arms/poles entirely.**

- **Right turns = clockwise rotation; left turns = the others**
  (rotation sense about the gravity vertical — CW viewed from above).
- Gate side comes from the rotation direction of each turn — any
  device, no mounting-side config, no deduction needed.
- The two devices differ only in WHICH gates they impact-confirm:
  left-pole device confirms left-turn gates, right-pole device right-
  turn gates (б§1). The blend (either device's impact confirms a
  gate) is unaffected by device identity.
- Residual value of per-device attribution: which pole contacted each
  gate (blocking quality per side) — coaching metric, optional.

## 3. Signed rotation in the pipeline

Current ω analysis is direction-agnostic (|q_i · q_{i-1}|). Side
classification needs the SIGNED rotation about the gravity vertical:

  ω_signed = Δ(yaw about gravity up-axis) per frame
           CW (viewed from above) → right turn → right gate

Synth (yaw-only, z-up): signed Δqz. Real pole mounting: project
angular velocity on the gravity direction obtained from the
quaternion (mount-robust — pole tilt does not flip the definition).

## 4. Synth convention corrected (source, rides next build)

Faithful bench model: 'r' device impacts CW turns (heading decreasing
in the generator's z-up convention), 'l' device CCW. The previous
'r'/'l' parity was inverted w.r.t. this physics — fixed in
sensor_synth.c with this doc.

## 5. Status

- App/sim: sides switch to signed-rotation classification (todo,
  small: replaces the laY half-plane test where the axes convention
  could otherwise bite on real hardware).
- Blend: already proven direction-agnostic (runs 32/33, 35/35).
- The "cross-blocking constant" concept: DEAD. The device-side
  "deduction" question: MOOT for GS (sides come from rotation); for
  slalom, config required.
