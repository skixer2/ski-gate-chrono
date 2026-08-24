# TC-2026-08-22-001 — LDC DRDY timing race + System Off wake (FW 5.28–5.32)

**Status:** PASS (FW 5.32 — switched to piezo button)  
**Closed:** 2026-08-24  
**Parent:** TC-2026-08-15-001

## Summary

LDC1612 INTB stays LOW until DATA0 is read (DRDY cleared). DRDY fires every
~819 µs. nRF52 refuses to enter System Off if DETECT already asserted.

Multiple fix attempts (FW 5.28–5.31) all failed — the race is inherent to
the LDC1612 DRDY mechanism + nRF52 GPIO SENSE.

## Fix

**FW 5.32:** Replaced LDC1612 with piezo push button on P0.02 for arming + System Off wake.
- Langir 16mm piezo, momentary NO, internal pull-up, active-low
- GPIO edge interrupt (FALLING) with 20 ms debounce
- System Off wake via GPIO SENSE_LOW — no DRDY race
- LDC1612 kept for diagnostics only (`c` recal, `z` diag, `?` status)

## Follow-up

TC-2026-08-24-001 — LDC was still in boot path (not actually removed from `setup()`),
causing button dead + auto-arm. Fixed in FW 5.34.

## Archived from

TEST_LEDGER.md §2 (active session), 2026-08-22 through 2026-08-24.
