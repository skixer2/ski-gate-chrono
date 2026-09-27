@echo off
rem tunnel-run.cmd v3 — the SYSTEM bridge (spawned by the watchdog).
rem
rem ROOT CAUSES (09-27, in order of discovery):
rem  1. Shared key + -F config: v17ni ACL entry = "others" under SYSTEM
rem     -> UNPROTECTED. Fixed: dedicated key, explicit args.
rem  2. Key file OWNER = v17ni: Win32-OpenSSH under SYSTEM requires the
rem     key file owner to be SYSTEM/Admins, regardless of ACL. Fixed:
rem     copy the key AS SYSTEM -> the copy's owner is SYSTEM -> check
rem     passes. Copy lives in C:\ProgramData (SYSTEM-writable).
rem  3. Transient -R 9999 bind conflicts (zombie sessions hold the
rem     remote bind up to 2 min after a kill). Fixed: retry loop.
rem NOTE: if the source key is ever regenerated, delete
rem       C:\ProgramData\sgc_tunnel_key so this recreates it.
rem NOTE: do NOT run this by hand as v17ni — it would create a
rem       v17ni-owned copy and poison the guard.
setlocal enabledelayedexpansion

set KEYSRC=C:\Users\v17ni\.ssh\id_ed25519_sgc_system
set KEYDST=C:\ProgramData\sgc_tunnel_key

if not exist %KEYDST% (
  copy /Y %KEYSRC% %KEYDST% >nul 2>&1
  icacls %KEYDST% /inheritance:r >nul 2>&1
  icacls %KEYDST% /grant:r "SYSTEM:F" "Administrators:F" >nul 2>&1
  echo [%DATE% %TIME%] key copy created >> C:\Users\v17ni\tunnel-run.log
)

set N=0
:loop
set /a N+=1
C:\Windows\System32\OpenSSH\ssh.exe -N -L 18789:127.0.0.1:18789 -R 9999:127.0.0.1:8080 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes -o ConnectionAttempts=3 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -o BatchMode=yes -o IdentitiesOnly=yes -i %KEYDST% root@72.60.88.60 >> C:\Users\v17ni\tunnel-run.log 2>&1
echo [%DATE% %TIME%] attempt !N! exited rc=!ERRORLEVEL! >> C:\Users\v17ni\tunnel-run.log
if !N! geq 5 exit /b 1
timeout /t 20 /nobreak >nul
goto loop
