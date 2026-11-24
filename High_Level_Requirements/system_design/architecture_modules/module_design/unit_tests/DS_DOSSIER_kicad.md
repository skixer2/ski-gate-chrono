# DOSSIER — KiCad 10.0.7 "automatically fixed" load error, unresolved after 3 rounds

## The exact error (verbatim, from the user's screenshot)

> "An error was found when loading the schematic that has been automatically
> fixed. Please save the schematic to repair the broken file or it may not be
> usable with other versions of KiCad."

Dialog title: "Information". Blue "i" icon. Single OK button.
This is a KiCad **repair/recovery** message, NOT the "created with an older
version" notice. Those are two different dialogs.

## Environment

- KiCad **10.0.7** on Windows (`C:\Program Files\KiCad\10.0\bin\kicad-cli.exe`)
- Project: `C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\pcb\sgc_p1\`
- Hierarchical schematic, 7 sheets: root `sgc_p1.kicad_sch` + 6 children
  (`sch_power`, `sch_mcu`, `sch_sensors`, `sch_storage`, `sch_boost_led`,
  `sch_connectors`)
- Generated programmatically by `gen_schematic.py` using the Python **kiutils**
  library, then post-processed

## What I have already fixed (proven by measurement)

### Round 1 — bare uuid tokens
kiutils emitted `(uuid a32dcc37-...)` with NO quotes. KiCad expects a string;
a bare token parses as a SYMBOL, type check fails, parser coerces = the repair.
Counted **972 bare uuids** at commit `ca52e33d`. Fixed with a post-write regex
pass; now **1364 quoted, 0 bare** in all 7 sheets.

### Round 2 — sheet format stamp
All 7 sheets were `(version 20231120)` = KiCad 8. Upgraded to `20260306`.

### Round 3 — orphan uuid in the project file
`sgc_p1.kicad_pro` had a self-contradiction:
```
"sheets":           [[9578d648-...,"Root"], ...]      <- correct (matches sheets)
"top_level_sheets": [{"uuid":"4921ceaf-...", ...}]    <- 4921ceaf exists in NO sheet
```
Fixed to `9578d648-...`. Verified 15/15 uuids resolve.

### Round 4 (just now) — symbol library was still KiCad 8
`sgc_p1.kicad_sym` was `20231120`. Migrated via `kicad-cli sym upgrade`
→ `20251024` ("Saving symbol library in updated format").

**The user reports the SAME exact error persists after round 2 and round 3.**

## Current state of every KiCad file

```
.kicad_sch    20260306   7 files   (current)
.kicad_sym    20251024   1 file    (current, just migrated)
.kicad_mod    20260206   3 files   (current?)
.kicad_mod    20221018   2 files   (old: D_SOD-1608_DFN1608D-2, SIT1532_CSP-4)
.kicad_pcb    20260206   1 file    (kicad-cli says "Board file was not updated" = current)
.kicad_pro    JSON valid, sheets array consistent
```

## Everything I have already PROVEN clean

1. `kicad-cli sch export netlist` → exit 0, no warning
2. Forced reparse: copy sheets, rewrite version to 20231120, `kicad-cli sch
   upgrade`, semantic diff → **"unchanged by KiCad"** for all 7 sheets
   (i.e. KiCad's own writer produces byte-identical structural content)
3. `kicad-cli sch erc` runs
4. No duplicate uuids within or across files
5. No missing `lib_symbols` entries
6. Parens balanced
7. Hierarchy: root references all 6 children by correct filename; each child has
   2 distinct instance paths: `/` and `/<box-uuid>` where box-uuid matches the
   root's `(sheet ... (uuid ...))` for that child
8. `.kicad_pro`: valid JSON, `sheets` array = 7 entries all resolving to real
   sheet uuids, no absolute paths, `sym-lib-table` and `fp-lib-table` present
   and pointing at files that exist
9. No lock files, no parent-directory `.kicad_pro` that could hijack the project

## THE CENTRAL PUZZLE

`kicad-cli` reports everything clean — and it **auto-repairs in memory on read**,
so it cannot reproduce a repair complaint. That is why CLI evidence has been
worthless three times. I need a defect that:
- KiCad's GUI/parser repairs on load
- `kicad-cli sch export/upgrade/erc` does NOT report

## Questions for you

1. Given the state above, what is the most likely REMAINING cause of a KiCad 10
   "automatically fixed" dialog on this hierarchy? Be specific.
2. Is there any known KiCad 10 defect class that survives all of: quoted uuids,
   correct version stamp, consistent .kicad_pro, upgraded .kicad_sym?
3. kiutils is known to be a KiCad 6/7/8-era library. What does it emit that
   KiCad 10's stricter parser must repair, that a KiCad-8-era reader accepts?
   Think about: `(symbol ... (pin ...))` shapes, `(effects ...)` fields,
   `(fill ...)` vs `(fill (type ...))`, `(stroke ...)` fields, `(justify ...)`,
   `lib_id` vs `lib_name`, `(instances ...)` blocks, `(property ...)` id/hide
   syntax, `(fields_autoplaced)`.
4. Is the "save to repair" wording a hint that KiCad wants the file REWRITTEN
   by the GUI specifically (i.e. some in-memory-only state)?
5. Concretely: what single command or minimal script would reproduce this
   dialog headlessly so I can confirm a fix rather than guess?

## Constraint

The user has been told "fixed" twice and it was not. Do not give me a
plausible-sounding theory. Give me defects that are *falsifiable against the
files*, and say what evidence would distinguish them.
