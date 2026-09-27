# SGC Bench Infrastructure (`tools/infra/`)

Everything here **keeps the bench alive** — it is NOT for interacting with
the firmware (those live one level up in `tools/`). Nothing here is run by
hand in daily work; the whole point is that it runs itself.

## The chain (who starts what)

```
Windows boot
  └─ scheduled task SGC-WatchdogBoot  (/sc onstart, SYSTEM, highest)
       └─ powershell node-watchdog.ps1     (this folder)
            ├─ 1. tunnel:  spawns tunnel-run.cmd   (this folder)
            │                └─ ssh -L 18789 (node link) + -R 9999 (file pull)
            │                     key: C:\ProgramData\sgc_tunnel_key
            │                     (SYSTEM-owned copy of ~/.ssh/id_ed25519_sgc_system)
            ├─ 2. files:   py -m http.server 8080  (serves C:\Users\v17ni)
            └─ 3. node:    openclaw node run       (USERPROFILE redirected)
```

## Files

| File | Role |
|---|---|
| `make-bootproof.bat` | ONE-TIME installer (admin): registers the boot task + starts it. Re-run after moving this folder — the task stores the absolute ps1 path. |
| `node-watchdog.ps1` | The 3-daemon loop (30 s probe, 2 fails → kill+respawn, 10-min heartbeat). |
| `tunnel-run.cmd` | The SYSTEM ssh bridge, v3: makes the SYSTEM-owned key copy on first run, 5×20 s retry loop vs. port-zombie conflicts. |
| `ssh-diag.bat` / `ssh-diag-run.cmd` | Forensic one-shot: run ssh under SYSTEM via a throwaway task, capture why it fails. |
| `register-watchdog.bat` | DEPRECATED (logon-task era). Kept as history. |

## Golden rules

1. **Never run `tunnel-run.cmd` by hand as v17ni** — it would create a
   v17ni-owned key copy in ProgramData and poison the `if not exist` guard.
2. **If `~/.ssh/id_ed25519_sgc_system` is ever regenerated**, delete
   `C:\ProgramData\sgc_tunnel_key` so the next run re-copies it.
3. After moving/renaming anything in this folder, re-run
   `make-bootproof.bat` (admin) — the scheduled task pins absolute paths.
4. Logs: `C:\Users\v17ni\node-watchdog.log` (watchdog),
   `tunnel-run.log` (ssh attempts), `http.log`, `nodehost.log`.

## Why the odd key dance (2026-09-27, for future archaeologists)

Win32-OpenSSH under SYSTEM rejects a private key whose *file owner* is a
normal user (v17ni), regardless of ACL. v3's fix: SYSTEM copies the key once
→ copy owner = SYSTEM → strict check passes. Details: git log of this folder,
2026-09-27 commits f668110 → 26fcc7d → 0491421.
