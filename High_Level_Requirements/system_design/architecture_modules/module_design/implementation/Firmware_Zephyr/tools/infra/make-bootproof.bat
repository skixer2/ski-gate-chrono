@echo off
rem make-bootproof.bat — ONE-TIME setup of the boot-proof bench harness.
rem RUN AS ADMINISTRATOR (right-click -> Run as administrator).
rem Registers the watchdog as a BOOT task (SYSTEM, no login needed),
rem cleans up session-bound copies of the daemons, and starts it now.
rem Undo:  schtasks /delete /tn SGC-WatchdogBoot /f

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: please run this as Administrator.
    pause
    exit /b 1
)

echo [1/4] Stopping session-bound copies (they would fight the task)...
taskkill /f /im ssh.exe 2>nul
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='powershell.exe'\" | Where-Object {$_.CommandLine -like '*node-watchdog*'} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" 2>nul
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe' or Name='py.exe'\" | Where-Object {$_.CommandLine -like '*http.server*'} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" 2>nul

echo [2/4] Removing the old logon task if present...
schtasks /delete /tn SGC-NodeWatchdog /f 2>nul

echo [3/4] Registering the boot task (SYSTEM, onstart + resume + 15-min liveness)...
rem v2 (2026-10-13): import XML instead of /sc onstart — the CLI default leaves
rem ExecutionTimeLimit at 72h, which KILLED the watchdog every 72h of uptime
rem (Sep 30 + Oct 11 incidents). The XML sets PT0S (unlimited) + a resume
rem trigger (Power-Troubleshooter id 1) + a 15-min liveness repetition.
powershell -NoProfile -Command "Set-Content -Encoding Unicode -Path '%TEMP%\sgc-wd-utf16.xml' -Value ((Get-Content -Raw '%~dp0SGC-WatchdogBoot-task.v2.xml') -replace 'UTF-8', 'UTF-16' -replace 'Power-Troubleshooter/Operational', 'System')"
schtasks /create /tn SGC-WatchdogBoot /xml "%TEMP%\sgc-wd-utf16.xml" /f
if %errorlevel% neq 0 (
    echo ERROR: task creation failed.
    pause
    exit /b 1
)

echo [4/4] (Re)starting it now (no reboot needed)...
schtasks /end /tn SGC-WatchdogBoot 2>nul
schtasks /run /tn SGC-WatchdogBoot
timeout /t 5 >nul
schtasks /query /tn SGC-WatchdogBoot

echo.
echo DONE. From the next boot on, the bench harness self-restores
echo with nobody logged in. Log: C:\Users\v17ni\node-watchdog.log
pause
