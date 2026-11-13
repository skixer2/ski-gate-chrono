# TC-2026-11-13-002 — Solo build/flash harness certification (COMPLETE)

| Field | Value |
|---|---|
| **Status** | PASS (b0259b first-try; b0259 passed after 3 surviving tree-kills) |
| **FW** | 0.25.8 (94b617d), fresh dirs b0259 / b0259b |
| **Artifacts** | hexsafe: C:\sgc\hexsafe\{0259,0259b}.signed.hex + C:\Users\v17ni\hexsafe\ mirrors; SHA256 b0259 F6DF672E…43AA278, b0259b 4DFA7F5E…F8BAB (cross-run difference = build timestamps) |

## Protocol proven (kill-model, measured)
- Plain invoke with NO explicit timeout; the invoking session MUST stay open — turn-end and node-reconnect kill the build tree (3 kills survived across b0259 attempts).
- Killed trees leave stale .ninja_lock (next ninja dies silently at [0/8]) and invisible log-handle locks (use a fresh log filename per resume).
- Direct cmake needs the NCS toolchain PATH (bare ccache) — direct_build.bat.
- Poll LOG FILES from separate calls; never trust the invoke return (bridge churn).
- Gates: BUILD_EXIT_0 + dfu manifest version_MCUBOOT + Intel-HEX decode (gate_version.py). findstr on hex = structurally unreliable (4 consecutive false negatives).
- b0259b cold-dir pass: 4.7 min first-try (warm ccache), zero kills.

## Open (blocked)
- Unattended one-shot (autoflash.bat + schtasks) blocked by PC spawn breakage: schtasks/WMI-spawned processes die at DLL-init (0xC0000142). Diagnostic staged: spawn-diag.ps1 (c106cfd).
