@echo off
rem tunnel-run.cmd — the SYSTEM bridge (spawned by the watchdog).
rem ROOT CAUSE 09-27 (final): Windows ssh under SYSTEM rejected the
rem shared key ("UNPROTECTED PRIVATE KEY FILE" — v17ni's ACL entry
rem counts as "others" when SYSTEM runs ssh). Dedicated SYSTEM-only
rem key: id_ed25519_sgc_system (SYSTEM+Administrators ACL, no user).
rem Pure explicit args — no -F config (its IdentityFile would load the
rem old key first and re-trigger the warning), no alias indirection.
rem
rem v2 (11:05 UTC): retry loop. First observed success (sshd Accepted
rem publickey 10:50:10) died on a transient -R 9999 bind conflict
rem (zombie session held it 2 min). ExitOnForwardFailure + no retry =
rem bridge down for a full watchdog cycle. Loop: 5 attempts, 20 s
rem apart; ssh itself also retries connect 3x per attempt.
setlocal
set N=0
:loop
set /a N+=1
C:\Windows\System32\OpenSSH\ssh.exe -N -L 18789:127.0.0.1:18789 -R 9999:127.0.0.1:8080 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes -o ConnectionAttempts=3 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -o BatchMode=yes -o IdentitiesOnly=yes -i C:\Users\v17ni\.ssh\id_ed25519_sgc_system root@72.60.88.60 >> C:\Users\v17ni\tunnel-run.log 2>&1
echo [%DATE% %TIME%] attempt %N% exited rc=%ERRORLEVEL% >> C:\Users\v17ni\tunnel-run.log
if %N% geq 5 exit /b 1
rem healthy exits (server restarts, network blips) also land here —
rem brief pause then re-arm covers them all
timeout /t 20 /nobreak >nul
goto loop
