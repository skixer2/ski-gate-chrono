@echo off
rem tunnel-run.cmd v4 (2026-10-08) -- split forwards: the node link can no
rem longer be taken hostage by the file channel.
rem
rem INCIDENT 2026-10-08: PC died abruptly -> VPS sshd kept the old tunnel
rem session alive in a TCP-retransmit tail (~20 min with ClientAliveInterval
rem 60 x3) still holding remote port 9999. v3 ran -L 18789 (node link) and
rem -R 9999 (file channel) in ONE ssh with ExitOnForwardFailure=yes, so every
rem watchdog respawn authenticated OK then died on the -R bind for the whole
rem window. Node link stayed down ~20 min until JP bridged manually with a
rem -L-only password ssh -- which is exactly the v4 MAIN behavior.
rem
rem v4 design:
rem   MAIN ssh = -L 18789 only (node link; watchdog probe target).
rem   AUX  ssh = -R 9999 only (file channel; own retry loop, capped life,
rem              re-spawned by the watchdog whenever tunnel-run restarts).
rem A poisoned/zombie 9999 bind can NEVER again block the node link; AUX
rem self-heals once the VPS reaps the dead session (<=~20 min worst case).
rem
rem NOTE: do NOT run by hand as v17ni -- it would create a v17ni-owned key
rem       copy and poison the SYSTEM guard (see v3 notes).
setlocal enabledelayedexpansion

set KEYSRC=C:\Users\v17ni\.ssh\id_ed25519_sgc_system
set KEYDST=C:\ProgramData\sgc_tunnel_key
set SSHBASE=C:\Windows\System32\OpenSSH\ssh.exe
set COMMON=-o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ConnectionAttempts=3 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -o BatchMode=yes -o IdentitiesOnly=yes
set AUXTRIES=60

if not exist %KEYDST% (
  copy /Y %KEYSRC% %KEYDST% >nul 2>&1
  icacls %KEYDST% /inheritance:r >nul 2>&1
  icacls %KEYDST% /grant:r "SYSTEM:F" "Administrators:F" >nul 2>&1
  echo [%DATE% %TIME%] key copy created >> C:\Users\v17ni\tunnel-run.log
)

if /i "%~1"=="AUX" goto aux

echo [%DATE% %TIME%] tunnel-run v4 start (main+aux spawn) >> C:\Users\v17ni\tunnel-run.log
start "sgc-tunnel-aux" /min cmd /c "%~f0" AUX

set N=0
:mainloop
set /a N+=1
%SSHBASE% -N -L 18789:127.0.0.1:18789 %COMMON% -o ExitOnForwardFailure=yes -i %KEYDST% root@72.60.88.60 >> C:\Users\v17ni\tunnel-run.log 2>&1
echo [%DATE% %TIME%] main attempt !N! exited rc=!ERRORLEVEL! >> C:\Users\v17ni\tunnel-run.log
if !N! geq 5 exit /b 1
timeout /t 20 /nobreak >nul
goto mainloop

:aux
set /a A+=1
%SSHBASE% -N -R 9999:127.0.0.1:8080 %COMMON% -o ExitOnForwardFailure=yes -i %KEYDST% root@72.60.88.60 >> C:\Users\v17ni\tunnel-aux.log 2>&1
echo [%DATE% %TIME%] aux attempt !A! exited rc=!ERRORLEVEL! >> C:\Users\v17ni\tunnel-aux.log
if !A! geq %AUXTRIES% exit /b 1
timeout /t 30 /nobreak >nul
goto aux
