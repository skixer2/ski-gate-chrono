# ⚠️ This folder has an alias: C:\sgc

`C:\sgc` is a Windows directory **junction** pointing HERE:

    C:\Users\<user>\Documents\projects\ski-gate-chrono\High_Level_Requirements
        \system_design\architecture_modules\module_design\implementation
        \Firmware_Zephyr

## Why it exists
The repo tree intentionally replicates the **V-model** (requirements → design →
implementation), which makes paths long. Windows caps paths at 260 chars, and
Zephyr build trees nest deep enough to hit that limit from this folder.

`C:\sgc` gives the toolchain a short handle: build from `C:\sgc\apps\<name>`
instead of the full path. Nothing is copied — one set of files, one git repo.

## Rules
- **Open/build in VS Code via `C:\sgc\apps\...`** (short paths, happy CMake)
- Git, TortoiseGit, scripts: either path works
- Created 2026-09-18 (`mklink /J C:\sgc <this folder>`)
- Check target: `(Get-Item C:\sgc).Target` in PowerShell
- If you ever see `C:\sgc` on a machine where this folder doesn't exist:
  that's a leftover — delete it (`rmdir C:\sgc` removes ONLY the junction,
  never the real folder or its contents)

## Do NOT
- do not delete the *real* folder thinking it's the alias
- do not put files "in C:\sgc" expecting them outside the repo —
  everything written there lands in this git tree
