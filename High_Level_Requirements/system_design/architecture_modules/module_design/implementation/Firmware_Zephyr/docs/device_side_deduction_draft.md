# Draft: gate-side determination & the two-device blend — ASSUMPTIONS EXPOSED

*Draft v0.1, 2026-10-11, from JP's corrections: (a) not sure the
"outside leg blocks" mapping is correct, (b) the devices are fixed on
the POLES, not worn on legs. This document pins the physical model
first; the algorithm follows from it. ZioClaw's previous messages
assumed skier-worn shin devices — WRONG per (b).*

## 0. OPEN QUESTIONS (block the algorithm design)

1. **Where exactly are the two devices?** Both on poles — of the SAME
   gate (a GS gate = a pair of poles), or on two different gates
   (consecutive?), or start/finish only?
2. **What does a pole-mounted device record during a race?**
   - Its own pole being hit (impact) — presumably yes
   - The pole's swing after the hit (rotation vector shows the pole
     swinging — the same signal family as the B4 start-trigger
     "poles swinging 90°→60°")
   - Baro: constant (the pole is static) — so the descent-ramp runs
     we've been synthesizing describe WHAT trajectory? (A pole never
     descends.)
3. **Which skier?** If a pole device records a whole training session,
   it sees MANY skiers — is a "run" one skier's passage (how isolated?)
   and how are two devices' data associated to the same skier?
4. **What is "estimation" on a device whose pole was NOT hit?** The
   un-hit pole of a pair: what signal does the passing skier leave on
   it (vibration coupling through the snow? air? nothing)?

## 1. What the deduction must produce

Per JP: "the actual device side could be deduced from the two devices'
run data." Candidate meanings depending on §0 answers:

- **(A) Which pole of a pair was hit** (red/blue, course-left/right):
  the hit device knows its impact; the side label needs the device's
  mounting position — either configured at install, or deduced if the
  two devices of a pair see systematically different signals
  (approach direction asymmetry).
- **(B) Skier-carried + pole-reference hybrid:** skier device records
  the run (descent, turns, impacts at gates it brushed); pole devices
  give exact passage timestamps (the Gold-tier pressure/RFID
  cross-check in the app). "Deduction" = matching skier-run gates to
  pole-device timestamps, and the skier device's impact-vs-turn
  correlation then deduces which LINE (right/left gates) the skier
  was skiing — no leg physics involved.

## 2. The flawed claim, retracted

> "+ω (left) turns → right-leg device (right leg is outside on left
> turns)"

This assumed devices strapped to the skier's shins. If devices are on
poles, there is no leg, no outside-leg blocking, and turn-direction
correlation of impacts measures something else entirely. RETRACTED
pending §0.

## 3. What stands regardless of the model

- Both devices sync to the same phone clock at connect → shared time
  base for any cross-device association (unchanged).
- The bench synthetic chain (packer, store, download, decode) is
  model-agnostic; only the GENERATOR's physics changes once the model
  is pinned.
- Impact-first association within one run (App 1.52+) is unchanged —
  it does not care what the impacts physically are.

## 4. Next step

JP answers §0 (or corrects the questions) → this doc gets the real
model → the generator + blend get rewritten to match → re-proven on
the bench.
