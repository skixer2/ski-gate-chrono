# SGC — Current Status

_Updated 2026-11-13. Canonical one-page state: firmware, builds, schematics, bench, blockers._

## Firmware
- Current: **FW 0.25.8** (commit 94b617d) — VERSION file + app_version.h synced in value; MCUboot-signed image version matches the app banner (0.25.8+0). Fixes the pre-phone-OTA version-reporting debt.
- **On device: 0.25.8** — flashed 2026-11-12 via SWD from the hexsafe copy, OpenOCD "Verified OK"; boot banner confirms (MCUboot "Image version: v0.25.8"; app "v0.25.8-94b617d"). Advertising as "SGC JP", telemetry sane, battery 100% stable.
- Recent: 0.25.7 CD-polarity fix (battery-%-on-battery, JP hardware-verified 11-10); 0.25.8 version-source sync (11-12).

## Builds
- Certified dirs: b0258 (source of the flashed image), b0259, b0259b (clean-room cold rebuild, first-try pass).
- Protocol (proven 11-12): plain invoke with NO explicit timeout; the invoking session MUST stay open (turn-end, node-reconnect and some cap-paths kill the build tree); poll the log file from separate calls; delete stale .ninja_lock after any tree kill; fresh log filename per resume (killed trees leave invisible log-handle locks); direct cmake needs the NCS toolchain PATH (tools/direct_build.bat on the PC).
- Archive: C:\sgc\hexsafe\{0257,0258,0259,0259b}.signed.hex + C:\Users\v17ni\hexsafe\ mirrors — SHA256-verified per file (b0259b: 4DFA7F5E...F8BAB, b0259: F6DF672E...43AA278; hashes differ across runs by design — build timestamps).
- Hex content gate: Intel-HEX DECODE only (tools/gate_version.py on the PC). findstr on hex is structurally unreliable (4 consecutive false negatives on valid hex).

## Schematics (P1)
- validate_sgc_p1.py: **FULLY GREEN** (2026-11-13, includes JP's D5 rework committed this day). Census 70 components, 359 pin connections, 72 nets — syntax + geometry + netlist + census all green. Nov-9 fails (R42/R43/C24/C25 placement, D5 pin wiring) all resolved.

## Open blockers / watch items
- Detached-spawn breakage on the PC: schtasks- and WMI-spawned processes die at DLL-init (0xC0000142); only the node-exec path spawns reliably. The staged one-shot autoflash.bat is blocked on this. Diagnosis tooling: spawn-diag.ps1 (c106cfd); the 11-12/13 14h node outage was root-caused and fixed in watchdog v2.7 (1635eab).
- zpull.bat has a latent quoting bug (git -C "%~dp0"); use the explicit repo path C:/Users/v17ni/Documents/projects/ski-gate-chrono.
- SSH relay key id_ed25519_sgc ACL was blocking ALL pulls (incl. JP's shell) — fixed 11-12 (single-ACE v17ni:F). VPS->PC content transfer via git bundle (hash-verified) is the proven workaround while the exec context is SYSTEM.
