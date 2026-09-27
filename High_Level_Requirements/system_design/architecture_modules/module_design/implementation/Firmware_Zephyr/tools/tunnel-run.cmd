@echo off
rem tunnel-run.cmd — the SYSTEM bridge (spawned by the watchdog).
rem ROOT CAUSE 09-27 (final): Windows ssh under SYSTEM rejected the
rem shared key ("UNPROTECTED PRIVATE KEY FILE" — v17ni's ACL entry
rem counts as "others" when SYSTEM runs ssh). Dedicated SYSTEM-only
rem key: id_ed25519_sgc_system (SYSTEM+Administrators ACL, no user).
rem Pure explicit args — no -F config (its IdentityFile would load the
rem old key first and re-trigger the warning), no alias indirection.
C:\Windows\System32\OpenSSH\ssh.exe -N -L 18789:127.0.0.1:18789 -R 9999:127.0.0.1:8080 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -o BatchMode=yes -i C:\Users\v17ni\.ssh\id_ed25519_sgc_system root@72.60.88.60 > C:\Users\v17ni\tunnel-run.log 2>&1
