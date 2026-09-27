@echo off
rem DEPRECATED 2026-09-27 — superseded by make-bootproof.bat (boot task,
rem SYSTEM, no login). Kept only as history; its logon task is deleted by
rem make-bootproof step [2/4]. Do not use.
rem ----
rem One-time registration of the SGC node watchdog (no admin needed).
rem Creates a hidden logon task that keeps the VPS tunnel alive.
rem Run ONCE: register-watchdog.bat   (to undo: schtasks /delete /tn SGC-NodeWatchdog /f)
schtasks /create /tn SGC-NodeWatchdog /tr "powershell -NoProfile -ExecutionPolicy Bypass -File %~dp0node-watchdog.ps1" /sc onlogon /rl limited /f
echo.
echo Task created. Starting it now (no reboot needed)...
schtasks /run /tn SGC-NodeWatchdog
schtasks /query /tn SGC-NodeWatchdog
echo.
echo Log: %%USERPROFILE%%\node-watchdog.log
