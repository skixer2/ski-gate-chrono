# Device-side determination & two-pole blend — draft v0.2

*2026-10-12. v0.1 assumed gate-poles; WRONG twice. JP 07:34: the
devices are fixed on the SKIER'S SKI POLES — one per hand. v0.2 =
pole model. Everything below marked (ASSUMPTION) needs JP's confirm.*

## 1. Physical model

- **Device L on the left ski pole, device R on the right ski pole**
  (arm_side / ABC1 config = which pole at install).
- Both poles travel with the skier: descent (baro rises ✓ — the
  ramp model was right), every turn visible in RV from both (ASSUMPTION:
  pole swing tracks body rotation closely enough for the ω analysis).
- **Impacts on a pole device, three sources:**
  1. **Gate contact** — the pole (or that side's body) hits/brushes
     the gate pole. Only ONE pole's device feels it clearly.
  2. **Pole plants** — rhythmic, every turn cycle (ASSUMPTION: ~1
     plant per pole per gate cycle in GS; amplitude/plant-direction
     TBD by JP). Plants must NOT be counted as gates → the impact
     classifier needs a plant-vs-gate discriminator (amplitude?
     axial-vs-lateral direction? rhythm ~0.75 s vs gates 1.5 s?).
  3. Noise/other contacts.
- "Estimation" (JP's blend spec) = the turn analysis each device can
  do independently of impacts: BOTH poles see ALL turns; each device
  estimates all gate times (apex+150 ms class); its own pole only
  impact-confirms the gates THAT pole contacted.

## 2. The blend (JP's spec, verbatim reading now)

- R-device run: impacts (R-pole gate contacts + R plants) + estimates
  (all gates).
- L-device run: same, complementary.
- Merge: impact from EITHER pole confirms the gate; unclaimed
  estimates stay estimated. Gate side = turn direction.
- Bonus per gate: **which pole made contact** → the skier's blocking
  pattern (cross-block vs inside reach vs no block) — coaching data.

## 3. Device-side / technique DEDUCTION (JP's question, answered)

Do NOT assume right-pole→left-gates (cross-blocking) — technique
varies, masters often don't block at all. Instead, DEDUCE from the
paired data:

```
for each pole device:
    correlate its gate-classified impacts with turn direction
    → R-pole impacts on LEFT-turn gates  = cross-blocker
    → R-pole impacts on RIGHT-turn gates = inside-reach blocker
    → no gate-classified impacts         = no blocker (plants only)
```

Self-calibrating per skier per run; a config error (ABC1 set wrong)
shows up as the deduced side contradicting the configured side →
warn, trust the data. This also REPLACES any hardcoded
cross-blocking constant.

Prereq: reliable plant-vs-gate classification on each device (§1.2)
— the deduction above must run on gate-classified impacts only.

## 4. OPEN (JP)

1. Plant rhythm & signature in GS master racing: plants per gate
   cycle, per pole; rough amplitude vs a gate hit; axial (along pole)
   vs lateral (gate hit) — can they be separated by our LACC/RV?
2. Does the pole-device really see ALL turns cleanly (pole swing vs
   body rotation) — or does the ω estimate need pole-swing filtering?
3. Gate contact: is it always the POLE that touches (vs shin)?
   If shin contacts dominate for some skiers, the pole device feels
   them only via body coupling — weaker impacts. Threshold impact?
4. Start trigger (B4 "poles swinging 90°→60°"): confirm the start
   detector picture — pole angle w.r.t. gravity at the start push?

## 5. Bench plan once §4 is answered

Synth 'S' gets a pole model: plants every cycle (both), gate hits
only on one direction's turns per device (already parameterized) →
r + l runs → blend → all gates + per-pole attribution → deduction
printout = blocking pattern.
